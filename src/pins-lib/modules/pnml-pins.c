#include <config.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <andl-lib/andl-lexer.h>
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

#define NUM_TRANSS pn_context->num_transitions
#define NUM_PLACES SIgetCount(pn_context->place_names)

static int noack = 0;

typedef enum { ID, NAME } edge_label_t;
static edge_label_t edge_label = ID;
static char *edge_label_option = "id";
static si_map_entry EDGE_LABEL[] = {
    {"id", ID},
    {"name", NAME},
    {NULL, 0}
};

static void
pnml_popt(poptContext con,
         enum poptCallbackReason reason,
         const struct poptOption *opt,
         const char *arg, void *data)
{
    (void) con; (void) opt; (void) arg; (void) data;
    switch(reason){
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        GBregisterLoader("pnml", PNMLloadGreyboxModel);
        GBregisterLoader("andl", ANDLloadGreyboxModel);

        if (noack != 0 && noack != 1 && noack != 2) {
            Abort("Option --noack only accepts value 1 or 2");
        }

        const int res = linear_search(EDGE_LABEL, edge_label_option);
        if (res < 0) {
            Warning(lerror, "unknown edge label %s", edge_label_option);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        }
        edge_label = res;

        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort("unexpected call to pnml_popt");
}

struct poptOption pnml_options[]= {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&pnml_popt, 0 , NULL , NULL },
    { "noack" , 0 , POPT_ARG_INT, &noack , 0 , "Set Noack order to apply","<1|2>" },
    { "edge-label", 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT,
        &edge_label_option, 0,
        "Select what to use for edge labels (only affects PNML, not ANDL)",
        "<id|name>" },
    POPT_TABLEEND
};

static int max_token_count = 0;

static void
pn_exit(model_t model)
{
    (void) model;
    Print1 (info, "max token count: %u", max_token_count);
}

static void
update_max (int max)
{
    do {
        if (max <= atomic_read(&max_token_count)) break;
    } while (!cas(&max_token_count, atomic_read(&max_token_count), max));
}

static int
get_successor_long(void *model, int t, int *in, TransitionCB cb, void *arg)
{
    pn_context_t *pn_context = GBgetContext(model);

    int out[NUM_PLACES];
    memcpy (out, in, sizeof(int[NUM_PLACES]));

    int overflown = 0;
    int max = 0;
    for (arc_t *arc = pn_context->arcs + pn_context->transitions[t].start; arc->transition == t; arc++) {
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
                if (arc->safe) out[arc->place] = arc->num;
                else {
                    // detect overflow and report later (only if transition is enabled)
                    overflown |= out[arc->place] > INT_MAX - arc->num;

                    // add tokens
                    out[arc->place] += arc->num;

                    // record max token count
                    if (out[arc->place] > max) max = out[arc->place];
                }
            }
            break;
            case ARC_LAST: break;
        }
    }

    if (overflown) Abort ("max token count exceeded");
    update_max (max);

    int lbl[1] = { pn_context->transitions[t].label };
    transition_info_t transition_info = { lbl, t, 0 };
    cb (arg, &transition_info, out, NULL);

    return 1;
}

static int
get_successor_short(void *model, int t, int *in, TransitionCB cb, void *arg)
{
    pn_context_t *pn_context = GBgetContext(model);

    HREassert(!pn_context->has_safe_places, "get_successor_short not compatible with safe places");

    const int num_writes = dm_ones_in_row(GBgetDMInfoMustWrite(model), t);

    int out[num_writes];
    memcpy (out, in, sizeof(int[num_writes]));
    int *place = out;

    int overflown = 0;
    int max = 0;
    for (arc_t *arc = pn_context->arcs + pn_context->transitions[t].start; arc->transition == t; arc++) {
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

    if (overflown) Abort ("max token count exceeded");
    update_max (max);

    int lbl[1] = { pn_context->transitions[t].label };
    transition_info_t transition_info = { lbl, t, 0 };
    cb (arg, &transition_info, out, NULL);

    return 1;
}

static int
get_update_long(void *model, int t, int *in, TransitionCB cb, void *arg)
{
    pn_context_t *pn_context = GBgetContext(model);

    int out[NUM_PLACES];
    memcpy (out, in, sizeof(int[NUM_PLACES]));

    int overflown = 0;
    int max = 0;
    for (arc_t *arc = pn_context->arcs + pn_context->transitions[t].start; arc->transition == t; arc++) {
        switch(arc->type) {
            case ARC_IN: {
                /*If there is an underflow then this transition is disabled,
                 *while it should not be, since this is the update function. */
                HREassert(out[arc->place] - arc->num >= 0, "transition should not have been disabled");

                // remove tokens
                out[arc->place] -= arc->num;
            }
            break;
            case ARC_OUT: {
                // establish postcondition
                if (arc->safe) out[arc->place] = arc->num;
                else {
                    // detect overflow and report later (only if transition is enabled)
                    overflown |= out[arc->place] > INT_MAX - arc->num;

                    // add tokens
                    out[arc->place] += arc->num;

                    // record max token count
                    if (out[arc->place] > max) max = out[arc->place];
                }
            }
            break;
            case ARC_LAST: break;
        }
    }

    if (overflown) Abort ("max token count exceeded");
    update_max (max);

    int lbl[1] = { pn_context->transitions[t].label };
    transition_info_t transition_info = { lbl, t, 0 };
    cb (arg, &transition_info, out, NULL);

    return 1;
}

static int
get_update_short(void *model, int t, int *in, void
(*callback)(void *arg, transition_info_t *transition_info, int *out, int *cpy), void *arg)
{
    pn_context_t *pn_context = GBgetContext(model);

    HREassert(!pn_context->has_safe_places, "get_update_short not compatible with safe places");

    const int num_writes = dm_ones_in_row(GBgetDMInfoMustWrite(model), t);

    int out[num_writes];
    memcpy (out, in, sizeof(int[num_writes]));
    int *place = out;

    int overflown = 0;
    int max = 0;
    for (arc_t *arc = pn_context->arcs + pn_context->transitions[t].start; arc->transition == t; arc++) {
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

    if (overflown) Abort ("max token count exceeded");
    update_max (max);

    int lbl[1] = { pn_context->transitions[t].label };
    transition_info_t transition_info = { lbl, t, 0 };
    callback (arg, &transition_info, out, NULL);

    return 1;
}

static int
get_label_long(model_t model, int label, int *src) {
    pn_context_t *pn_context = GBgetContext(model);
    HREassert(label < pn_context->num_guards, "unknown state label");
    const arc_t *arc = pn_context->guards[label];
    return src[arc->place] >= arc->num;
}

static int
get_label_short(model_t model, int label, int *src) {
    pn_context_t *pn_context = GBgetContext(model);
    HREassert(label < pn_context->num_guards, "unknown state label");
    return src[0] >= pn_context->guards[label]->num;
}

static void
get_labels(model_t model, sl_group_enum_t group, int *src, int *label) {
    pn_context_t *pn_context = GBgetContext(model);
    if (group == GB_SL_GUARDS || group == GB_SL_ALL) {
        for (int i = 0; i < pn_context->num_guards; i++) {
            const arc_t *arc = pn_context->guards[i];
            label[i] = src[arc->place] >= arc->num;
        }
    }
}

static int
groups_of_edge(model_t model, int edgeno, int index, int *groups)
{
    pn_context_t *pn_context = GBgetContext(model);

    const chunk c = pins_chunk_get(model, lts_type_get_edge_label_typeno(GBgetLTStype(model), edgeno), index);

    const int label = SIlookup(pn_context->edge_labels, c.data);

    if (label == SI_INDEX_FAILED) return 0;

    const int num_labels = SIgetCount(pn_context->edge_labels);
    int len;
    const int begin = pn_context->groups_of_edges_begin[index];
    if (index + 1 == num_labels) len = NUM_TRANSS - begin;
    else len = pn_context->groups_of_edges_begin[index + 1] - begin;

    if (groups != NULL) {
        memcpy(groups, pn_context->groups_of_edges + begin, sizeof(int[len]));
    }

    return len;
}

static void
find_ids(xmlNode *a_node, pn_context_t *pn_context, string_index_t trans_names, string_index_t arc_names, xmlNode **toolspecific)
{
    for (xmlNode *node = a_node; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(node->name, (const xmlChar*) "place") == 0) {
                const xmlChar *id = xmlGetProp(node, (const xmlChar*) "id");
                if (SIput(pn_context->place_names, (char*) id) == SI_INDEX_FAILED) Abort("duplicate place");
            } else if (xmlStrcmp(node->name, (const xmlChar*) "transition") == 0) {
                const xmlChar *id = xmlGetProp(node, (const xmlChar*) "id");
                if (SIput(trans_names, (char*) id) == SI_INDEX_FAILED) Abort("duplicate transition");
            } else if (xmlStrcmp(node->name, (const xmlChar*) "arc") == 0) {
                const xmlChar *id = xmlGetProp(node, (const xmlChar*) "id");
                if (SIput(arc_names, (char*) id) == SI_INDEX_FAILED) Abort("duplicate arc");
            } else if (*toolspecific == NULL && xmlStrcmp(node->name, (const xmlChar*) "toolspecific") == 0){
                *toolspecific = node;
            } else if (xmlStrcmp(node->name, (const xmlChar*) "net") == 0) {
                const xmlChar *type = xmlGetProp(node, (const xmlChar*) "type");
                if (!(
                    xmlStrcmp(type, (const xmlChar*) "http://www.pnml.org/version-2009/grammar/pnmlcoremodel") == 0 ||
                    xmlStrcmp(type, (const xmlChar*) "http://www.pnml.org/version-2009/grammar/ptnet") == 0)) {
                    Abort("pnml type \"%s\" is not supported", type);
                }
            } else if (xmlStrcmp(node->name, (const xmlChar*) "finalmarkings") == 0) {
                Warning(info, "WARNING: ignoring \"finalmarkings\"");
                continue;
            }
        }
        find_ids(node->children, pn_context, trans_names, arc_names, toolspecific);
    }
}

static void
parse_toolspecific(xmlNode *a_node, pn_context_t *pn_context, int safe, bitvector_t *safe_places)
{
    for (xmlNode *node = a_node; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(node->name, (const xmlChar*) "structure") == 0) {
                xmlChar *val = xmlStrdup(xmlGetProp(node, (const xmlChar*) "safe"));
                xmlChar *lower = val;
                for ( ; *val; ++val) *val = tolower(*val);
                safe = xmlStrcmp(lower, (const xmlChar*) "true") == 0;
            } else if (xmlStrcmp(node->name, (const xmlChar*) "places") == 0 && safe) {
                xmlChar *places = xmlStrdup(xmlNodeGetContent(node));
                // weird format; change all newlines to spaces
                for (size_t i = 0; i < strlen((char*) places); i++) {
                    if (places[i] == '\n') places[i] = ' ';
                }
                char *place;
                while ((place = strsep((char**) &places, " ")) != NULL) {
                    if (strlen(place) == 0) continue; // weird again
                    int num;
                    if ((num = SIlookup(pn_context->place_names, place)) == SI_INDEX_FAILED) Abort("missing place: %s", place);
                    if (safe) bitvector_set(safe_places, num);
                }
            }
        }
        parse_toolspecific(node->children, pn_context, safe, safe_places);
    }
}

static void
parse_net(xmlNode *a_node, pn_context_t *pn_context, string_index_t trans_names, string_index_t arc_names, bitvector_t *safe_places)
{
    for (xmlNode *node = a_node; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(node->name, (const xmlChar*) "text") == 0) {
                const xmlChar *id = xmlGetProp(node->parent->parent, (const xmlChar*) "id");
                if (xmlStrcmp(node->parent->name, (const xmlChar*) "name") == 0) {
                    if (xmlStrcmp(node->parent->parent->name, (const xmlChar*) "net") == 0) pn_context->name = (char*) xmlStrdup(xmlNodeGetContent(node));
                    else {
                        if (edge_label == NAME) {
                            if (xmlStrcmp(node->parent->parent->name, (const xmlChar*) "transition") == 0) {
                                int num;
                                if ((num = SIlookup(trans_names, (char*) id)) == SI_INDEX_FAILED) Abort("missing transition");
                                pn_context->transitions[num].label = SIput(pn_context->edge_labels, (char*) xmlNodeGetContent(node));
                            }
                        }
                    }
                } else if (xmlStrcmp(node->parent->name, (const xmlChar*) "initialMarking") == 0) {
                    int num;
                    if ((num = SIlookup(pn_context->place_names, (char*) id)) == SI_INDEX_FAILED) Abort("missing place");
                    const int val = atoi((char*) xmlNodeGetContent(node));
                    
                    // test if the value fits in an int
                    char buf[strlen((char*) xmlNodeGetContent(node)) + 1];
                    sprintf(buf, "%d", val);
                    if (strcmp(buf, (char*) xmlNodeGetContent(node)) != 0) {
                        Abort("Make sure the initial marking \"%s\" fits in a signed 32-bit integer", xmlNodeGetContent(node));
                    }
                        
                    pn_context->init_state[num] = val;
                    if (val > max_token_count) max_token_count = val;
                } else if (xmlStrcmp(node->parent->name, (const xmlChar*) "inscription") == 0) {
                    int num;
                    if ((num = SIlookup(arc_names, (char*) id)) == SI_INDEX_FAILED) Abort("missing arc");
                    pn_context->arcs[num].num = atoi((char*) xmlNodeGetContent(node));
                    
                    // test if the value fits in an int
                    char buf[strlen((char*) xmlNodeGetContent(node)) + 1];
                    sprintf(buf, "%d", pn_context->arcs[num].num);
                    if (strcmp(buf, (char*) xmlNodeGetContent(node)) != 0) {
                        Abort("Make sure the inscription \"%s\" fits in a signed 32-bit integer", xmlNodeGetContent(node));
                    }
                }
            } else if (xmlStrcmp(node->name, (const xmlChar*) "arc") == 0) {
                int num;
                const xmlChar *id = xmlGetProp(node, (const xmlChar*) "id");
                if ((num = SIlookup(arc_names, (char*) id)) == SI_INDEX_FAILED) Abort("missing arc");
                pn_context->arcs[num].num = 1;

                const xmlChar *source = xmlGetProp(node, (const xmlChar*) "source");
                const xmlChar *target = xmlGetProp(node, (const xmlChar*) "target");
                if (source == NULL || target == NULL) Abort("invalid arc at line: %d", node->line)
                int source_num;
                int target_num;
                if ((source_num = SIlookup(pn_context->place_names, (char*) source)) != SI_INDEX_FAILED &&
                    (target_num = SIlookup(trans_names, (char*) target)) != SI_INDEX_FAILED) {
                    // this is an in arc
                    pn_context->arcs[num].transition = target_num;
                    pn_context->arcs[num].place = source_num;
                    pn_context->arcs[num].type = ARC_IN;
                    pn_context->transitions[target_num].in_arcs++;
                    pn_context->num_in_arcs++;
                } else if ((source_num = SIlookup(trans_names, (char*) source)) != SI_INDEX_FAILED &&
                    (target_num = SIlookup(pn_context->place_names, (char*) target)) != SI_INDEX_FAILED) {
                    // this is an out arc
                    pn_context->arcs[num].transition = source_num;
                    pn_context->arcs[num].place = target_num;
                    pn_context->arcs[num].type = ARC_OUT;
                    pn_context->transitions[source_num].out_arcs++;
                } else Abort("incorrect net");
                pn_context->arcs[num].safe = bitvector_is_set(safe_places, pn_context->arcs[num].place);
            }
        }
        parse_net (node->children, pn_context, trans_names, arc_names, safe_places);
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
    const arc_t *aa = (const arc_t*) a;
    const arc_t *ab = (const arc_t*) b;

    if (aa->transition < ab->transition) return -1;
    if (aa->transition > ab->transition) return 1;
    if (aa->place < ab->place) return -1;
    if (aa->place > ab->place) return 1;

    if (aa->type != ab->type) return aa->type == ARC_IN ? -1 : 1;
    return 0;
}

static void
attach_arcs(pn_context_t *pn_context)
{
    if (NUM_TRANSS > 0) {
        qsort(pn_context->arcs, pn_context->num_arcs, sizeof(arc_t), compare_arcs);

        pn_context->guards_info = RTmalloc(NUM_TRANSS * sizeof(guard_t*));
        guard_t *guards;
        guards = RTmalloc(sizeof(int[NUM_TRANSS]) + sizeof(int[pn_context->num_in_arcs]));
        pn_context->guards = RTmalloc(sizeof(arc_t *) * pn_context->num_in_arcs);

        string_index_t guard_unique = SIcreate();
        for (arc_t *arc = pn_context->arcs; arc->type != ARC_LAST; arc++) {
            const int num = arc - pn_context->arcs;
            if (num == 0 || arc->transition != (arc - 1)->transition) {
                pn_context->transitions[arc->transition].start = num;

                guards->count = 0;
                pn_context->guards_info[arc->transition] = guards;
                guards += 1 + pn_context->transitions[arc->transition].in_arcs;
            }
            if (arc->type == ARC_IN) {
                const char *u = "%d%d";
                char unique[snprintf(NULL, 0, u, arc->place, arc->num) + 1];
                sprintf(unique, u, arc->place, arc->num);
                int duplicate;
                if ((duplicate = SIput(guard_unique, unique)) != SI_INDEX_FAILED) pn_context->guards[duplicate] = arc;
                pn_context->guards_info[arc->transition]->guard[pn_context->guards_info[arc->transition]->count++] = duplicate;
            }
        }
        pn_context->num_guards = SIgetCount(guard_unique);
        SIdestroy(&guard_unique);
        pn_context->guards = RTrealloc(pn_context->guards, sizeof(arc_t *) * pn_context->num_guards);
    }
}

/*
 * if type == ARC_OUT, compute |.t \cap S|
 * if type == ARC_IN, compute |t. \cap S|
 */
static inline double
noack_num_pre_or_post(pn_context_t *pn_context, int transition, arc_dir_t type, int *assigned)
{
    const transition_t *transs = pn_context->transitions;
    double result = .0;

    for (arc_t *arc = pn_context->arcs + transs[transition].start; arc->transition == transition; arc++) {
        if (arc->type == type && assigned[arc->place]) result += 1.0;
    }

    return result;
}

static inline double
noack_g1(pn_context_t *pn_context, int transition, int *assigned)
{
    const double weight = noack_num_pre_or_post(pn_context, transition, ARC_IN, assigned);
    if (weight == .0) return .1;
    else return weight;
}

static inline double
noack_h(pn_context_t *pn_context, int transition, int *assigned)
{
    const double weight = noack_num_pre_or_post(pn_context, transition, ARC_OUT, assigned);
    if (weight == .0) return .2;
    else return 2.0 * weight;
}

static int*
noack1(pn_context_t *pn_context)
{
    Print1 (info, "Computing Noack1 order");

    rt_timer_t timer = RTcreateTimer();
    RTstartTimer(timer);

    arc_t *arcs = pn_context->arcs;

    const transition_t *transs = pn_context->transitions;

    int num_assigned = 0;
    int assigned[NUM_PLACES];
    memset(assigned, 0, sizeof(int[NUM_PLACES]));

    int *order = RTmalloc(sizeof(int[NUM_PLACES]));

    while (num_assigned < NUM_PLACES) {
        double weights[NUM_PLACES];
        memset(weights, 0, sizeof(double[NUM_PLACES]));

        int num_arcs[NUM_PLACES];
        memset(num_arcs, 0, sizeof(int[NUM_PLACES]));
        for (arc_t *arc = arcs; arc->type != ARC_LAST; arc++) {
            const int place = arc->place;
            if (assigned[place]) continue;

            const int trans = arc->transition;
            const arc_dir_t type = arc->type;

            num_arcs[place]++;

            if (transs[trans].in_arcs == 0 || transs[trans].out_arcs == 0) continue;

            if (type == ARC_OUT) {
                weights[place] +=
                    (noack_g1(pn_context, trans, assigned) / transs[trans].in_arcs) +
                    ((2.0 * noack_num_pre_or_post(pn_context, trans, ARC_OUT, assigned)) / transs[trans].out_arcs);
            } else { // type == ARC_IN
                weights[place] +=
                    (noack_h(pn_context, trans, assigned) / transs[trans].out_arcs) +
                    ((noack_num_pre_or_post(pn_context, trans, ARC_IN, assigned) + 1.0) / transs[trans].in_arcs);
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
    if (HREme(HREglobal()) == 0 ) {
        RTprintTimer (infoLong, timer, "Computing Noack1 order took");
    }
    RTdeleteTimer(timer);

    return order;
}

static inline double
noack_g2(pn_context_t *pn_context, int transition, int *assigned)
{
    const double weight = noack_num_pre_or_post(pn_context, transition, ARC_OUT, assigned);
    if (weight == .0) return .1;
    else return 2.0 * weight;
}

static int*
noack2(pn_context_t *pn_context)
{
    Print1 (info, "Computing Noack2 order");

    rt_timer_t timer = RTcreateTimer();
    RTstartTimer (timer);

    arc_t *arcs = pn_context->arcs;

    const transition_t *transs = pn_context->transitions;

    int num_assigned = 0;
    int assigned[NUM_PLACES];
    memset (assigned, 0, sizeof(int[NUM_PLACES]));

    int *order = RTmalloc(sizeof(int[NUM_PLACES]));

    while (num_assigned < NUM_PLACES) {
        double weights[NUM_PLACES];
        memset(weights, 0, sizeof(double[NUM_PLACES]));

        int num_arcs[NUM_PLACES];
        memset(num_arcs, 0, sizeof(int[NUM_PLACES]));
        for (arc_t *arc = arcs; arc->type != ARC_LAST; arc++) {
            const int place = arc->place;
            if (assigned[place]) continue;

            const int trans = arc->transition;
            const arc_dir_t type = arc->type;

            num_arcs[place]++;

            if (type == ARC_OUT) {
                if (transs[trans].in_arcs > 0) {
                    weights[place] += noack_g1(pn_context, trans, assigned) / transs[trans].in_arcs;
                }
                if (transs[trans].out_arcs > 0) {
                    weights[place] += noack_g2(pn_context, trans, assigned) / transs[trans].out_arcs;
                }
            } else if (type == ARC_IN && transs[trans].in_arcs > 0 && transs[trans].out_arcs > 0) {
                weights[place] +=
                    (noack_h(pn_context, trans, assigned) / transs[trans].out_arcs) +
                    ((noack_num_pre_or_post(pn_context, trans, ARC_IN, assigned) + 1.0) / transs[trans].in_arcs);
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

    RTstopTimer (timer);
    if (HREme(HREglobal()) == 0 ) {
        RTprintTimer (infoLong, timer, "Computing Noack2 order took");
    }
    RTdeleteTimer (timer);

    return order;
}

static void
initGreyboxModel(model_t model)
{
    pn_context_t *pn_context = RTmallocZero(sizeof(pn_context_t));
    GBsetContext (model, pn_context);

    pn_context->timer = RTcreateTimer();
    RTstartTimer(pn_context->timer);

    pn_context->place_names = SIcreate();
    pn_context->edge_labels = SIcreate();

    pn_context->has_safe_places = 0;

    Print1 (infoLong, "Determining Petri net size");
}

static void
create_lts_type(model_t model)
{
    pn_context_t *pn_context = GBgetContext(model);

    Print1 (info, "Petri net has %d places, %d transitions and %d arcs",
        NUM_PLACES, NUM_TRANSS, pn_context->num_arcs);

    lts_type_t ltstype;

    Print1 (infoLong, "Creating LTS type");

    // get ltstypes
    ltstype = lts_type_create();

    // adding types
    const int int_type = lts_type_add_type(ltstype, "place", NULL);
    const int act_type = lts_type_add_type(ltstype, "action", NULL);

    lts_type_set_format (ltstype, int_type, LTStypeSInt32);
    lts_type_set_format (ltstype, act_type, LTStypeEnum);

    lts_type_set_state_length (ltstype, NUM_PLACES);

    // edge label types
    lts_type_set_edge_label_count (ltstype, 1);
    lts_type_set_edge_label_name (ltstype, 0, "action");
    lts_type_set_edge_label_type (ltstype, 0, "action");
    lts_type_set_edge_label_typeno (ltstype, 0, act_type);

    const int guard_type = lts_type_add_type(ltstype, "guard", NULL);
    lts_type_set_format (ltstype, guard_type, LTStypeBool);

    for (int i = 0; i < NUM_PLACES; i++) {
        lts_type_set_state_typeno(ltstype, i, int_type);
        const char *name = SIget(pn_context->place_names, i);
        lts_type_set_state_name(ltstype, i, name);
    }

    GBsetLTStype (model, ltstype); // must set ltstype before setting initial state
                                  // creates tables for types!

    Print1 (infoLong, "Analyzing Petri net behavior");
}

static int
compare_transition_label(const void *a, const void *b, void *arg)
{
    transition_t *transitions = (transition_t*) arg;
    const int *t1 = (const int*) a;
    const int *t2 = (const int*) b;

    return transitions[*t1].label - transitions[*t2].label;
}

static void
init_groups_of_edge(pn_context_t *pn_context)
{
    pn_context->groups_of_edges = RTmalloc(sizeof(int[NUM_TRANSS]));
    for (int i = 0; i < NUM_TRANSS; i++) pn_context->groups_of_edges[i] = i;
    qsort_r(pn_context->groups_of_edges, NUM_TRANSS, sizeof(int),
            compare_transition_label, pn_context->transitions);

    const int num_labels = SIgetCount(pn_context->edge_labels);
    pn_context->groups_of_edges_begin = RTmalloc(sizeof(int[num_labels]));

    int current_label = -1;
    for (int i = 0; i < NUM_TRANSS; i++) {
        const int t = pn_context->groups_of_edges[i];
        const transition_t transition = pn_context->transitions[t];
        if (current_label != transition.label) {
            current_label = transition.label;
            pn_context->groups_of_edges_begin[current_label] = i;
        }
    }
}

static void
set_dependencies(model_t model)
{
    pn_context_t *pn_context = GBgetContext(model);

    lts_type_t ltstype = GBgetLTStype(model);

    Print1 (info, "Petri net %s analyzed", pn_context->name);

    const int act_type = lts_type_find_type(ltstype, "action");
    for (int i = 0; i < SIgetCount(pn_context->edge_labels); i++) {
        pins_chunk_put_at(model, act_type, chunk_str(SIget(pn_context->edge_labels, i)), i);
    }

    GBsetInitialState (model, pn_context->init_state);
    RTfree (pn_context->init_state);

    attach_arcs (pn_context);
    
    matrix_t *dm_info = RTmalloc(sizeof(matrix_t));
    matrix_t *dm_read_info = RTmalloc(sizeof(matrix_t));
    matrix_t *dm_must_write_info = RTmalloc(sizeof(matrix_t));
    matrix_t *dm_update = RTmalloc(sizeof(matrix_t));

    dm_create (dm_info, NUM_TRANSS, NUM_PLACES);
    dm_create (dm_read_info, NUM_TRANSS, NUM_PLACES);
    dm_create (dm_must_write_info, NUM_TRANSS, NUM_PLACES);
    dm_create (dm_update, NUM_TRANSS, NUM_PLACES);

    GBsetDMInfo (model, dm_info);
    GBsetDMInfoRead (model, dm_read_info);
    GBsetDMInfoMustWrite (model, dm_must_write_info);
    GBsetMatrix (model, LTSMIN_MATRIX_ACTIONS_READS, dm_update,
        PINS_MAY_SET, PINS_INDEX_GROUP, PINS_INDEX_STATE_VECTOR);

    for (arc_t *arc = pn_context->arcs; arc->type != ARC_LAST; arc++) {
        if (arc->type == ARC_IN) {
            dm_set(dm_info, arc->transition, arc->place);
            dm_set(dm_read_info, arc->transition, arc->place);
            dm_set(dm_update, arc->transition, arc->place);
            dm_set(dm_must_write_info, arc->transition, arc->place);
        } else { // arc->type == ARC_OUT
            dm_set(dm_info, arc->transition, arc->place);
            dm_set(dm_must_write_info, arc->transition, arc->place);
            if (!arc->safe) {
                dm_set(dm_read_info, arc->transition, arc->place);
                dm_set(dm_update, arc->transition, arc->place);
            } else pn_context->has_safe_places = 1;
        }
    }
    Print1 (info, "There are %ssafe places", pn_context->has_safe_places ? "" : "no ");

    switch (noack) {
        case 1:
            GBsetVarPerm (model, noack1(pn_context));
            break;
        case 2:
            GBsetVarPerm (model, noack2(pn_context));
            break;
        default:
            break;
    }

    lts_type_set_state_label_count (ltstype, pn_context->num_guards);
    matrix_t *sl_info = RTmalloc(sizeof(matrix_t));
    dm_create (sl_info, pn_context->num_guards, NUM_PLACES);

    const int guard_type = lts_type_find_type(ltstype, "guard");
    for (int i = 0; i < pn_context->num_guards; i++) {
        const arc_t *arc = pn_context->guards[i];
        const char *g = "guard_%s_ge_%d";
        char label_name[snprintf(NULL, 0, g, SIget(pn_context->place_names, arc->place), arc->num) + 1];
        sprintf(label_name, g, SIget(pn_context->place_names, arc->place), arc->num);
        lts_type_set_state_label_name (ltstype, i, label_name);
        lts_type_set_state_label_typeno (ltstype, i, guard_type);
        dm_set (sl_info, i, arc->place);
    }
    GBsetStateLabelInfo (model, sl_info);

    // set the label group implementation
    sl_group_t *sl_group_all = RTmalloc(sizeof(sl_group_t) + pn_context->num_guards * sizeof(int));
    sl_group_all->count = pn_context->num_guards;
    for(int i = 0; i < sl_group_all->count; i++) sl_group_all->sl_idx[i] = i;
    GBsetStateLabelGroupInfo (model, GB_SL_ALL, sl_group_all);

    sl_group_t *sl_group_guards = RTmalloc(sizeof(sl_group_t) + pn_context->num_guards * sizeof(int));
    sl_group_guards->count = pn_context->num_guards;
    for(int i = 0; i < sl_group_guards->count; i++) sl_group_guards->sl_idx[i] = i;
    GBsetStateLabelGroupInfo (model, GB_SL_GUARDS, sl_group_guards);

    GBsetGuardsInfo (model, pn_context->guards_info);

    GBsetStateLabelLong (model, get_label_long);
    GBsetStateLabelShort (model, get_label_short);

    GBsetStateLabelsGroup (model, get_labels);

    // get next state
    GBsetNextStateLong (model, (next_method_grey_t) get_successor_long);
    GBsetActionsLong (model, (next_method_grey_t) get_update_long);
    if (!pn_context->has_safe_places) {
        GBsetNextStateShort (model, (next_method_grey_t) get_successor_short);
        GBsetActionsShort (model, (next_method_grey_t) get_update_short);
        GBsetNextStateShortR2W (model, (next_method_grey_t) get_successor_short);
        GBsetActionsShortR2W (model, (next_method_grey_t) get_update_short);
    } else Print1 (infoLong, "Since this net has 1-safe places, short next-state functions are not used");

    init_groups_of_edge(pn_context);

    GBsetGroupsOfEdge (model, groups_of_edge);

    GBsetExit (model, pn_exit);

    lts_type_validate (ltstype);

    if (PINS_POR) {
        Print1 (infoLong, "Creating Do Not Accord matrix");
        matrix_t *dna_info = RTmalloc(sizeof(matrix_t));
        dm_create (dna_info, NUM_TRANSS, NUM_TRANSS);
        for (int i = 0; i < NUM_TRANSS; i++) {
            for (int j = 0; j < NUM_TRANSS; j++) {
                for (arc_t *arc_i = pn_context->arcs + pn_context->transitions[i].start; arc_i->transition == i; arc_i++) {
                    if (arc_i->type != ARC_IN) continue;
                    for (arc_t *arc_j = pn_context->arcs + pn_context->transitions[j].start; arc_j->transition == j; arc_j++) {
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
        GBsetDoNotAccordInfo (model, dna_info);

        Print1 (infoLong, "Creating Guard Necessary Enabling Set matrix");
        matrix_t *gnes_info = RTmalloc(sizeof(matrix_t));
        dm_create (gnes_info, pn_context->num_guards, NUM_TRANSS);
        for (int i = 0; i < pn_context->num_guards; i++) {
            const arc_t *source = pn_context->guards[i];
            for (int j = 0; j < NUM_TRANSS; j++) {
                for (arc_t *target = pn_context->arcs + pn_context->transitions[j].start; target->transition == j; target++) {
                    if (target->type != ARC_OUT) continue;
                    if (target->place == source->place) dm_set(gnes_info, i, j);
                }
            }
        }
        GBsetGuardNESInfo (model, gnes_info);

        Print1 (infoLong, "Creating Guard Necessary Disabling Set matrix");
        matrix_t *gnds_info = RTmalloc(sizeof(matrix_t));
        dm_create (gnds_info, pn_context->num_guards, NUM_TRANSS);
        for (int i = 0; i < pn_context->num_guards; i++) {
            const arc_t *source = pn_context->guards[i];
            for (int j = 0; j < NUM_TRANSS; j++) {
                if (source->transition == j) dm_set(gnds_info, i, j);
            }
        }
        GBsetGuardNDSInfo (model, gnds_info);

        Print1 (infoLong, "Creating Do Not Left Accord (DNB) matrix");
        matrix_t *ndb_info = RTmalloc(sizeof(matrix_t));
        dm_create (ndb_info, NUM_TRANSS, NUM_TRANSS);
        for (int i = 0; i < NUM_TRANSS; i++) {
            for (int j = 0; j < NUM_TRANSS; j++) {
                for (arc_t *source = pn_context->arcs + pn_context->transitions[i].start; source->transition == i; source++) {
                    if (source->type != ARC_IN) continue;
                    for (arc_t *target = pn_context->arcs + pn_context->transitions[j].start; target->transition == j; target++) {
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
        GBsetMatrix (model, LTSMIN_NOT_LEFT_ACCORDS, ndb_info, PINS_STRICT, PINS_INDEX_OTHER, PINS_INDEX_OTHER);
    } else Print1 (infoLong, "Not creating POR matrices");

    RTstopTimer (pn_context->timer);
    if (HREme(HREglobal()) == 0 ) {
        RTprintTimer (infoShort, pn_context->timer, "Loading Petri net took");
    }
    RTdeleteTimer (pn_context->timer);
}

void
ANDLloadGreyboxModel(model_t model, const char *name)
{
    FILE *f = fopen(name, "r");
    yyscan_t scanner;
    andl_lex_init(&scanner);
    andl_set_in(f, scanner);

    initGreyboxModel(model);

    andl_context_t andl_context;
    andl_context.error = 0;
    andl_context.pn_context = GBgetContext(model);
    andl_context.arc_man = create_manager(65535);
    andl_context.trans_man = create_manager(65535);
    andl_context.init_man = create_manager(65535);
    ADD_ARRAY(andl_context.arc_man, andl_context.pn_context->arcs, arc_t);
    ADD_ARRAY(andl_context.trans_man, andl_context.pn_context->transitions, transition_t);
    ADD_ARRAY(andl_context.init_man, andl_context.pn_context->init_state, int);
   
    andl_parse(scanner, &andl_context);
    andl_lex_destroy(scanner);

    fclose(f);
    if (andl_context.error) {
        Abort("Could not parse file %s", name);
    } else {
        create_lts_type(model);
        set_dependencies(model);
    }
}

void
PNMLloadGreyboxModel(model_t model, const char *name)
{
    if (HREme(HREglobal())==0) {
        Warning(info, "Edge label is %s", edge_label_option);
    }

    xmlDoc *doc = NULL;

    LIBXML_TEST_VERSION

    if ((doc = xmlReadFile(name, NULL, 0)) == NULL) Abort("Could not open file: %s", name);

    initGreyboxModel(model);
    pn_context_t *pn_context = GBgetContext(model);

    string_index_t trans_names;
    if (edge_label == NAME) trans_names = SIcreate();
    else /* if edge_label == ID */ trans_names = pn_context->edge_labels;
    string_index_t arc_names = SIcreate();
    xmlNode *toolspecific = NULL;
    
    xmlNode *node = xmlDocGetRootElement(doc);
    find_ids(node, pn_context, trans_names, arc_names, &toolspecific);

    pn_context->num_transitions = SIgetCount(trans_names);
    pn_context->num_arcs = SIgetCount(arc_names);

    pn_context->arcs = RTalignZero(CACHE_LINE_SIZE, sizeof(arc_t[pn_context->num_arcs + 1]));
    pn_context->arcs[pn_context->num_arcs].type = ARC_LAST;
    pn_context->arcs[pn_context->num_arcs].transition = -1;
    pn_context->arcs[pn_context->num_arcs].place = -1;
    pn_context->transitions = RTmallocZero(sizeof(transition_t[NUM_TRANSS]));
    if (edge_label == ID) {
        for (int i = 0; i < NUM_TRANSS; i++) {
            pn_context->transitions[i].label = i;
        }
    }
    pn_context->init_state = RTmallocZero(sizeof(int[NUM_PLACES]));

    create_lts_type(model);

    node = xmlDocGetRootElement(doc);

    bitvector_t safe_places;
    bitvector_create(&safe_places, NUM_PLACES);
    if (toolspecific != NULL) parse_toolspecific(toolspecific, pn_context, 0, &safe_places);
    parse_net(node, pn_context, trans_names, arc_names, &safe_places);
    bitvector_free(&safe_places);

    if (edge_label == NAME) SIdestroy(&trans_names);
    SIdestroy(&arc_names);

    set_dependencies(model);

    xmlFreeDoc (doc);
}
