#include <config.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/atomics.h>
#include <pins-lib/modules/pnml-pins.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/pins-util.h>

#define NUM_TRANSS SIgetCount(context->pnml_transs)
#define NUM_PLACES SIgetCount(context->pnml_places)
#define NUM_ARCS SIgetCount(context->pnml_arcs)

static int noack = 0;

static void
pnml_popt(poptContext con,
         enum poptCallbackReason reason,
         const struct poptOption* opt,
         const char* arg, void* data)
{
    (void) con; (void) opt; (void) arg; (void) data;
    switch(reason){
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        GBregisterLoader("pnml", PNMLloadGreyboxModel);

        if (noack != 0 && noack != 1 && noack != 2) {
            Abort("Option --noack only accepts value 1 or 2");
        }

        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort("unexpected call to pnml_popt");
}

struct poptOption pnml_options[]= {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&pnml_popt, 0 , NULL , NULL },
    { "noack" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &noack , 0 , "Set Noack order to apply","<[1|2]>" },
    POPT_TABLEEND
};

typedef enum { ARC_IN, ARC_OUT, ARC_LAST } arc_dir_t;

typedef struct arc {
    int transition;
    int place;
    int num;
    arc_dir_t type;
} arc_t;

typedef struct transition {
    int start;
    int in_arcs;
    int out_arcs;
} transition_t;

typedef struct pnml_context {
    xmlChar* name;
    int num_safe_places;
    bitvector_t safe_places;
    transition_t* transitions;
    arc_t* arcs;

    string_index_t pnml_places;
    string_index_t pnml_transs;
    string_index_t pnml_arcs;

    xmlNode* toolspecific;
    uint8_t safe;

    guard_t** guards_info;
    int num_guards;
    arc_t** guards; // not allowed to access transition!
    int num_in_arcs;
} pnml_context_t;

static int max_token_count = 0;

static void
pnml_exit(model_t model)
{
    (void) model;
    Warning(info, "max token count: %u", max_token_count);
}

static int
get_successor_long(void* model, int t, int* in, void
(*callback)(void* arg, transition_info_t* transition_info, int* out, int* cpy), void* arg)
{
    pnml_context_t* context = GBgetContext(model);

    int out[NUM_PLACES];
    memcpy(out, in, sizeof(int[NUM_PLACES]));

    int overflown = 0;
    int max = 0;
    for (arc_t* arc = context->arcs + context->transitions[t].start; arc->transition == t; arc++) {
        switch(arc->type) {
            case ARC_IN: {
                // check precondition
                if (out[arc->place] - arc->num < 0) return 0; // underflow (token count < 0)

                // remove tokens
                out[arc->place] -= arc->num;
            }
            break;
            case ARC_OUT: {
                // establish postcondition
                if (!bitvector_is_set(&(context->safe_places), arc->place)) {

                    // detect overflow and report later (only if transition is enabled)
                    overflown |= out[arc->place] > INT_MAX - arc->num;

                    // add tokens
                    out[arc->place] += arc->num;

                    // record max token count
                    if (out[arc->place] > max) max = out[arc->place];
                } else out[arc->place] = 1;
            }
            break;
            case ARC_LAST: break;
        }
    }

    if (overflown) Abort("max token count exceeded");

    volatile int* ptr;
    do {
        ptr = &max_token_count;
        if (max <= *ptr) break;
    } while (!cas(ptr, *ptr, max));

    transition_info_t transition_info = { (int[1]) { t }, t, 0 };
    callback(arg, &transition_info, out, NULL);

    return 1;
}

static int
get_successor_short(void* model, int t, int* in, void
(*callback)(void* arg, transition_info_t* transition_info, int* out, int* cpy), void* arg)
{
    pnml_context_t* context = GBgetContext(model);

    HREassert(context->num_safe_places == 0, "get_successor_short not compatible with safe places");

    const int num_writes = dm_ones_in_row(GBgetDMInfoMustWrite(model), t);

    int out[num_writes];
    memcpy(out, in, sizeof(int[num_writes]));
    int* place = out;

    int overflown = 0;
    int max = 0;
    for (arc_t* arc = context->arcs + context->transitions[t].start; arc->transition == t; arc++) {
        switch(arc->type) {
            case ARC_IN: {
                // check precondition
                if (*place - arc->num < 0) return 0; // underflow (token count < 0)

                // remove tokens
                *place -= arc->num;
            }
            break;
            case ARC_OUT: {
                // establish postcondition

                // detect overflow and report later (only if transition is enabled)
                overflown |= *place > INT_MAX - arc->num;

                // add tokens
                *place += arc->num;

                // record max token count
                if (*place > max) max = *place;
            }
            break;
            case ARC_LAST: break;
        }
        if (arc->place != (arc + 1)->place) place++;
    }

    if (overflown) Abort("max token count exceeded");

    volatile int* ptr;
    do {
        ptr = &max_token_count;
        if (max <= *ptr) break;
    } while (!cas(ptr, *ptr, max));

    transition_info_t transition_info = { (int[1]) { t }, t, 0 };
    callback(arg, &transition_info, out, NULL);

    return 1;
}

static int
get_update_long(void* model, int t, int* in, void
(*callback)(void* arg, transition_info_t* transition_info, int* out, int* cpy), void* arg)
{
    pnml_context_t* context = GBgetContext(model);

    int out[NUM_PLACES];
    memcpy(out, in, sizeof(int[NUM_PLACES]));

    int overflown = 0;
    int max = 0;
    for (arc_t* arc = context->arcs + context->transitions[t].start; arc->transition == t; arc++) {
        switch(arc->type) {
            case ARC_IN: {
                /* If there is an underflow then this transition is disabled,
                 * while it should not be, since this is the update function. */
                HREassert(out[arc->place] - arc->num >= 0, "transition should not have been disabled");

                // remove tokens
                out[arc->place] -= arc->num;
            }
            break;
            case ARC_OUT: {
                // establish postcondition
                if (!bitvector_is_set(&(context->safe_places), arc->place)) {

                    // detect overflow and report later (only if transition is enabled)
                    overflown |= out[arc->place] > INT_MAX - arc->num;

                    // add tokens
                    out[arc->place] += arc->num;

                    // record max token count
                    if (out[arc->place] > max) max = out[arc->place];
                } else out[arc->place] = 1;
            }
            break;
            case ARC_LAST: break;
        }
    }

    if (overflown) Abort("max token count exceeded");

    volatile int* ptr;
    do {
        ptr = &max_token_count;
        if (max <= *ptr) break;
    } while (!cas(ptr, *ptr, max));

    transition_info_t transition_info = { (int[1]) { t }, t, 0 };
    callback(arg, &transition_info, out, NULL);

    return 1;
}

static int
get_update_short(void* model, int t, int* in, void
(*callback)(void* arg, transition_info_t* transition_info, int* out, int* cpy), void* arg)
{
    pnml_context_t* context = GBgetContext(model);

    HREassert(context->num_safe_places == 0, "get_update_short not compatible with safe places");

    const int num_writes = dm_ones_in_row(GBgetDMInfoMustWrite(model), t);

    int out[num_writes];
    memcpy(out, in, sizeof(int[num_writes]));
    int* place = out;

    int overflown = 0;
    int max = 0;
    for (arc_t* arc = context->arcs + context->transitions[t].start; arc->transition == t; arc++) {
        switch(arc->type) {
            case ARC_IN: {
                // check precondition

                /* If there is an underflow then this transition is disabled,
                 * while it should not be, since this is the update function. */
                HREassert(*place - arc->num >= 0, "transition should not have been disabled");

                // remove tokens
                *place -= arc->num;
            }
            break;
            case ARC_OUT: {
                // establish postcondition

                // detect overflow and report later (only if transition is enabled)
                overflown |= *place > INT_MAX - arc->num;

                // add tokens
                *place += arc->num;

                // record max token count
                if (*place > max) max = *place;
            }
            break;
            case ARC_LAST: break;
        }
        if (arc->place != (arc + 1)->place) place++;
    }

    if (overflown) Abort("max token count exceeded");

    volatile int* ptr;
    do {
        ptr = &max_token_count;
        if (max <= *ptr) break;
    } while (!cas(ptr, *ptr, max));

    transition_info_t transition_info = { (int[1]) { t }, t, 0 };
    callback(arg, &transition_info, out, NULL);

    return 1;
}

static int
get_label_long(model_t model, int label, int* src) {
    pnml_context_t* context = GBgetContext(model);
    HREassert(label < context->num_guards, "unknown state label");
    const arc_t* arc = context->guards[label];
    return src[arc->place] >= arc->num;
}

static int
get_label_short(model_t model, int label, int* src) {
    pnml_context_t* context = GBgetContext(model);
    HREassert(label < context->num_guards, "unknown state label");
    return src[0] >= context->guards[label]->num;
}

static void
get_labels(model_t model, sl_group_enum_t group, int* src, int* label) {
    pnml_context_t* context = GBgetContext(model);
    if (group == GB_SL_GUARDS || group == GB_SL_ALL) {
        for (int i = 0; i < context->num_guards; i++) {
            const arc_t* arc = context->guards[i];
            label[i] = src[arc->place] >= arc->num;
        }
    }
}

static void
find_ids(xmlNode* a_node, pnml_context_t* context)
{
    for (xmlNode* node = a_node; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(node->name, (const xmlChar*) "place") == 0) {
                const xmlChar* id = xmlGetProp(node, (const xmlChar*) "id");
                if (SIput(context->pnml_places, (char*) id) == SI_INDEX_FAILED) Abort("duplicate place");
            } else if(xmlStrcmp(node->name, (const xmlChar*) "transition") == 0) {
                const xmlChar* id = xmlGetProp(node, (const xmlChar*) "id");
                if (SIput(context->pnml_transs, (char*) id) == SI_INDEX_FAILED) Abort("duplicate transition");
            } else if(xmlStrcmp(node->name, (const xmlChar*) "arc") == 0) {
                const xmlChar* id = xmlGetProp(node, (const xmlChar*) "id");
                if (SIput(context->pnml_arcs, (char*) id) == SI_INDEX_FAILED) Abort("duplicate arc");
            } else if(context->toolspecific == NULL && xmlStrcmp(node->name, (const xmlChar*) "toolspecific") == 0) context->toolspecific = node;
        }
        find_ids(node->children, context);
    }
}

static void
parse_toolspecific(xmlNode* a_node, pnml_context_t* context)
{
    for (xmlNode* node = a_node; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(node->name, (const xmlChar*) "structure") == 0) {
                xmlChar* val = xmlStrdup(xmlGetProp(node, (const xmlChar*) "safe"));
                xmlChar* lower = val;
                for ( ; *val; ++val) *val = tolower(*val);
                context->safe = xmlStrcmp(lower, (const xmlChar*) "true") == 0;
            } else if (xmlStrcmp(node->name, (const xmlChar*) "places") == 0 && context->safe) {
                xmlChar* places = xmlStrdup(xmlNodeGetContent(node));
                // weird format; change all newlines to spaces
                for (size_t i = 0; i < strlen((char*) places); i++) {
                    if (places[i] == '\n') places[i] = ' ';
                }
                char* place;
                while ((place = strsep((char**) &places, " ")) != NULL) {
                    if (strlen(place) == 0) continue; // weird again
                    int num;
                    if ((num = SIlookup(context->pnml_places, place)) == SI_INDEX_FAILED) Abort("missing place: %s", place);
                    bitvector_set(&(context->safe_places), num);
                    context->num_safe_places++;
                }
            }
        }
        parse_toolspecific(node->children, context);
    }
}

static void
parse_net(xmlNode* a_node, model_t model, int* init_state[])
{
    pnml_context_t* context = GBgetContext(model);
    for (xmlNode* node = a_node; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(node->name, (const xmlChar*) "text") == 0) {
                const xmlChar* id = xmlGetProp(node->parent->parent, (const xmlChar*) "id");
                if (xmlStrcmp(node->parent->name, (const xmlChar*) "name") == 0) {
                    if (xmlStrcmp(node->parent->parent->name, (const xmlChar*) "net") == 0) context->name = xmlStrdup(xmlNodeGetContent(node));
                    else if (xmlStrcmp(node->parent->parent->name, (const xmlChar*) "transition") == 0) {
                        int num;
                        if ((num = SIlookup(context->pnml_transs, (char*) id)) == SI_INDEX_FAILED) Abort("missing transition");
                        pins_chunk_put_at (model, lts_type_find_type(GBgetLTStype(model), "action"), chunk_str((char*) xmlNodeGetContent(node)), num);
                    } else if (xmlStrcmp(node->parent->parent->name, (const xmlChar*) "place") == 0) {
                        int num;
                        if ((num = SIlookup(context->pnml_places, (char*) id)) == SI_INDEX_FAILED) Abort("missing place");
                        lts_type_set_state_name(GBgetLTStype(model), num, (char*) xmlNodeGetContent(node));
                    }
                } else if (xmlStrcmp(node->parent->name, (const xmlChar*) "initialMarking") == 0) {
                    int num;
                    if ((num = SIlookup(context->pnml_places, (char*) id)) == SI_INDEX_FAILED) Abort("missing place");
                    const int val = atoi((char*) xmlNodeGetContent(node));
                    (*init_state)[num] = val;
                    if (val > max_token_count) max_token_count = val;
                } else if (xmlStrcmp(node->parent->name, (const xmlChar*) "inscription") == 0) {
                    int num;
                    if ((num = SIlookup(context->pnml_arcs, (char*) id)) == SI_INDEX_FAILED) Abort("missing arc");
                    context->arcs[num].num = atoi((char*) xmlNodeGetContent(node));
                }
            } else if (xmlStrcmp(node->name, (const xmlChar*) "arc") == 0) {
                int num;
                const xmlChar* id = xmlGetProp(node, (const xmlChar*) "id");
                if ((num = SIlookup(context->pnml_arcs, (char*) id)) == SI_INDEX_FAILED) Abort("missing arc");
                context->arcs[num].num = 1;

                const xmlChar* source = xmlGetProp(node, (const xmlChar*) "source");
                const xmlChar* target = xmlGetProp(node, (const xmlChar*) "target");
                int source_num;
                int target_num;
                if ((source_num = SIlookup(context->pnml_places, (char*) source)) != SI_INDEX_FAILED &&
                    (target_num = SIlookup(context->pnml_transs, (char*) target)) != SI_INDEX_FAILED) {
                    // this is an in arc
                    context->arcs[num].transition = target_num;
                    context->arcs[num].place = source_num;
                    context->arcs[num].type = ARC_IN;
                    context->transitions[target_num].in_arcs++;
                    context->num_in_arcs++;

                    dm_set(GBgetDMInfoRead(model), target_num, source_num);
                    dm_set(GBgetDMInfoMustWrite(model), target_num, source_num);
                    dm_set(GBgetDMInfo(model), target_num, source_num);
                    dm_set(GBgetMatrix(model, GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS)), target_num, source_num);
                } else if ((source_num = SIlookup(context->pnml_transs, (char*) source)) != SI_INDEX_FAILED &&
                    (target_num = SIlookup(context->pnml_places, (char*) target)) != SI_INDEX_FAILED) {
                    // this is an out arc
                    context->arcs[num].transition = source_num;
                    context->arcs[num].place = target_num;
                    context->arcs[num].type = ARC_OUT;
                    context->transitions[source_num].out_arcs++;

                    if (!bitvector_is_set(&(context->safe_places), target_num)) {
                        dm_set(GBgetDMInfoRead(model), source_num, target_num);
                        dm_set(GBgetMatrix(model, GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS)), source_num, target_num);
                    }
                    dm_set(GBgetDMInfoMustWrite(model), source_num, target_num);
                    dm_set(GBgetDMInfo(model), source_num, target_num);
                } else Abort("incorrect net");
            }
        }
        parse_net(node->children, model, init_state);
    }
}

/*
 * Sorts arcs:
 *  1.  first in ascending order of transition number,
 *  2.  then in ascending order of place number,
 *  3.  then in-arcs before out-arcs
 *
 *  1.  is necessary for any next-state function,
 *      so that it is known which arc belongs to which transition group, and
 *      allows to put many arcs of a group on the CPU's cache line.
 *  2.  is necessary for any short next-state function,
 *      since we only work with short vectors; arc->place is useless.
 *  3.  is necessary for any next-state function,
 *      to check the precondition of a transition,
 *      before establishing the postcondition.
 */
static int
compare_arcs(const void *a, const void *b)
{
    const arc_t* aa = (const arc_t*) a;
    const arc_t* ab = (const arc_t*) b;

    if (aa->transition < ab->transition) return -1;
    if (aa->transition > ab->transition) return 1;
    if (aa->place < ab->place) return -1;
    if (aa->place > ab->place) return 1;

    if (aa->type != ab->type) return aa->type == ARC_IN ? -1 : 1;
    return 0;
}

static void
attach_arcs(pnml_context_t* context)
{
    if (NUM_TRANSS > 0) {
        qsort(context->arcs, NUM_ARCS, sizeof(arc_t), compare_arcs);

        context->guards_info = RTmalloc(NUM_TRANSS * sizeof(guard_t*));
        guard_t* guards;
        guards = RTmalloc(sizeof(int[NUM_TRANSS]) + sizeof(int[context->num_in_arcs]));
        context->guards = RTmalloc(sizeof(arc_t*) * context->num_in_arcs);

        string_index_t guard_unique = SIcreate();
        for (arc_t* arc = context->arcs; arc->type != ARC_LAST; arc++) {
            const int num = arc - context->arcs;
            if (num == 0 || arc->transition != (arc - 1)->transition) {
                context->transitions[arc->transition].start = num;

                guards->count = 0;
                context->guards_info[arc->transition] = guards;
                guards += 1 + context->transitions[arc->transition].in_arcs;
            }
            if (arc->type == ARC_IN) {
                const char* u = "%d%d";
                char unique[snprintf(NULL, 0, u, arc->place, arc->num) + 1];
                sprintf(unique, u, arc->place, arc->num);
                int duplicate;
                if ((duplicate = SIput(guard_unique, unique)) != SI_INDEX_FAILED) context->guards[duplicate] = arc;
                context->guards_info[arc->transition]->guard[context->guards_info[arc->transition]->count++] = duplicate;
            }
        }
        context->num_guards = SIgetCount(guard_unique);
        SIdestroy(&guard_unique);
        context->guards = RTrealloc(context->guards, sizeof(arc_t*) * context->num_guards);
    }
}

/*
 * if type == ARC_OUT, compute |.t \cap S|
 * if type == ARC_IN, compute |t. \cap S|
 */
static inline double
noack_num_pre_or_post(pnml_context_t* context, int transition, arc_dir_t type, int* assigned)
{
    const transition_t* transs = context->transitions;
    double result = .0;

    for (arc_t* arc = context->arcs + transs[transition].start; arc->transition == transition; arc++) {
        if (arc->type == type && assigned[arc->place]) result += 1.0;
    }

    return result;
}

static inline double
noack_g1(pnml_context_t* context, int transition, int* assigned)
{
    const double weight = noack_num_pre_or_post(context, transition, ARC_IN, assigned);
    if (weight == .0) return .1;
    else return weight;
}

static inline double
noack_h(pnml_context_t* context, int transition, int* assigned)
{
    const double weight = noack_num_pre_or_post(context, transition, ARC_OUT, assigned);
    if (weight == .0) return .2;
    else return 2.0 * weight;
}

static int*
noack1(pnml_context_t* context)
{
    Warning(info, "Computing Noack1 order");

    rt_timer_t timer = RTcreateTimer();
    RTstartTimer(timer);

    arc_t* arcs = context->arcs;

    const transition_t* transs = context->transitions;

    int num_assigned = 0;
    int assigned[NUM_PLACES];
    memset(assigned, 0, sizeof(int[NUM_PLACES]));

    int* order = RTmalloc(sizeof(int[NUM_PLACES]));

    while (num_assigned < NUM_PLACES) {
        double weights[NUM_PLACES];
        memset(weights, 0, sizeof(double[NUM_PLACES]));

        int num_arcs[NUM_PLACES];
        memset(num_arcs, 0, sizeof(int[NUM_PLACES]));
        for (arc_t* arc = arcs; arc->type != ARC_LAST; arc++) {
            const int place = arc->place;
            if (assigned[place]) continue;

            const int trans = arc->transition;
            const arc_dir_t type = arc->type;

            num_arcs[place]++;

            if (transs[trans].in_arcs == 0 || transs[trans].out_arcs == 0) continue;

            if (type == ARC_OUT) {
                weights[place] +=
                    (noack_g1(context, trans, assigned) / transs[trans].in_arcs) +
                    ((2.0 * noack_num_pre_or_post(context, trans, ARC_OUT, assigned)) / transs[trans].out_arcs);
            } else { // type == ARC_IN
                weights[place] +=
                    (noack_h(context, trans, assigned) / transs[trans].out_arcs) +
                    ((noack_num_pre_or_post(context, trans, ARC_IN, assigned) + 1.0) / transs[trans].in_arcs);
            }
        }

        int max_place = -1;
        double max_weight = -1.0;
        for (int i = 0; i < NUM_PLACES; i++) {
            if (!assigned[i]) {
                const double weight = weights[i] / num_arcs[i];
                if (weight > max_weight) {
                    max_weight = weight;
                    max_place = i;
                }
            }
        }

        assigned[max_place] = 1;

        order[NUM_PLACES - ++num_assigned] = max_place;
    }

    RTstopTimer(timer);
    RTprintTimer(info, timer, "Computing Noack1 order took");
    RTdeleteTimer(timer);

    return order;
}

static inline double
noack_g2(pnml_context_t* context, int transition, int* assigned)
{
    const double weight = noack_num_pre_or_post(context, transition, ARC_OUT, assigned);
    if (weight == .0) return .1;
    else return 2.0 * weight;
}

static int*
noack2(pnml_context_t* context)
{
    Warning(info, "Computing Noack2 order");

    rt_timer_t timer = RTcreateTimer();
    RTstartTimer(timer);

    arc_t* arcs = context->arcs;

    const transition_t* transs = context->transitions;

    int num_assigned = 0;
    int assigned[NUM_PLACES];
    memset(assigned, 0, sizeof(int[NUM_PLACES]));

    int* order = RTmalloc(sizeof(int[NUM_PLACES]));

    while (num_assigned < NUM_PLACES) {
        double weights[NUM_PLACES];
        memset(weights, 0, sizeof(double[NUM_PLACES]));

        int num_arcs[NUM_PLACES];
        memset(num_arcs, 0, sizeof(int[NUM_PLACES]));
        for (arc_t* arc = arcs; arc->type != ARC_LAST; arc++) {
            const int place = arc->place;
            if (assigned[place]) continue;

            const int trans = arc->transition;
            const arc_dir_t type = arc->type;

            num_arcs[place]++;

            if (type == ARC_OUT) {
                if (transs[trans].in_arcs > 0) {
                    weights[place] += noack_g1(context, trans, assigned) / transs[trans].in_arcs;
                }
                if (transs[trans].out_arcs > 0) {
                    weights[place] += noack_g2(context, trans, assigned) / transs[trans].out_arcs;
                }
            } else if (type == ARC_IN && transs[trans].in_arcs > 0 && transs[trans].out_arcs > 0) {
                weights[place] +=
                    (noack_h(context, trans, assigned) / transs[trans].out_arcs) +
                    ((noack_num_pre_or_post(context, trans, ARC_IN, assigned) + 1.0) / transs[trans].in_arcs);
            }
        }

        int max_place = -1;
        double max_weight = -1.0;
        for (int i = 0; i < NUM_PLACES; i++) {
            if (!assigned[i]) {
                const double weight = weights[i] / num_arcs[i];
                if (weight > max_weight) {
                    max_weight = weight;
                    max_place = i;
                }
            }
        }

        assigned[max_place] = 1;

        order[NUM_PLACES - ++num_assigned] = max_place;
    }

    RTstopTimer(timer);
    RTprintTimer(info, timer, "Computing Noack2 order took");
    RTdeleteTimer(timer);

    return order;
}

void
PNMLloadGreyboxModel(model_t model, const char* name)
{
    rt_timer_t t = RTcreateTimer();
    RTstartTimer(t);

    pnml_context_t* context = RTmallocZero(sizeof(pnml_context_t));
    GBsetContext(model, context);

    xmlDoc* doc = NULL;

    LIBXML_TEST_VERSION

    if ((doc = xmlReadFile(name, NULL, 0)) == NULL) Abort("Could not open file: %s", name);

    context->pnml_places = SIcreate();
    context->pnml_transs = SIcreate();
    context->pnml_arcs = SIcreate();

    Warning(infoLong, "Determining Petri net size");
    xmlNode* node = xmlDocGetRootElement(doc);
    find_ids(node, context);
    Warning(info, "Petri net has %d places, %d transitions and %d arcs",
        NUM_PLACES, NUM_TRANSS, NUM_ARCS);

    bitvector_create(&(context->safe_places), NUM_PLACES);
    bitvector_clear(&(context->safe_places));

    Warning(infoLong, "Analyzing safe places");
    if (context->toolspecific != NULL) parse_toolspecific(context->toolspecific, context);
    Warning(info, "There are %d safe places", context->num_safe_places);

    lts_type_t ltstype;
    matrix_t* dm_info = RTmalloc(sizeof(matrix_t));
    matrix_t* dm_read_info = RTmalloc(sizeof(matrix_t));
    matrix_t* dm_must_write_info = RTmalloc(sizeof(matrix_t));
    matrix_t* dm_update = RTmalloc(sizeof(matrix_t));

    Warning(infoLong, "Creating LTS type");

    // get ltstypes
    ltstype = lts_type_create();

    // adding types
    int int_type = lts_type_add_type(ltstype, LTSMIN_TYPE_NUMERIC, NULL);
    int act_type = lts_type_add_type(ltstype, "action", NULL);

    lts_type_set_format(ltstype, int_type, LTStypeDirect);
    lts_type_set_format(ltstype, act_type, LTStypeEnum);

    lts_type_set_state_length(ltstype, NUM_PLACES);

    // edge label types
    lts_type_set_edge_label_count(ltstype, 1);
    lts_type_set_edge_label_name(ltstype, 0, "action");
    lts_type_set_edge_label_type(ltstype, 0, "action");
    lts_type_set_edge_label_typeno(ltstype, 0, act_type);

    const int guard_type = lts_type_add_type(ltstype, LTSMIN_TYPE_GUARD, NULL);
    lts_type_set_format(ltstype, guard_type, LTStypeEnum);
    
    const int bool_type = lts_type_add_type(ltstype, LTSMIN_TYPE_BOOL, NULL);
    lts_type_set_format(ltstype, bool_type, LTStypeEnum);

    for (int i = 0; i < NUM_PLACES; ++i) {
        lts_type_set_state_typeno(ltstype, i, int_type);
        lts_type_set_state_name(ltstype, i, "tmp");
    }

    GBsetLTStype(model, ltstype); // must set ltstype before setting initial state
                                  // creates tables for types!

    pins_chunk_put_at(model, guard_type, chunk_str(LTSMIN_VALUE_GUARD_FALSE), 0);
    pins_chunk_put_at(model, guard_type, chunk_str(LTSMIN_VALUE_GUARD_TRUE), 1);
    pins_chunk_put_at(model, guard_type, chunk_str(LTSMIN_VALUE_GUARD_MAYBE), 2);
    
    // add bool type for LTSmin language expressions
    pins_chunk_put_at(model, bool_type, chunk_str(LTSMIN_VALUE_BOOL_FALSE), 0);
    pins_chunk_put_at(model, bool_type, chunk_str(LTSMIN_VALUE_BOOL_TRUE), 1);    

    dm_create(dm_info, NUM_TRANSS, NUM_PLACES);
    dm_create(dm_read_info, NUM_TRANSS, NUM_PLACES);
    dm_create(dm_must_write_info, NUM_TRANSS, NUM_PLACES);
    dm_create(dm_update, NUM_TRANSS, NUM_PLACES);

    GBsetDMInfo(model, dm_info);
    GBsetDMInfoRead(model, dm_read_info);
    GBsetDMInfoMustWrite(model, dm_must_write_info);
    GBsetMatrix(model, LTSMIN_MATRIX_ACTIONS_READS, dm_update,
        PINS_MAY_SET, PINS_INDEX_GROUP, PINS_INDEX_STATE_VECTOR);

    context->arcs = RTalignZero(CACHE_LINE_SIZE, sizeof(arc_t[NUM_ARCS + 1]));
    context->arcs[NUM_ARCS].type = ARC_LAST;
    context->arcs[NUM_ARCS].transition = -1;
    context->arcs[NUM_ARCS].place = -1;
    context->transitions = RTmallocZero(sizeof(transition_t[NUM_TRANSS]));

    Warning(infoLong, "Analyzing Petri net behavior");
    node = xmlDocGetRootElement(doc);
    int* init_state = RTmallocZero(sizeof(int[NUM_PLACES]));
    parse_net(node, model, &init_state);
    Warning(info, "Petri net %s analyzed", name);
    GBsetInitialState(model, init_state);
    RTfree(init_state);

    xmlFreeDoc(doc);

    attach_arcs(context);

    switch (noack) {
        case 1:
            GBsetVarPerm(model, noack1(context));
            break;
        case 2:
            GBsetVarPerm(model, noack2(context));
            break;
        default:
            break;
    }

    lts_type_set_state_label_count (ltstype, context->num_guards);
    matrix_t* sl_info = RTmalloc(sizeof(matrix_t));
    dm_create(sl_info, context->num_guards, NUM_PLACES);

    for (int i = 0; i < context->num_guards; i++) {
        const arc_t* arc = context->guards[i];
        const char* g = "guard_%s_ge_%d";
        char label_name[snprintf(NULL, 0, g, SIget(context->pnml_places, arc->place), arc->num) + 1];
        sprintf(label_name, g, SIget(context->pnml_places, arc->place), arc->num);
        lts_type_set_state_label_name (ltstype, i, label_name);
        lts_type_set_state_label_typeno (ltstype, i, guard_type);
        dm_set(sl_info, i, arc->place);
    }
    GBsetStateLabelInfo(model, sl_info);

    // set the label group implementation
    sl_group_t* sl_group_all = RTmalloc(sizeof(sl_group_t) + context->num_guards * sizeof(int));
    sl_group_all->count = context->num_guards;
    for(int i = 0; i < sl_group_all->count; i++) sl_group_all->sl_idx[i] = i;
    GBsetStateLabelGroupInfo(model, GB_SL_ALL, sl_group_all);

    sl_group_t* sl_group_guards = RTmalloc(sizeof(sl_group_t) + context->num_guards * sizeof(int));
    sl_group_guards->count = context->num_guards;
    for(int i = 0; i < sl_group_guards->count; i++) sl_group_guards->sl_idx[i] = i;
    GBsetStateLabelGroupInfo(model, GB_SL_GUARDS, sl_group_guards);

    GBsetGuardsInfo(model, context->guards_info);

    GBsetStateLabelLong(model, get_label_long);
    GBsetStateLabelShort(model, get_label_short);

    GBsetStateLabelsGroup(model, get_labels);

    // get next state
    GBsetNextStateLong(model, (next_method_grey_t) get_successor_long);
    GBsetActionsLong(model, (next_method_grey_t) get_update_long);
    if (context->num_safe_places == 0) {
        GBsetNextStateShort(model, (next_method_grey_t) get_successor_short);
        GBsetActionsShort(model, (next_method_grey_t) get_update_short);
        GBsetNextStateShortR2W(model, (next_method_grey_t) get_successor_short);
        GBsetActionsShortR2W(model, (next_method_grey_t) get_update_short);
    } else Warning(infoLong, "Since this net has 1-safe places, short next-state functions are not used");

    GBsetExit(model, pnml_exit);

    lts_type_validate(ltstype);

    if (PINS_POR) {
        Warning(infoLong, "Creating Do Not Accord matrix");
        matrix_t* dna_info = RTmalloc(sizeof(matrix_t));
        dm_create(dna_info, NUM_TRANSS, NUM_TRANSS);
        for (int i = 0; i < NUM_TRANSS; i++) {
            for (int j = 0; j < NUM_TRANSS; j++) {
                for (arc_t* arc_i = context->arcs + context->transitions[i].start; arc_i->transition == i; arc_i++) {
                    if (arc_i->type != ARC_IN) continue;
                    for (arc_t* arc_j = context->arcs + context->transitions[j].start; arc_j->transition == j; arc_j++) {
                        if (arc_i->type != ARC_IN) continue;
                        if (arc_i->place == arc_j->place) {
                            dm_set(dna_info, i, j);
                            goto next_dna;
                        }
                    }
                }
                next_dna: ;
            }
        }
        GBsetDoNotAccordInfo(model, dna_info);

        Warning(infoLong, "Creating Guard Necessary Enabling Set matrix");
        matrix_t* gnes_info = RTmalloc(sizeof(matrix_t));
        dm_create(gnes_info, context->num_guards, NUM_TRANSS);
        for (int i = 0; i < context->num_guards; i++) {
            const arc_t* source = context->guards[i];
            for (int j = 0; j < NUM_TRANSS; j++) {
                for (arc_t* target = context->arcs + context->transitions[j].start; target->transition == j; target++) {
                    if (target->type != ARC_OUT) continue;
                    if (target->place == source->place) dm_set(gnes_info, i, j);
                }
            }
        }
        GBsetGuardNESInfo(model, gnes_info);

        Warning(infoLong, "Creating Guard Necessary Disabling Set matrix");
        matrix_t* gnds_info = RTmalloc(sizeof(matrix_t));
        dm_create(gnds_info, context->num_guards, NUM_TRANSS);
        for (int i = 0; i < context->num_guards; i++) {
            const arc_t* source = context->guards[i];
            for (int j = 0; j < NUM_TRANSS; j++) {
                if (source->transition == j) dm_set(gnds_info, i, j);
            }
        }
        GBsetGuardNDSInfo(model, gnds_info);

        Warning(infoLong, "Creating Do Not Left Accord (DNB) matrix");
        matrix_t* ndb_info = RTmalloc(sizeof(matrix_t));
        dm_create(ndb_info, NUM_TRANSS, NUM_TRANSS);
        for (int i = 0; i < NUM_TRANSS; i++) {
            for (int j = 0; j < NUM_TRANSS; j++) {
                for (arc_t* source = context->arcs + context->transitions[i].start; source->transition == i; source++) {
                    if (source->type != ARC_IN) continue;
                    for (arc_t* target = context->arcs + context->transitions[j].start; target->transition == j; target++) {
                        if (target->type != ARC_OUT) continue;
                        if (source->place == target->place) {
                            dm_set(ndb_info, i, j);
                            goto next_ndb;
                        }
                    }
                }
                next_ndb: ;
            }
        }
        GBsetMatrix(model, LTSMIN_NOT_LEFT_ACCORDS, ndb_info, PINS_STRICT, PINS_INDEX_OTHER, PINS_INDEX_OTHER);
    } else Warning(infoLong, "Not creating POR matrices");

    RTstopTimer(t);
    RTprintTimer(infoShort, t, "Loading Petri net took");
    RTdeleteTimer(t);
}
