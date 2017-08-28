#include <hre/config.h>

#include <limits.h>
#include <stdlib.h>
#include <time.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-check.h>
#include <pins-lib/por/por-ample.h>
#include <pins-lib/por/por-beam.h>
#include <pins-lib/por/por-deletion.h>
#include <pins-lib/por/por-internal.h>
#include <pins-lib/por/por-leap.h>
#include <pins-lib/por/por-lipton.h>
#include <pins-lib/por/por-tr.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/por/pins2pins-por-check.h>
#include <util-lib/dfs-stack.h>

int SAFETY = 0;
int NO_L12 = 0;
int NO_V = 0;
int NO_MCNDS = 0;
int PREFER_NDS = 0;
int CHECK_SEEN = 1;
int NO_MC = 0;
int USE_DEL = 0;
int POR_WEAK = 0;
int TR_MODE = 0;
int TR_MAX = 1000;

static int NO_COMMUTES = 0;
static int NO_DNA = 0;
static int NO_NES = 0;
static int NO_NDS = 0;
static int NO_MDS = 0;
static int leap = 0;
static const char *algorithm = "heur";
static const char *weak = "no";

pins_por_t              PINS_POR = PINS_POR_NONE;

static si_map_entry por_weak[]={
    {"no",      WEAK_NONE},
    {"",        WEAK_HANSEN},
    {"hansen",  WEAK_HANSEN},
    {"valmari", WEAK_VALMARI},
    {NULL, 0}
};

por_alg_t    PINS_POR_ALG = POR_UNINITIALIZED;

static si_map_entry por_algorithm[]={
    {"none",    POR_NONE},
    {"",        POR_BEAM},
    {"heur",    POR_BEAM},
    {"del",     POR_DEL},
    {"tr",      POR_LIPTON},
    {"str",     POR_LIPTON},
    {"str2",    POR_TR},
    {"ample",   POR_AMPLE},
    {"ample1",  POR_AMPLE1},
    {NULL, 0}
};

#define  POR_SHORT_OPT 'p'

static void
por_popt (poptContext con, enum poptCallbackReason reason,
          const struct poptOption *opt, const char *arg, void *data)
{
    (void)con; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE: break;
    case POPT_CALLBACK_REASON_POST: break;
    case POPT_CALLBACK_REASON_OPTION:
        if (opt->shortName == POR_SHORT_OPT) {
            if (arg == NULL) arg = "";
            int num = linear_search (por_algorithm, arg);
            if (num < 0) {
                Warning (error, "unknown POR algorithm %s", arg);
                HREprintUsage();
                HREexit(LTSMIN_EXIT_FAILURE);
            }
            if ((PINS_POR_ALG = num) != POR_NONE)
                PINS_POR = PINS_CORRECTNESS_CHECK ? PINS_POR_CHECK : PINS_POR_ON;
            if (strcmp(arg, "str") == 0)
                USE_DEL = 1;
            if (PINS_POR_ALG == POR_LIPTON || PINS_POR_ALG == POR_TR)
                POR_WEAK = 1;
            return;
        } else if (opt->shortName == -1) {
            if (arg == NULL) arg = "";
            int num = linear_search (por_weak, arg);
            if (num < 0) {
                Warning (error, "unknown weak setting %s", arg);
                HREprintUsage();
                HREexit(LTSMIN_EXIT_FAILURE);
            } else {
                POR_WEAK = num;
            }
            return;
        }
        return;
    }
    Abort("unexpected call to por_popt");
}

struct poptOption por_options[]={
    {NULL, 0, POPT_ARG_CALLBACK, (void *)por_popt, 0, NULL, NULL},
    { "por", POR_SHORT_OPT, POPT_ARG_STRING | POPT_ARGFLAG_OPTIONAL,
      &algorithm, 0, "enable partial order reduction / (stubborn) transaction reduction", "<heur|del|tr|str> (default: heur)" },
    { "weak" , -1, POPT_ARG_STRING  | POPT_ARGFLAG_OPTIONAL , &weak , 0 , "Weak stubborn set theory" , "[valmari] (default: uses stronger left-commutativity)" },
    { "leap" , 0, POPT_ARG_VAL  | POPT_ARGFLAG_OPTIONAL , &leap , 1 , "Leaping POR (Cartesian product of several disjoint stubborn sets)" , NULL },

    /* HIDDEN OPTIONS FOR EXPERIMENTATION */
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, beam_options, 0, NULL, NULL},
    { "prefer-nds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &PREFER_NDS , 1 , "prefer MC+NDS over NES" , NULL },
//    { "check" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &PINS_POR , PINS_POR_CHECK , "verify partial order reduction peristent sets" , NULL },
    { "no-dna" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_DNA , 1 , "without DNA" , NULL },
    { "no-commutes" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_COMMUTES , 1 , "without commutes (for left-accordance)" , NULL },
    { "no-nes" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_NES , 1 , "without NES" , NULL },
    { "no-mds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MDS , 1 , "without MDS" , NULL },
    { "noseen" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &CHECK_SEEN , 0 , "Prefer visited successor states" , NULL },
    { "tr" , 0, POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN , &TR_MODE , 0 , "TR mode" , NULL },
    { "trmax" , 0, POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN , &TR_MAX , 0 , "TR max" , NULL },
    { "no-nds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_NDS , 1 , "without NDS (for dynamic label info)" , NULL },
    { "no-mc" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MC , 1 , "without MC" , NULL },
    { "no-mcnds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MCNDS , 1 , "Do not create NESs from MC and NDS" , NULL },
    { "no-V" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_V , 1 , "without V proviso, instead use Peled's visibility proviso, or V'     " , NULL },
    { "no-L12" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_L12 , 1 , "without L1/L2 proviso, instead use Peled's cycle proviso, or L2'   " , NULL },
    POPT_TABLEEND
};

/**
 * Initialize the structures to record visible groups.
 *
 * Disabled NDS / NES dynamic visibility. It is probably incorrect (NO_DYN_VIS == 1).
 */
static inline void
init_visible_labels (por_context* ctx)
{
    if (ctx->visible_initialized) return;
    ctx->visible_initialized = 1;

    for (int i = 0; i < ctx->nlabels; i++)
        bms_push_if (ctx->visible_labels, 0, i, ctx->label_visibility[i]);
    for (int i = 0; i < ctx->ngroups; i++) {
        if (!ctx->group_visibility[i]) continue;
        for (int *l = ci_begin(ctx->group2guard[i]); l != ci_end(ctx->group2guard[i]); l++) {
            bms_push_new (ctx->visible_labels, 0, *l);
        }
    }
    Print1 (infoLong, "POR visible labels: %d / %d", bms_count(ctx->visible_labels, 0), ctx->nlabels);

    ci_list            *vis = bms_list (ctx->visible_labels, 0);
    for (int *l = ci_begin(vis); l != ci_end(vis); l++) {
        for (int j = 0; j < ctx->label_nes[*l]->count; j++) {
            int             group = ctx->label_nes[*l]->data[j];
            bms_push_new (ctx->visible, VISIBLE_NES, group);
            bms_push_new (ctx->visible, VISIBLE, group);
        }
        for (int j = 0; j < ctx->label_nds[*l]->count; j++) {
            int group = ctx->label_nds[*l]->data[j];
            bms_push_new (ctx->visible, VISIBLE_NDS, group);
            bms_push_new (ctx->visible, VISIBLE, group);
        }
    }
    SAFETY = bms_count(ctx->visible, VISIBLE) != 0 || NO_L12;
}

static hre_key_t local_key;

typedef struct local_s {
    state_find_f find_state;
    void *find_state_ctx;
} local_t;

static inline local_t *
get_local ()
{
    local_t* loc = HREgetLocal (local_key);
    if (loc == NULL) {
        loc = RTmallocZero (sizeof(local_t));
        HREsetLocal (local_key, loc);
    }
    return loc;
}

void
por_set_find_state (state_find_f f, void *t)
{
    local_t* loc = get_local ();
    loc->find_state = f;
    loc->find_state_ctx = t;
}

bool
por_seen (int *dst, int group, bool src_changed)
{
    transition_info_t  ti = GB_TI (NULL, src_changed ? -1 : group);
    local_t           *loc = get_local ();
    HREassert (loc->find_state && loc->find_state_ctx,
            "POR seen not activated (call por_set_find_state)");

    return loc->find_state (dst, &ti, loc->find_state_ctx);
}

static void
seen_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    por_context        *ctx = context;

    size_t              c = ctx->enabled_list->count;
    ci_add_if (ctx->enabled_list, ti->group, c == 0 || ci_top(ctx->enabled_list) != ti->group); //TODO: buggy

    if (!CHECK_SEEN) return;
    size_t              s = bms_count (ctx->include, 0);;
    if (por_seen(dst, ti->group, ctx->src_changed)) {
        bms_push_if (ctx->include, 0, ti->group,
                     s == 0 || bms_top (ctx->include, 0) != ti->group);
    } else if (s > 0 && bms_top (ctx->include, 0) == ti->group) {
        bms_pop (ctx->include, 0);
    }
    (void) cpy; (void) dst;
}

void
por_seen_groups (por_context *ctx, int *src, int src_changed)
{
    // Find seen successor, for free inclusion
    ctx->enabled_list->count = 0;
    ctx->src_changed = src_changed;
    bms_clear_all (ctx->include);
    GBgetTransitionsAll (ctx->parent, src, seen_cb, ctx);
    Debug("Found %zu 'seen' groups", bms_count(ctx->include, 0));
}

bool
por_seen_group (por_context *ctx, int *src, int group, int src_changed)
{
    // Find seen successor, for free inclusion
    ctx->enabled_list->count = 0;
    ctx->src_changed = src_changed;
    bms_clear_all (ctx->include);
    GBgetTransitionsLong (ctx->parent, group, src, seen_cb, ctx);
    int ret = bms_count (ctx->include, 0) != 0;
    bms_clear_all (ctx->include);
    return ret;
    Debug("Found %zu 'seen' groups", bms_count(ctx->include, 0));
}

void
por_init_transitions (model_t model, por_context *ctx, int *src)
{
    // Find seen successor, for free inclusion
    por_seen_groups (ctx, src, false);

    init_visible_labels (ctx);

    // fill guard status, request all guard values
    GBgetStateLabelsGroup (model, GB_SL_GUARDS, src, ctx->label_status);

    ctx->visible_enabled = 0;
    int enabled = 0; // check
    // fill group status and score
    for (int i = 0; i < ctx->ngroups; i++) {
        ctx->group_status[i] = GS_ENABLED; // reset
        // mark groups as disabled
        for (int j = 0; j < ctx->group2guard[i]->count; j++) {
            int guard = ctx->group2guard[i]->data[j];
            if (ctx->label_status[guard] == 0) {
                ctx->group_status[i] = GS_DISABLED;
                break;
            }
        }
        // set group score
        if (ctx->group_status[i] == GS_ENABLED) {
            HREassert (ctx->enabled_list->data[enabled++] == i,
                       "Guards do not agree with enabledness of group %d", i);
            ctx->visible_enabled += is_visible (ctx, i);
        }
    }

    HREassert (ctx->enabled_list->count == enabled, // see TODO above
               "Guards do not agree with enabledness of group %d", ctx->enabled_list->data[enabled]);
}

/**
 * Default functions for long and short
 * Note: these functions don't work for partial order reduction,
 *       because partial order reduction selects a subset of the transition
 *       group and doesn't know beforehand whether to emit this group or not
 */
static int
por_long (model_t self, int group, int *src, TransitionCB cb, void *ctx)
{
    (void)self; (void)group; (void)src; (void)cb; (void)ctx;
    Abort ("Using Partial Order Reduction in combination with --grey or -reach? Long call failed.");
}

static int
por_short (model_t self, int group, int *src, TransitionCB cb, void *ctx)
{
    (void)self; (void)group; (void)src; (void)cb; (void)ctx;
    Abort ("Using Partial Order Reduction in combination with -reach or --cached? Short call failed.");
}

/**
 * Setup the partial order reduction layer.
 * Reads available dependencies, i.e. read/writes dependencies when no
 * NDA/NES/NDS is provided.
 */
model_t
GBaddPOR (model_t model)
{
    switch (PINS_POR) {
        case PINS_POR_NONE:     return model;
        case PINS_POR_ON:       return PORwrapper(model);
        case PINS_POR_CHECK:    return GBaddPORCheck(model);
        default: {
            HREassert (false, "Unknown POR mode: %d", PINS_POR);
            return NULL;
        }
    }
}

model_t
PORwrapper (model_t model)
{
    HREcreateLocal (&local_key, RTfree);

    if (pins_get_accepting_state_label_index(model) != -1) {
        Print1  (info, "POR layer: model may be a buchi automaton.");
        Print1  (info, "POR layer: use LTSmin's own LTL layer (--ltl) for correct POR.");
    }

    // check support for guards, fail without
    if (!pins_has_guards(model)) {
        PINS_POR = PINS_POR_NONE;
        Print1 (info, "Frontend doesn't have guards. Ignoring --por.");
        return model;
    }

    // do the setup
    model_t             pormodel = GBcreateBase ();

    por_context *ctx = RTmalloc (sizeof *ctx);
    ctx->parent = model;

    sl_group_t *guardLabels = GBgetStateLabelGroupInfo (model, GB_SL_GUARDS);
    sl_group_t* sl_guards = GBgetStateLabelGroupInfo(model, GB_SL_ALL);
    ctx->nguards = guardLabels->count;
    ctx->nlabels = pins_get_state_label_count(model);
    ctx->ngroups = pins_get_group_count(model);
    ctx->nslots = pins_get_state_variable_count (model);
    ctx->visible_initialized = 0;
    ctx->visible = bms_create (ctx->ngroups, VISIBLE_COUNT);
    HREassert (ctx->nguards <= ctx->nlabels);
    HREassert (guardLabels->sl_idx[0] == 0, "Expecting guards at index 0 of all labels.");

    Print1 (info, "Initializing POR dependencies: labels %d, guards %d",
            ctx->nlabels, ctx->nguards);

    matrix_t           *p_dm = NULL;
    matrix_t           *p_dm_w = GBgetDMInfoMayWrite (model);
    int id = GBgetMatrixID (model, LTSMIN_MATRIX_ACTIONS_READS);
    if (id == SI_INDEX_FAILED) {
        p_dm = GBgetDMInfo (model);
    } else {
        p_dm = GBgetMatrix (model, id);
    }
    HREassert (dm_ncols(p_dm) == ctx->nslots && dm_nrows(p_dm) == ctx->ngroups);

    // guard to group dependencies
    matrix_t        *p_sl = GBgetStateLabelInfo(model);
    matrix_t label_is_dependent;
    dm_create(&label_is_dependent, ctx->nlabels, ctx->ngroups);
    for (int i = 0; i < ctx->nlabels; i++) {
        for (int j = 0; j < ctx->ngroups; j++) {
            for (int k = 0; k < ctx->nslots; k++) {
                if (dm_is_set(p_sl, sl_guards->sl_idx[i], k) && dm_is_set(p_dm_w, j, k)) {
                    dm_set( &label_is_dependent, i, j );
                    break;
                }
            }
        }
    }

    // extract inverse relation, transition group to guard
    matrix_t gg_matrix;
    dm_create(&gg_matrix, ctx->ngroups, ctx->nguards);
    for (int i = 0; i < ctx->ngroups; i++) {
        guard_t *g = GBgetGuard(model, i);
        HREassert (g != NULL, "GUARD RETURNED NULL %d", i);
        for (int j = 0; j < g->count; j++) {
            dm_set(&gg_matrix, i, g->guard[j]);
        }
    }
    ctx->guard2group            = (ci_list **) dm_cols_to_idx_table(&gg_matrix);
    ctx->group2guard            = (ci_list **) dm_rows_to_idx_table(&gg_matrix);
    dm_free(&gg_matrix);

    // mark minimal necessary enabling set
    matrix_t *p_gnes_matrix = GBgetGuardNESInfo(model);
    NO_NES |= p_gnes_matrix == NULL;

    if (!NO_NES) {
        HREassert (dm_nrows(p_gnes_matrix) == ctx->nlabels &&
                   dm_ncols(p_gnes_matrix) == ctx->ngroups);
        // copy p_gnes_matrix to gnes_matrix, then optimize it
        dm_copy(p_gnes_matrix, &ctx->label_nes_matrix);
    } else {
        dm_create(&ctx->label_nes_matrix, ctx->nlabels, ctx->ngroups);
        for (int i = 0; i < ctx->nlabels; i++) {
            for (int j = 0; j < ctx->ngroups; j++) {
                dm_set (&ctx->label_nes_matrix, i, j);
            }
        }
    }
    // optimize nes
    // remove all transition groups that do not write to this guard
    for (int i = 0; i < ctx->nlabels; i++) {
        for (int j = 0; j < ctx->ngroups; j++) {
            // if guard i has group j in the nes, make sure
            // the group writes to the same part of the state
            // vector the guard reads from, otherwise
            // this value can be removed
            if (dm_is_set(&ctx->label_nes_matrix, i, j)) {
                if (!dm_is_set(&label_is_dependent, i, j)) {
                    dm_unset(&ctx->label_nes_matrix, i, j);
                }
            }
        }
    }
    ctx->label_nes   = (ci_list **) dm_rows_to_idx_table(&ctx->label_nes_matrix);

    // same for nds
    matrix_t *label_nds_matrix = GBgetGuardNDSInfo(model);
    NO_NDS |= label_nds_matrix == NULL;

    if (!NO_NDS) {
        HREassert (dm_nrows(label_nds_matrix) == ctx->nlabels &&
                   dm_ncols(label_nds_matrix) == ctx->ngroups);
        // copy p_gnds_matrix to gnes_matrix, then optimize it
        dm_copy(label_nds_matrix, &ctx->label_nds_matrix);
    } else {
        dm_create(&ctx->label_nds_matrix, ctx->nlabels, ctx->ngroups);
        for (int i = 0; i < ctx->nlabels; i++) {
            for (int j = 0; j < ctx->ngroups; j++) {
                dm_set (&ctx->label_nds_matrix, i, j);
            }
        }
    }

    // optimize nds matrix
    // remove all transition groups that do not write to this guard
    for (int i = 0; i < ctx->nlabels; i++) {
        for (int j = 0; j < ctx->ngroups; j++) {
            // if guard i has group j in the nes, make sure
            // the group writes to the same part of the state
            // vector the guard reads from, otherwise
            // this value can be removed
            if (dm_is_set(&ctx->label_nds_matrix, i, j)) {
                if (!dm_is_set(&label_is_dependent, i, j)) {
                    dm_unset(&ctx->label_nds_matrix, i, j);
                }
            }
        }
    }
    ctx->label_nds   = (ci_list **) dm_rows_to_idx_table(&ctx->label_nds_matrix);

    matrix_t nds;
    dm_create(&nds, ctx->ngroups, ctx->ngroups);
    for (int i = 0; i < ctx->ngroups; i++) {
        for (int j = 0; j < ctx->ngroups; j++) {
            if (guard_of(ctx, i, &ctx->label_nds_matrix, j)) {
                dm_set( &nds, i, j);
            }
        }
    }

    // extract guard not co-enabled and guard-nes information
    // from guard may-be-co-enabled with guard relation:
    // for a guard g, find all guards g' that may-not-be co-enabled with it
    // then, for each g', mark all groups in gnce_matrix
    matrix_t *label_mce_matrix = GBgetGuardCoEnabledInfo(model);
    matrix_t *guard_group_not_coen = NULL;
    id = GBgetMatrixID(model, LTSMIN_GUARD_GROUP_NOT_COEN);
    if (id != SI_INDEX_FAILED) {
        guard_group_not_coen = GBgetMatrix(model, id);
    }

    NO_MC |= label_mce_matrix == NULL && guard_group_not_coen == NULL;
    if (NO_MC && !NO_MCNDS) {
        if (__sync_bool_compare_and_swap(&NO_MCNDS, 0, 1)) { // static variable
            Warning (info, "No maybe-coenabled matrix found. Turning off NESs from NDS+MC.");
        }
    }

    if (!NO_MC) {
        if (guard_group_not_coen) {
            HREassert (dm_ncols(guard_group_not_coen) >= ctx->ngroups &&
                       dm_nrows(guard_group_not_coen) >= ctx->nguards);

            dm_copy (guard_group_not_coen, &ctx->gnce_matrix);
        } else {
            HREassert (dm_ncols(label_mce_matrix) >= ctx->nguards &&
                       dm_nrows(label_mce_matrix) >= ctx->nguards);

            dm_create(&ctx->gnce_matrix, ctx->nguards, ctx->ngroups);
            for (int g = 0; g < ctx->nguards; g++) {
                // iterate over all guards
                for (int gg = 0; gg < ctx->nguards; gg++) {
                    // find all guards that may not be co-enabled
                    if (dm_is_set(label_mce_matrix, g, gg)) continue;

                    // gg may not be co-enabled with g, find all
                    // transition groups in which it is used
                    for (int tt = 0; tt < ctx->guard2group[gg]->count; tt++) {
                        dm_set(&ctx->gnce_matrix, g, ctx->guard2group[gg]->data[tt]);
                    }
                }
            }
        }
        ctx->guard_nce             = (ci_list **) dm_rows_to_idx_table(&ctx->gnce_matrix);

        dm_create(&ctx->nce, ctx->ngroups, ctx->ngroups);
        for (int g = 0; g < ctx->nguards; g++) {
            for (int j = 0; j < ctx->guard_nce[g]->count; j++) {
                int t1 = ctx->guard_nce[g]->data[j];
                for (int h = 0; h < ctx->guard2group[g]->count; h++) {
                    int t2 = ctx->guard2group[g]->data[h];
                    dm_set (&ctx->nce, t1, t2);
                }
            }
        }
        ctx->group_nce             = (ci_list **) dm_rows_to_idx_table(&ctx->nce);
    }

    // extract accords with matrix
    matrix_t *not_accords_with = GBgetDoNotAccordInfo(model);
    NO_DNA |= not_accords_with == NULL;

    HREassert (NO_DNA || (dm_nrows(not_accords_with) == ctx->ngroups &&
                          dm_ncols(not_accords_with) == ctx->ngroups));

    // Combine Do Not Accord with dependency and other information
    dm_create(&ctx->not_accords_with, ctx->ngroups, ctx->ngroups);
    for (int i = 0; i < ctx->ngroups; i++) {
        for (int j = 0; j < ctx->ngroups; j++) {
            if (i == j) {
                dm_set(&ctx->not_accords_with, i, j);
            } else {

                if ( !NO_MC && dm_is_set(&ctx->nce, i , j) ) {
                    continue; // transitions never coenabled!
                }

                if ( !NO_DNA && !dm_is_set(not_accords_with, i , j) ) {
                    continue; // transitions accord with each other
                }

                if (dm_is_set(&nds,i,j) || dm_is_set(&nds,j,i)) {
                    dm_set( &ctx->not_accords_with, i, j );
                    continue;
                }

                // is dependent?
                for (int k = 0; k < ctx->nslots; k++) {
                    if ((dm_is_set( p_dm_w, i, k) && dm_is_set( p_dm, j, k)) ||
                        (dm_is_set( p_dm, i, k) && dm_is_set( p_dm_w, j, k)) ||
                        (dm_is_set( p_dm_w, i, k) && dm_is_set(p_dm_w, j, k))) {
                        dm_set( &ctx->not_accords_with, i, j );
                        break;
                    }
                }
            }
        }
    }
    ctx->not_accords = (ci_list **) dm_rows_to_idx_table(&ctx->not_accords_with);

    matrix_t *commutes = GBgetCommutesInfo (model);
    NO_COMMUTES |= commutes == NULL;

    if (POR_WEAK && (NO_NES || NO_NDS)) {
        if (__sync_bool_compare_and_swap(&POR_WEAK, 1, 0)) { // static variable
            Warning (info, "No NES/NDS, which is required for weak relations. Switching to strong stubborn sets.");
        }
    }

    if (!POR_WEAK) {
        ctx->not_left_accords  = ctx->not_accords;
        ctx->not_left_accordsn = ctx->not_accords;
    } else {
        /**
         *
         * WEAK relations
         *
         *  Guard-based POR (Journal):
         *
         *           j                    j
         *      s —------>s1         s ------->s1
         *                 |         |         |
         *                 | i  ==>  |         | i
         *                 |         |         |
         *                 v         v         v
         *                s1’        s’------>s1'
         *
         *  Valmari's weak definition (requires algorithmic changes):
         *
         *           j                    j
         *      s —------>s1         s ------>s1
         *      |          |         |         |
         *      |          | i  ==>  |         | i
         *      |          |         |         |
         *      v          v         v         v
         *      s'        s1’        s’------>s1'
         *
         *
         * Combine DNA / Commutes with must_disable and may enable
         *
         * (may enable is inverse of NES)
         *
         */

        id = GBgetMatrixID(model, LTSMIN_NOT_LEFT_ACCORDS);
        if (id != SI_INDEX_FAILED) {
            ctx->nla = GBgetMatrix(model, id);
        } else {
            matrix_t *must_disable = NULL;
            int id = GBgetMatrixID(model, LTSMIN_MUST_DISABLE_MATRIX);
            if (id != SI_INDEX_FAILED) {
                must_disable = GBgetMatrix(model, id);
            } else {
                Print1 (info, "No must-disable matrix available for weak sets.");
                NO_MDS = 1;
            }
            matrix_t *must_enable = NULL;
            id = GBgetMatrixID(model, LTSMIN_MUST_ENABLE_MATRIX);
            if (id != SI_INDEX_FAILED) {
                must_enable = GBgetMatrix(model, id);
            } else if (POR_WEAK == WEAK_VALMARI) {
                Print1 (info, "No must-enable matrix available for Valmari's weak sets.");
            }

            ctx->nla = RTmalloc(sizeof(matrix_t));
            dm_create(ctx->nla, ctx->ngroups, ctx->ngroups);
            for (int i = 0; i < ctx->ngroups; i++) {
                for (int j = 0; j < ctx->ngroups; j++) {
                    if (i == j) continue;

                    // j must disable i (OK)
                    if (!NO_MDS && all_guards(ctx, i, must_disable, j)) {
                        continue;
                    }

                    if (POR_WEAK == WEAK_VALMARI) {
                        // j must enable i (OK)
                        if (must_enable != NULL && all_guards(ctx, i, must_enable, j)) {
                            continue;
                        }

                        // i and j are never coenabled (OK)
                        if ( !NO_MC && dm_is_set(&ctx->nce, i , j) ) {
                            continue;
                        }
                    } else {
                        // j may enable i (NOK)
                        if (guard_of(ctx, i, &ctx->label_nes_matrix, j)) {
                            dm_set( ctx->nla, i, j );
                            continue;
                        }
                    }

                    if (NO_COMMUTES) {
                        // !DNA (OK)
                        if (!dm_is_set(&ctx->not_accords_with, i, j)) continue;
                    } else {
                        // i may disable j (NOK)
                        if (dm_is_set(&nds, j, i)) {
                            dm_set( ctx->nla, i, j );
                            continue;
                        }
                        // i may enable j (NOK)
                        if (guard_of(ctx, j, &ctx->label_nes_matrix, i)) {
                            dm_set( ctx->nla, i, j );
                            continue;
                        }
                        // i,j commute (OK)
                        if ( dm_is_set(commutes, i , j) ) continue;
                    }

                    // is even dependent? Front-end might miss it.
                    for (int k = 0; k < ctx->nslots; k++) {
                        if ((dm_is_set( p_dm_w, i, k) && dm_is_set( p_dm, j, k)) ||
                            (dm_is_set( p_dm, i, k) && dm_is_set( p_dm_w, j, k)) ||
                            (dm_is_set( p_dm_w, i, k) && dm_is_set(p_dm_w, j, k))) {
                            dm_set( ctx->nla, i, j );
                            break;
                        }
                    }
                }
            }
        }
        ctx->not_left_accords = (ci_list **) dm_rows_to_idx_table(ctx->nla);
        ctx->not_left_accordsn= (ci_list **) dm_cols_to_idx_table(ctx->nla);

        matrix_t dna_diff;
        dm_create(&dna_diff, ctx->ngroups, ctx->ngroups);
        for (int i = 0; i < ctx->ngroups; i++) {
            for (int j = 0; j < ctx->ngroups; j++) {
                if ( dm_is_set(&ctx->not_accords_with, i , j) &&
                    !dm_is_set(ctx->nla, i , j) ) {
                    dm_set (&dna_diff, i, j);
                }
            }
        }
        ctx->dna_diff = (ci_list **) dm_rows_to_idx_table(&dna_diff);
    }

    // free temporary matrices
    dm_free (&label_is_dependent);
    dm_free (&nds);

    // setup global group_in/group_has relation
    // idea, combine nes and nds in one data structure (ns, necessary set)
    // each guard is either disabled (use nes) or enabled (nds)
    // setup: ns = [0...n_guards-1] nes, [n_guards..2n_guards-1] nds
    // this way the search algorithm can skip the nes/nds that isn't needed based on
    // guard_status, using <n or >=n as conditions
    matrix_t group_in;
    dm_create (&group_in, NS_SIZE(ctx), ctx->ngroups);
    for (int i = 0; i < ctx->nguards; i++) {
        for (int j = 0; j < ctx->label_nes[i]->count; j++) {
            dm_set(&group_in, i, ctx->label_nes[i]->data[j]);
        }
        if (!NO_MCNDS) { // add NDS range:
            for (int j = 0; j < ctx->label_nds[i]->count; j++) {
                int group = ctx->label_nds[i]->data[j];
                dm_set(&group_in, i+ctx->nguards, group);
            }
        }
    }
    // build tables ns, and group in
    ctx->ns = (ci_list**) dm_rows_to_idx_table(&group_in);
    ctx->group2ns = (ci_list**) dm_cols_to_idx_table(&group_in);
    dm_free (&group_in);

    // group has relation
    // mapping [0...n_guards-1] disabled guard (nes)
    // mapping [n_guards..2*n_guards-1] enabled guard (nds)
    // group has relation is more difficult because nds
    // needs not co-enabled info and transition groups
    matrix_t group_has;
    dm_create (&group_has, ctx->ngroups, NS_SIZE(ctx));
    for (int i = 0; i < ctx->nguards; i++) {
        // nes
        for (int j = 0; j < ctx->guard2group[i]->count; j++) {
            dm_set(&group_has, ctx->guard2group[i]->data[j], i);
        }
        // nds
        if (!NO_MCNDS) { // add NDS range:
            for (int j = 0; j < ctx->guard_nce[i]->count; j++) {
                dm_set(&group_has, ctx->guard_nce[i]->data[j], i + ctx->nguards);
            }
        }
    }
    // build table group has
    ctx->group_has = (ci_list**) dm_rows_to_idx_table(&group_has);
    ctx->group_hasn = (ci_list**) dm_cols_to_idx_table(&group_has);
    dm_free (&group_has);

    if (PREFER_NDS) {
        for (int i = 0; i < ctx->ngroups; i++) {
            list_invert (ctx->group_has[i]);
        }
    }

    GBsetContext (pormodel, ctx);
    GBsetNextStateLong  (pormodel, por_long);
    GBsetNextStateShort (pormodel, por_short);
    next_method_black_t next_all;
    switch (PINS_POR_ALG) {
    case POR_AMPLE: {
        next_all = ample_search_all;
        ctx->alg = ample_create_context (ctx, true);
        break;
    }
    case POR_AMPLE1: {
        next_all = ample_search_one;
        ctx->alg = ample_create_context (ctx, false);
        break;
    }
    case POR_BEAM: {
        next_all = beam_search_all;
        ctx->alg  = beam_create_context (ctx);
        break;
    }
    case POR_DEL: {
        next_all = del_por_all;
        ctx->alg = del_create (ctx);
        break;
    }
    case POR_LIPTON: {
        next_all = lipton_por_all;
        ctx->alg = lipton_create (ctx, pormodel);
        break;
    }
    case POR_TR: {
        next_all = tr_por_all;
        ctx->alg = tr_create (ctx, pormodel);
        break;
    }
    default: Abort ("Unknown POR algorithm: '%s'", key_search(por_algorithm, PINS_POR_ALG));
    }
    GBsetNextStateAll   (pormodel, next_all);

    if (leap) {
        HREassert (PINS_POR_ALG != POR_LIPTON && PINS_POR_ALG != POR_TR, "Lipton reduction and leaping sets cannot be combined.");
        // changes POR model (sets modified r/w matrices)
        ctx->leap = leap_create_context (pormodel, model, next_all);
        GBsetNextStateAll   (pormodel, leap_search_all);
    }

    GBinitModelDefaults (&pormodel, model);

    if (GBgetPorGroupVisibility(pormodel) == NULL) {
        // reserve memory for group visibility, will be provided by ltl layer or tool
        ctx->group_visibility = RTmallocZero( ctx->ngroups * sizeof(int) );
        GBsetPorGroupVisibility  (pormodel, ctx->group_visibility);
    } else {
        ctx->group_visibility = GBgetPorGroupVisibility(pormodel);
    }
    if (GBgetPorStateLabelVisibility(pormodel) == NULL) {
        // reserve memory for group visibility, will be provided by ltl layer or tool
        ctx->label_visibility = RTmallocZero( ctx->nlabels * sizeof(int) );
        GBsetPorStateLabelVisibility  (pormodel, ctx->label_visibility);
    } else {
        ctx->label_visibility = GBgetPorStateLabelVisibility (pormodel);
    }

    ctx->enabled_list = ci_create (ctx->ngroups);

    int                 s0[ctx->nslots];
    GBgetInitialState (model, s0);
    GBsetInitialState (pormodel, s0);

    ctx->group_status = RTmallocZero(ctx->ngroups * sizeof(char));
    ctx->label_status = RTmallocZero(ctx->nguards * sizeof(int));

    ctx->nes_score    = RTmallocZero(NS_SIZE(ctx) * sizeof(int));
    ctx->group_score  = RTmallocZero(ctx->ngroups * sizeof(int));

    ctx->include = bms_create (ctx->ngroups, 1);
    ctx->exclude = bms_create (ctx->ngroups, 1);
    ctx->fix     = bms_create (ctx->ngroups, 1);

    ctx->visible_labels = bms_create (ctx->nlabels, 1);

    return pormodel;
}

bool
por_is_stubborn (por_context *ctx, int group)
{
    if (leap)           return leap_is_stubborn (ctx, group);

    switch (PINS_POR_ALG) {
    case POR_AMPLE:
    case POR_AMPLE1:    return ample_is_stubborn (ctx, group);
    case POR_BEAM:      return beam_is_stubborn (ctx, group);
    case POR_DEL:       return del_is_stubborn ((del_ctx_t *)ctx->alg, group);
    case POR_LIPTON:    return lipton_is_stubborn (ctx, group);
    case POR_TR:        return tr_is_stubborn (ctx, group);
    default: Abort ("Unknown POR algorithm: '%s'", key_search(por_algorithm, PINS_POR_ALG));
    }
}

void
por_stats (model_t model)
{
    if (leap) {
        leap_stats (model);
        return;
    }

    switch (PINS_POR_ALG) {
    case POR_AMPLE:     break;
    case POR_AMPLE1:    break;
    case POR_BEAM:      break;
    case POR_DEL:       break;
    case POR_LIPTON:    lipton_stats (model); break;
    case POR_TR:        tr_stats (model); break;
    default: return;
    }
}

void
hook_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    prov_t *infoctx = (prov_t *)context;
    transition_info_t ti_new = GB_TI (ti->labels, ti->group);
    ti_new.por_proviso = infoctx->force_proviso_true;
    infoctx->cb(infoctx->user_context, &ti_new, dst, cpy);
    // catch transition info status
    if (infoctx->force_proviso_true || ti_new.por_proviso) {
        infoctx->por_proviso_true_cnt++;
    } else {
        infoctx->por_proviso_false_cnt++;
    }
}
