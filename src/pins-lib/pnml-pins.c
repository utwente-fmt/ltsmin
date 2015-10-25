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
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/atomics.h>
#include <pins-lib/pnml-pins.h>
#include <util-lib/hashmap.h>

static void
pnml_popt(poptContext con,
         enum poptCallbackReason reason,
         const struct poptOption* opt,
         const char* arg, void* data)
{
    (void)con;(void)opt;(void)arg;(void)data;
    switch(reason){
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        GBregisterLoader("pnml", PNMLloadGreyboxModel);
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort("unexpected call to pnml_popt");
}

struct poptOption pnml_options[]= {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&pnml_popt, 0 , NULL , NULL },
    POPT_TABLEEND
};

typedef enum { ARC_IN, ARC_OUT, ARC_LAST } arc_dir_t;

typedef struct arc {
    int transition;
    int place;
    uint32_t num;
    arc_dir_t type;
} arc_t;

typedef struct transition {
    int start;
    int in_arcs;
    int out_arcs;
} transition_t;

typedef struct pnml_context {
    xmlChar* name;
    int num_places;
    int num_transs;
    size_t num_arcs;
    int num_safe_places;
    bitvector_t safe_places;
    transition_t* transitions;
    arc_t* arcs;

    map_t pnml_places;
    map_t pnml_transs;
    map_t pnml_arcs;
    void* blaat;

    xmlNode* toolspecific;
    uint8_t safe;

    size_t num_in_arcs;
    guard_t** guards_info;

} pnml_context_t;

static uint32_t max_token_count = 0;

static void
pnml_exit(model_t model)
{
    (void) model;
    Warning(info, "max token count: %u\n", max_token_count);
}

static int
get_successor_long(void* model, int t, int* in, void
(*callback)(void* arg, transition_info_t* transition_info, int* out, int* cpy), void* arg)
{
    pnml_context_t* context = GBgetContext(model);

    uint32_t out[context->num_places];
    memcpy(out, in, sizeof(int[context->num_places]));

    int overflown = 0;
    uint32_t max = 0;
    for (arc_t* arc = context->arcs + context->transitions[t].start; arc->transition == t; arc++) {
        switch(arc->type) {
            case ARC_IN: {
                // check precondition
                if (out[arc->place] - arc->num > out[arc->place]) return 0; // underflow (token count < 0)
                out[arc->place] -= arc->num;
            }
            break;
            case ARC_OUT: {
                // establish postcondition
                if (!bitvector_is_set(&(context->safe_places), arc->place)) {

                    // detect overflow and report later (only if transition is enabled)
                    overflown |= out[arc->place] > UINT32_MAX - arc->num;

                    out[arc->place] += arc->num;
                    if (out[arc->place] > max) max = out[arc->place];
                } else out[arc->place] = 1;
            }
            break;
            case ARC_LAST: break;
        }
    }

    if (overflown) Abort("max token count exceeded");

    volatile uint32_t* ptr;
    do {
        ptr = &max_token_count;
        if (max <= *ptr) break;
    } while (!cas(ptr, *ptr, max));

    transition_info_t transition_info = { (int[1]) { t }, t, 0 };
    callback(arg, &transition_info, (int*) out, NULL);

    return 1;
}

static int
get_successor_short(void* model, int t, int* in, void
(*callback)(void* arg, transition_info_t* transition_info, int* out, int* cpy), void* arg)
{
    pnml_context_t* context = GBgetContext(model);

    uint32_t out[context->transitions[t].in_arcs + context->transitions[t].out_arcs];

    int writes = 0;
    int reads = 0;
    int overflown = 0;
    uint32_t max = 0;
    for (arc_t* arc = context->arcs + context->transitions[t].start; arc->transition == t; arc++) {
        switch(arc->type) {
            case ARC_IN: {
                // check precondition
                out[writes] = in[reads++];
                if (out[writes] - arc->num > out[writes]) return 0; // underflow (token count < 0)
                out[writes] -= arc->num;
            }
            break;
            case ARC_OUT: {
                // establish postcondition
                if (bitvector_is_set(&(context->safe_places), arc->place)) out[writes] = 1;
                else {
                    out[writes] = in[reads++];

                    // detect overflow and report later (only if transition is enabled)
                    overflown |= out[writes] > UINT32_MAX - arc->num;

                    out[writes] += arc->num;
                    if (out[writes] > max) max = out[writes];
                }
            }
            break;
            case ARC_LAST: break;
        }
        if (arc->place != (arc + 1)->place) writes++;
    }

    if (overflown) Abort("max token count exceeded");

    volatile uint32_t* ptr;
    do {
        ptr = &max_token_count;
        if (max <= *ptr) break;
    } while (!cas(ptr, *ptr, max));

    transition_info_t transition_info = { (int[1]) { t }, t, 0 };
    callback(arg, &transition_info, (int*) out, NULL);

    return 1;
}

static int
get_label_long(model_t model, int label, int*src) {
    pnml_context_t* context = GBgetContext(model);
    if (label >= context->num_places) Abort("unknown state label");
    return src[label] > 0;
}

static int
get_label_short(model_t model, int label, int*src) {
    pnml_context_t* context = GBgetContext(model);
    if (label >= context->num_places) Abort("unknown state label");
    return src[0] > 0;
}

static void
get_labels(model_t model, sl_group_enum_t group, int*src, int *label) {
    pnml_context_t* context = GBgetContext(model);
    if (group == GB_SL_GUARDS || group == GB_SL_ALL) {
        for (int i = 0; i < context->num_places; i++) {
            label[i] = src[i] > 0;
        }
    }
}

static void
find_ids(xmlNode* a_node, pnml_context_t* context)
{
    for (xmlNode* node = a_node; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(node->name, (const xmlChar*) "place") == 0) {
                xmlChar* id = xmlGetProp(node, (const xmlChar*) "id");
                if (hashmap_get(context->pnml_places, (char*) id, &(context->blaat)) == MAP_OK) Abort("duplicate place");
                if (context->num_places == INT_MAX) Abort("too many places");
                if (hashmap_put(context->pnml_places, (char*) id, (void*) (size_t) context->num_places) == MAP_OMEM) Abort("out of memory");
                context->num_places++;
            } else if(xmlStrcmp(node->name, (const xmlChar*) "transition") == 0) {
                xmlChar* id = xmlGetProp(node, (const xmlChar*) "id");
                if (hashmap_get(context->pnml_transs, (char*) id, &(context->blaat)) == MAP_OK) Abort("duplicate transition");
                if (context->num_transs == INT_MAX) Abort("too many transitions");
                if (hashmap_put(context->pnml_transs, (char*) id, (void*) (size_t) context->num_transs) == MAP_OMEM) Abort("out of memory");
                context->num_transs++;
            } else if(xmlStrcmp(node->name, (const xmlChar*) "arc") == 0) {
                xmlChar* id = xmlGetProp(node, (const xmlChar*) "id");
                if (hashmap_get(context->pnml_arcs, (char*) id, &(context->blaat)) == MAP_OK) Abort("duplicate arc");
                if (context->num_arcs == SIZE_MAX) Abort("too many arcs");
                if (hashmap_put(context->pnml_arcs, (char*) id, (void*) (size_t) context->num_arcs) == MAP_OMEM) Abort("out of memory");
                context->num_arcs++;
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
                char* place;
                while ((place = strsep((char**) &places, " ")) != NULL) {
                    size_t num;
                    if (hashmap_get(context->pnml_places, place, (void**) &num) == MAP_MISSING) Abort("missing place");
                    bitvector_set(&(context->safe_places), num);
                    context->num_safe_places++;
                }
            }
        }
        parse_toolspecific(node->children, context);
    }
}

static void
parse_net(xmlNode* a_node, model_t model, uint32_t* init_state[])
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
                        if (hashmap_get(context->pnml_transs, (char*) id, (void**) &num) == MAP_MISSING) Abort("missing transition");
                        GBchunkPutAt(model, lts_type_find_type(GBgetLTStype(model), "action"), chunk_str((char*) xmlNodeGetContent(node)), num);
                    } else if (xmlStrcmp(node->parent->parent->name, (const xmlChar*) "place") == 0) {
                        int num;
                        if (hashmap_get(context->pnml_places, (char*) id, (void**) &(num)) == MAP_MISSING) Abort("missing place");
                        lts_type_set_state_name(GBgetLTStype(model), num, (char*) xmlNodeGetContent(node));
                    }
                } else if (xmlStrcmp(node->parent->name, (const xmlChar*) "initialMarking") == 0) {
                    int num;
                    if (hashmap_get(context->pnml_places, (char*) id, (void**) &num) == MAP_MISSING) Abort("missing place");
                    const uint32_t val = (uint32_t) atol((char*) xmlNodeGetContent(node));
                    (*init_state)[num] = val;
                    if (val > max_token_count) max_token_count = val;
                } else if (xmlStrcmp(node->parent->name, (const xmlChar*) "inscription") == 0) {
                    int num;
                    if (hashmap_get(context->pnml_arcs, (char*) id, (void**) &num) == MAP_MISSING) Abort("missing arc");
                    if (context->arcs[num].num == 0) context->arcs[num].num = (uint32_t) atol((char*) xmlNodeGetContent(node));
                }
            } else if (xmlStrcmp(node->name, (const xmlChar*) "arc") == 0) {
                int num;
                if (hashmap_get(context->pnml_arcs, (char*) xmlGetProp(node, (const xmlChar*) "id"), (void**) &num) == MAP_MISSING) Abort("missing arc");
                if (context->arcs[num].num == 0) context->arcs[num].num = 1;

                const xmlChar* source = xmlGetProp(node, (const xmlChar*) "source");
                const xmlChar* target = xmlGetProp(node, (const xmlChar*) "target");
                int source_num;
                int target_num;
                if (hashmap_get(context->pnml_places, (char*) source, (void**) &source_num) == MAP_OK && hashmap_get(context->pnml_transs, (char*) target, (void**) &target_num) == MAP_OK) {
                    // this is an in arc
                    context->arcs[num].transition = target_num;
                    context->arcs[num].place = source_num;
                    context->arcs[num].type = ARC_IN;
                    context->transitions[target_num].in_arcs++;
                    context->num_in_arcs++;

                    dm_set(GBgetDMInfoRead(model), target_num, source_num);
                    dm_set(GBgetDMInfoMustWrite(model), target_num, source_num);
                    dm_set(GBgetDMInfo(model), target_num, source_num);
                } else if (hashmap_get(context->pnml_transs, (char*) source, (void**) &source_num) == MAP_OK && hashmap_get(context->pnml_places, (char*) target, (void**) &target_num) == MAP_OK) {
                    // this is an out arc
                    context->arcs[num].transition = source_num;
                    context->arcs[num].place = target_num;
                    context->arcs[num].type = ARC_OUT;
                    context->transitions[source_num].out_arcs++;

                    if (!bitvector_is_set(&(context->safe_places), target_num)) dm_set(GBgetDMInfoRead(model), source_num, target_num);
                    dm_set(GBgetDMInfoMustWrite(model), source_num, target_num);
                    dm_set(GBgetDMInfo(model), source_num, target_num);
                } else Abort("incorrect net");
            }
        }
        parse_net(node->children, model, init_state);
    }
}

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
    if (context->num_transs > 0) {
        qsort(context->arcs, context->num_arcs, sizeof(arc_t), compare_arcs);

        context->guards_info = RTmalloc(context->num_transs * sizeof(guard_t*));
        guard_t* guards = RTmalloc(sizeof(int[context->num_transs]) + sizeof(int[context->num_in_arcs]));

        for (arc_t* arc = context->arcs; arc->type != ARC_LAST; arc++) {
            const int num = arc - context->arcs;
            if (num == 0 || arc->transition != (arc - 1)->transition) {
                context->transitions[arc->transition].start = num;

                guards->count = 0;
                context->guards_info[arc->transition] = guards;
                guards += 1 + context->transitions[arc->transition].in_arcs;
            }
            if (arc->type == ARC_IN) context->guards_info[arc->transition]->guard[context->guards_info[arc->transition]->count++] = arc->place;
        }
    }
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

    context->pnml_places = hashmap_new();
    context->pnml_transs = hashmap_new();
    context->pnml_arcs = hashmap_new();

    Warning(infoLong, "Determining Petri net size");
    xmlNode* node = xmlDocGetRootElement(doc);
    find_ids(node, context);
    Warning(info, "Petri net has %d places, %d transitions and %zu arcs",
        context->num_places, context->num_transs, context->num_arcs);

    if (bitvector_create(&(context->safe_places), context->num_places) != 0) Abort("Out of memory");
    bitvector_clear(&(context->safe_places));

    Warning(infoLong, "Analyzing safe places");
    if (context->toolspecific != NULL) parse_toolspecific(context->toolspecific, context);
    Warning(info, "There are %d safe places", context->num_safe_places);

    lts_type_t ltstype;
    matrix_t* dm_info = RTmalloc(sizeof(matrix_t));
    matrix_t* dm_read_info = RTmalloc(sizeof(matrix_t));
    matrix_t* dm_must_write_info = RTmalloc(sizeof(matrix_t));

    Warning(infoLong, "Creating LTS type");

    // get ltstypes
    ltstype = lts_type_create();

    // adding types
    int int_type = lts_type_add_type(ltstype, "int", NULL);
    int act_type = lts_type_add_type(ltstype, "action", NULL);

    lts_type_set_format(ltstype, int_type, LTStypeDirect);
    lts_type_set_format(ltstype, act_type, LTStypeEnum);

    lts_type_set_state_length(ltstype, context->num_places);

    // edge label types
    lts_type_set_edge_label_count(ltstype, 1);
    lts_type_set_edge_label_name(ltstype, 0, "action");
    lts_type_set_edge_label_type(ltstype, 0, "action");
    lts_type_set_edge_label_typeno(ltstype, 0, act_type);

    const int bool_type = lts_type_add_type(ltstype, LTSMIN_TYPE_BOOL, NULL);

    for (int i = 0; i < context->num_places; ++i) {
        lts_type_set_state_typeno(ltstype, i, int_type);
        lts_type_set_state_name(ltstype, i, "tmp");
    }

    GBsetLTStype(model, ltstype); // must set ltstype before setting initial state
                                  // creates tables for types!

    GBchunkPutAt(model, bool_type, chunk_str(LTSMIN_VALUE_BOOL_FALSE), 0);
    GBchunkPutAt(model, bool_type, chunk_str(LTSMIN_VALUE_BOOL_TRUE ), 1);

    dm_create(dm_info, context->num_transs, context->num_places);
    dm_create(dm_read_info, context->num_transs, context->num_places);
    dm_create(dm_must_write_info, context->num_transs, context->num_places);

    GBsetDMInfo(model, dm_info);
    GBsetDMInfoRead(model, dm_read_info);
    GBsetDMInfoMustWrite(model, dm_must_write_info);
    GBsetSupportsCopy(model);

    context->arcs = RTalignZero(CACHE_LINE_SIZE, sizeof(arc_t[context->num_arcs + 1]));
    context->arcs[context->num_arcs].type = ARC_LAST;
    context->arcs[context->num_arcs].transition = -1;
    context->arcs[context->num_arcs].place = -1;
    context->transitions = RTmallocZero(sizeof(transition_t[context->num_transs]));

    Warning(infoLong, "Analyzing Petri net behavior");
    node = xmlDocGetRootElement(doc);
    uint32_t* init_state = RTmallocZero(sizeof(uint32_t[context->num_places]));
    parse_net(node, model, &init_state);
    Warning(info, "Petri net %s analyzed", name);
    GBsetInitialState(model, (int*) init_state);
    RTfree(init_state);

    hashmap_free(context->pnml_places);
    hashmap_free(context->pnml_transs);
    hashmap_free(context->pnml_arcs);
    xmlFreeDoc(doc);
    xmlCleanupParser();

    attach_arcs(context);

    // get next state
    GBsetNextStateLong(model, (next_method_grey_t) get_successor_long);
    GBsetNextStateShort(model, (next_method_grey_t) get_successor_short);

    GBsetExit(model, pnml_exit);

    // init state labels
    lts_type_set_state_label_count (ltstype, context->num_places);

    for(int i = 0; i < context->num_places; i++) {
        char label_name[snprintf(NULL, 0, "guard_%s", lts_type_get_state_name(ltstype, i)) + 1];
        sprintf(label_name, "guard_%s", lts_type_get_state_name(ltstype, i));
        lts_type_set_state_label_name (ltstype, i, label_name);
        lts_type_set_state_label_typeno (ltstype, i, bool_type);
    }

    // set the label group implementation
    sl_group_t* sl_group_all = RTmalloc(sizeof(sl_group_t) + context->num_places * sizeof(int));
    sl_group_all->count = context->num_places;
    for(int i = 0; i < sl_group_all->count; i++) sl_group_all->sl_idx[i] = i;
    GBsetStateLabelGroupInfo(model, GB_SL_ALL, sl_group_all);

    sl_group_t* sl_group_guards = RTmalloc(sizeof(sl_group_t) + context->num_places * sizeof(int));
    sl_group_guards->count = context->num_places;
    for(int i = 0; i < sl_group_guards->count; i++) sl_group_guards->sl_idx[i] = i;
    GBsetStateLabelGroupInfo(model, GB_SL_GUARDS, sl_group_guards);

    matrix_t *sl_info = RTmalloc(sizeof(matrix_t));
    dm_create(sl_info, context->num_places, context->num_places);
    for (int i = 0; i < context->num_places; i++) dm_set(sl_info, i, i);
    GBsetStateLabelInfo(model, sl_info);

    GBsetGuardsInfo(model, context->guards_info);

    GBsetStateLabelLong(model, get_label_long);
    GBsetStateLabelShort(model, get_label_short);

    GBsetStateLabelsGroup(model, get_labels);

    lts_type_validate(ltstype);

    if (PINS_POR) {
        Warning(infoLong, "Creating Do Not Accord matrix");
        matrix_t* dna_info = RTmalloc(sizeof(matrix_t));
        dm_create(dna_info, context->num_transs, context->num_transs);
        for (int i = 0; i < context->num_transs; i++) {
            for (int j = 0; j < context->num_transs; j++) {
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
        dm_create(gnes_info, context->num_places, context->num_transs);
        for (int i = 0; i < context->num_transs; i++) {
            for (arc_t* target = context->arcs + context->transitions[i].start; target->transition == i; target++) {
                if (target->type != ARC_OUT) continue;
                dm_set(gnes_info, target->place, i);
            }
        }
        GBsetGuardNESInfo(model, gnes_info);

        Warning(infoLong, "Creating Guard Necessary Disabling Set matrix");
        matrix_t* gnds_info = RTmalloc(sizeof(matrix_t));
        dm_create(gnds_info, context->num_places, context->num_transs);
        for (int i = 0; i < context->num_transs; i++) {
            for (arc_t* source = context->arcs + context->transitions[i].start; source->transition == i; source++) {
                if (source->type != ARC_IN) continue;
                dm_set(gnds_info, source->place, i);
            }
        }
        GBsetGuardNDSInfo(model, gnds_info);

        Warning(infoLong, "Creating Do Not Left Accord (DNB) matrix");
        matrix_t* ndb_info = RTmalloc(sizeof(matrix_t));
        dm_create(ndb_info, context->num_transs, context->num_transs);
        for (int i = 0; i < context->num_transs; i++) {
            for (int j = 0; j < context->num_transs; j++) {
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
