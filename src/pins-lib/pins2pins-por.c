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
#include <pins-lib/pins2pins-por.h>
#include <pins-lib/pins-util.h>
#include <util-lib/bitmultiset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>


int NO_L12 = 0;
static int NO_COMMUTES = 0;
static int NO_HEUR = 0;
static int MAX_BEAM = -1;
static int NO_HEUR_BEAM = 0;
static int NO_DNA = 0;
static int NO_NES = 0;
static int NO_NDS = 0;
static int NO_MDS = 0;
static int NO_MCNDS = 0;
static int NO_MC = 0;
static int USE_MC = 0;
static int NO_DYN_VIS = 0;
static int NO_V = 0;
static int PREFER_NDS = 0;
static int RANDOM = 0;
static const char *algorithm = "heur";
static const char *weak = "no";

static int SAFETY = 0;

typedef enum {
    WEAK_NONE = 0,
    WEAK_HANSEN,
    WEAK_VALMARI
} por_weak_t;

static si_map_entry por_weak[]={
    {"no",      WEAK_NONE},
    {"",        WEAK_HANSEN},
    {"hansen",  WEAK_HANSEN},
    {"valmari", WEAK_VALMARI},
    {NULL, 0}
};

int POR_WEAK = 0; //extern

typedef enum {
    POR_NONE,
    POR_HEUR,
    POR_DEL,
    POR_SCC,
    POR_SCC1,
    POR_AMPLE,
    POR_AMPLE1,
} por_alg_t;

static por_alg_t alg = -1;

static si_map_entry por_algorithm[]={
    {"none",    POR_NONE},
    {"",        POR_HEUR},
    {"heur",    POR_HEUR},
    {"del",     POR_DEL},
    {"scc",     POR_SCC},
    {"scc1",    POR_SCC1},
    {"ample",   POR_AMPLE},
    {"ample1",  POR_AMPLE1},
    {NULL, 0}
};

static void
por_popt (poptContext con, enum poptCallbackReason reason,
          const struct poptOption *opt, const char *arg, void *data)
{
    (void)con; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE: break;
    case POPT_CALLBACK_REASON_POST: break;
    case POPT_CALLBACK_REASON_OPTION:
        if (opt->shortName == 'p') {
            if (arg == NULL) arg = "";
            int num = linear_search (por_algorithm, arg);
            if (num < 0) {
                Warning (error, "unknown POR algorithm %s", arg);
                HREprintUsage();
                HREexit(LTSMIN_EXIT_FAILURE);
            }
            if ((alg = num) != POR_NONE)
                PINS_POR = PINS_POR_ON;
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
    { "por", 'p', POPT_ARG_STRING | POPT_ARGFLAG_OPTIONAL | POPT_ARGFLAG_SHOW_DEFAULT,
      &algorithm, 0, "enable partial order reduction", "<|heur|del|scc>" },

    /* HIDDEN OPTIONS FOR EXPERIMENTATION */

    { "check" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &PINS_POR , PINS_POR_CHECK , "verify partial order reduction peristent sets" , NULL },
    { "no-dna" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_DNA , 1 , "without DNA" , NULL },
    { "no-commutes" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_COMMUTES , 1 , "without commutes (for left-accordance)" , NULL },
    { "no-nes" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_NES , 1 , "without NES" , NULL },
    { "no-heur" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_HEUR , 1 , "without heuristic" , NULL },
    { "beam" , 0, POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN , &MAX_BEAM , 1 , "maximum number of beam searches" , NULL },
    { "no-heur-beam" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_HEUR_BEAM , 1 , "without heuristic / beam search" , NULL },
    { "no-mds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MDS , 1 , "without MDS" , NULL },
    { "no-nds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_NDS , 1 , "without NDS (for dynamic label info)" , NULL },
    { "no-mc" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MC , 1 , "without MC" , NULL },
    { "use-mc" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &USE_MC , 1 , "Use MC to reduce search path" , NULL },
    { "no-mcnds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MCNDS , 1 , "Do not create NESs from MC and NDS" , NULL },
    { "no-dynamic-labels" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_DYN_VIS , 1 , "without dynamic visibility" , NULL },
    { "no-V" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_V , 1 , "without V proviso, instead use Peled's visibility proviso, or V'     " , NULL },
    { "no-L12" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_L12 , 1 , "without L1/L2 proviso, instead use Peled's cycle proviso, or L2'   " , NULL },
    { "prefer-nds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &PREFER_NDS , 1 , "prefer MC+NDS over NES" , NULL },
    { "por-random" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &RANDOM , 1 , "randomize enabled and NES selection" , NULL },
    { "weak" , -1, POPT_ARG_STRING  | POPT_ARGFLAG_OPTIONAL | POPT_ARGFLAG_DOC_HIDDEN , &weak , 0 , "Weak stubborn set theory" , NULL },
    POPT_TABLEEND
};

const char* keyNames[] = {"None", "Any", "Visible", "Invisible"};
typedef enum {
    KEY_NONE,
    KEY_ANY,
    KEY_VISIBLE,
    KEY_INVISIBLE
} key_type_t;

typedef enum {
    VISIBLE,            // group or label NES/NDS
    VISIBLE_GROUP,      // group
    VISIBLE_NES,        // label NES
    VISIBLE_NDS,        // label NDS
    VISIBLE_COUNT
} visible_t;

static inline int
is_visible (por_context* ctx, int group)
{
    return bms_has(ctx->visible, VISIBLE, group);
}

// number of necessary sets (halves if MC is absent, because no NDSs then)
static inline int
NS_SIZE (por_context* ctx)
{
    return NO_MCNDS ? ctx->nguards : ctx->nguards << 1;
}

/**
 * Initialize the structures to record visible groups.
 */
static inline void
init_visible_labels (por_context* ctx)
{
    if (ctx->visible != NULL) return;
    NO_DYN_VIS |= NO_V;

    model_t model = ctx->parent;
    int groups = dm_nrows (GBgetDMInfo(model));
    ctx->visible = bms_create (groups, VISIBLE_COUNT);
    ctx->visible_nes = bms_create (groups, 8);
    ctx->visible_nds = bms_create (groups, 8);

    for (int i = 0; i < groups; i++) {
        if (!ctx->group_visibility[i]) continue;
        bms_push_new (ctx->visible, VISIBLE, i);
        bms_push_new (ctx->visible, VISIBLE_GROUP, i);
    }

    int c = 0;
    for (int i = 0; i < ctx->nlabels; i++) {
        if (!ctx->label_visibility[i]) continue;

        for (int j = 0; j < ctx->label_nes[i]->count; j++) {
            int group = ctx->label_nes[i]->data[j];
            bms_push_new (ctx->visible, VISIBLE_NES, group);
            bms_push_new (ctx->visible, VISIBLE, group);
            bms_push_new (ctx->visible_nes, c, group);
        }
        for (int j = 0; j < ctx->label_nds[i]->count; j++) {
            int group = ctx->label_nds[i]->data[j];
            bms_push_new (ctx->visible, VISIBLE_NDS, group);
            bms_push_new (ctx->visible, VISIBLE, group);
            bms_push_new (ctx->visible_nds, c, group);
        }

        HREassert (++c <= 8, "Only 8 dynamic labels supported currently.");
    }
    int vgroups = bms_count(ctx->visible, VISIBLE_GROUP);
    if (!NO_DYN_VIS && vgroups > 0 && vgroups != bms_count(ctx->visible, VISIBLE)) {
        Print1 (info, "Turning off dynamic visibility in presence of visible groups");
        NO_DYN_VIS = 1;
    }
    if (!NO_DYN_VIS && (alg == POR_AMPLE || alg == POR_AMPLE1)) {
        Print1 (info, "Turning off dynamic visibility for ample-set algorithm.");
        NO_DYN_VIS = 1;
    }
    SAFETY = bms_count(ctx->visible, VISIBLE) != 0 || NO_L12;
}

static void
por_init_transitions (model_t model, por_context *ctx, int *src)
{
    init_visible_labels (ctx);

    // fill guard status, request all guard values
    GBgetStateLabelsGroup (model, GB_SL_GUARDS, src, ctx->label_status);

    ctx->visible_enabled = 0;
    ctx->visible_nes_enabled = 0;
    ctx->visible_nds_enabled = 0;
    ctx->enabled_list->count = 0;
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
            ctx->enabled_list->data[ctx->enabled_list->count++] = i;
            ctx->visible_enabled += is_visible (ctx, i);
            char vis = ctx->visible->set[i];
            ctx->visible_nes_enabled += (vis & (1 << VISIBLE_NES)) != 0;
            ctx->visible_nds_enabled += (vis & (1 << VISIBLE_NDS)) != 0;
        }
    }
    Debugf ("Visible %d, +enabled %d/%d (NES: %d, NDS: %d)\n", bms_count(ctx->visible,VISIBLE),
           ctx->visible_enabled, ctx->enabled_list->count, ctx->visible_nes_enabled, ctx->visible_nds_enabled);
}

typedef enum {
    ES_SELECTED     = 0x01,
    ES_READY        = 0x02,
    ES_EMITTED      = 0x04,
} emit_status_t;

/**
 * The analysis has multiple search contexts (search_context).
 * Each search context is a search in a different state started form a different
 * initial transition. The beam search will always start working on the
 * search context with the lowest score.
 */
typedef struct search_context {
    int             idx;            // index of this search context
    int             group;          // initial enabled group of search
    int             initialized;    // initialized arrays?
    char           *emit_status;    // status of each transition group
    int            *nes_score;      // nes score
    ci_list        *work_enabled;   // search queue of enabled transitions
    ci_list        *work_disabled;  // search queue of disabled transitions
    ci_list        *enabled;        // enabled transitions selected
    ci_list        *emitted;        // emitted
    int             score_disabled; // search weight
    int             score_visible;  // selected number of visible transitions
    int             score_vis_en;   // selected number of visible and enabled transitions
    key_type_t      has_key;        // key transitions type
    int             visible_nes;    // set of visible NESs included
    int             visible_nds;    // set of visible NDSs included
} search_context_t;

typedef struct beam_s {
    int              beam_used;     // number of search contexts in use
    search_context_t**search;       // search contexts
} beam_t;

static inline void
incr_ns_update (por_context *ctx, int group, int new_group_score)
{
    int oldgroup_score = ctx->group_score[group];
    if (oldgroup_score == new_group_score) return;

    ctx->group_score[group] = new_group_score;
    for (int i = 0; i < ctx->group2ns[group]->count; i++) {
        int ns = ctx->group2ns[group]->data[i];
        ctx->nes_score[ns] += new_group_score - oldgroup_score;
    }
}

static void
por_transition_costs (por_context *ctx)
{
    if (NO_HEUR) return;

    // set score for enable transitions
    if (PINS_LTL || SAFETY) {
        for (int i = 0; i < ctx->ngroups; i++) {
            int             new_score;
            int             vis;
            if (ctx->group_status[i] == GS_DISABLED) {
                new_score = 1;
            } else if ((vis = ctx->visible->set[i])) {
                if (NO_V) {
                    new_score = ctx->enabled_list->count * ctx->ngroups;
                } else {
                    bool nes = vis & (1 << VISIBLE_NES);
                    bool nds = vis & (1 << VISIBLE_NDS);
                    if (NO_DYN_VIS || (nes && nds)) {
                        new_score = ctx->visible_enabled * ctx->ngroups +
                                bms_count(ctx->visible, VISIBLE) - ctx->visible_enabled;
                    } else if (nes) {
                        new_score = ctx->visible_nds_enabled * ctx->ngroups +
                                bms_count(ctx->visible, VISIBLE_NDS) - ctx->visible_nds_enabled;
                    } else { // VISIBLE_NDS:
                        new_score = ctx->visible_nes_enabled * ctx->ngroups +
                                bms_count(ctx->visible, VISIBLE_NES) - ctx->visible_nes_enabled;
                    }
                }
            } else {
                new_score = ctx->ngroups;
            }
            incr_ns_update (ctx, i, new_score);
        }
    } else {
        for (int i = 0; i < ctx->ngroups; i++) {
            int enabled = ctx->group_status[i] == GS_ENABLED;
            incr_ns_update (ctx, i, enabled ? ctx->ngroups : 1);
        }
    }
}

/**
 * The function update_score is called whenever a new group is added to the stubborn set
 * It takes care of updating the heuristic function for the nes based on the new group selection
 */
static inline void
update_ns_scores (por_context* ctx, search_context_t *s, int group)
{
    // change the heuristic function according to selected group
    for(int k=0 ; k< ctx->group2ns[group]->count; k++) {
        int ns = ctx->group2ns[group]->data[k];
        // note: this selects some nesses that aren't used, but take the extra work
        // instead of accessing the guards to verify the work
        s->nes_score[ns] -= ctx->group_score[group];
        HRE_ASSERT (s->nes_score[ns] >= 0, "FAILURE! NS: %s%d, group: %d", ns < ctx->nguards ? "+" : "-", ns, group);
    }
}

/**
 * Mark a group selected, update counters and NS scores.
 */
static inline bool
select_group (por_context* ctx, search_context_t *s, int group,
              bool update_scores)
{
    if (s->emit_status[group] & ES_SELECTED) { // already selected
        Debugf ("(%d), ", group);
        return false;
    }
    s->emit_status[group] |= ES_SELECTED;

    if (!NO_HEUR && update_scores) {
        update_ns_scores (ctx, s, group);
    }

    // and add to work array and update counts
    int visible = is_visible (ctx, group);
    if (ctx->group_status[group] & GS_DISABLED) {
        s->work_disabled->data[s->work_disabled->count++] = group;
        s->score_disabled += 1;
    } else {
        s->work_enabled->data[s->work_enabled->count++] = group;
        s->score_vis_en += visible;
        s->enabled->data[s->enabled->count++] = group;
    }
    s->score_visible += visible;
    Debugf ("%d, ", group);
    return true;
}

/**
 * These functions emits the persistent set with cycle proviso
 * To ensure this proviso extra communication with the algorithm is required.
 * The algorithm annotates each transition with the por_proviso flag.
 * For ltl, all selected transition groups in the persistent set must
 * have this por_proviso flag set to true, otherwise enabled(s) will be returned.
 * For safety, the proviso needs to hold for at least on emitted state.
 * The client may (should) set the proviso always to true for deadlocks.
 */

typedef struct proviso_s {
    TransitionCB    cb;
    void           *user_context;
    int             por_proviso_true_cnt;
    int             por_proviso_false_cnt;
    int             force_proviso_true;     // feedback to algorithm that proviso already holds
} proviso_t;

void hook_cb (void *context, transition_info_t *ti, int *dst, int *cpy) {
    proviso_t *infoctx = (proviso_t *)context;
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

static inline int
emit_all (por_context *ctx, ci_list *list, proviso_t *provctx, int *src)
{
    int c = 0;
    for (int z = 0; z < list->count; z++) {
        int i = list->data[z];
        c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, provctx);
    }
    return c;
}

static inline int
emit_new (por_context *ctx, ci_list *list, proviso_t *provctx, int *src)
{
    beam_t             *beam = (beam_t *) ctx->beam_ctx;
    search_context_t   *s = beam->search[0];
    int c = 0;
    for (int z = 0; z < list->count; z++) {
        int i = list->data[z];
        if (s->emit_status[i] & ES_EMITTED) continue;
        s->emit_status[i] |= ES_EMITTED;
        s->emitted->data[s->emitted->count++] = i;
        c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, provctx);
    }
    return c;
}

/**
 * Based on the heuristic and find the cheapest NS (NES/NDS) for a disabled
 * group.
 */
static inline int
beam_cheapest_ns (por_context* ctx, search_context_t *s, int group, int *cost)
{
    *cost = INT32_MAX;
    int             n_guards = ctx->nguards;
    int             count = ctx->group_has[group]->count;
    int             selected_ns = -1;
    int             c = RANDOM ? clock() : 0;
    for (int i = 0; i < count; i++) {
        int index = i;
        if (RANDOM) index = (i + c) % count;
        int ns = ctx->group_has[group]->data[index];
        if (!NO_HEUR && s->nes_score[ns] >= *cost) continue;

        // check guard status for ns (nes for disabled and nds for enabled):
        if ((ns < n_guards && (ctx->label_status[ns] == 0))  ||
            (ns >= n_guards && (ctx->label_status[ns-n_guards] != 0)) ) {

            selected_ns = ns;
            *cost = s->nes_score[ns];
            if (NO_HEUR || *cost == 0) return selected_ns; // can't improve
        }
    }
    return selected_ns;
}

static inline bool
select_all (por_context* ctx, search_context_t *s, ci_list *list,
            bool update_scores)
{
    bool added_new = false;
    for (int i = 0; i < list->count; i++) {
        int group = list->data[i];
        added_new |= select_group (ctx, s, group, update_scores);
    }
    return added_new;
}

static inline int
beam_is_key (por_context *ctx, search_context_t *s, int group, int max_score)
{
    int                 score = 0;
    for (int t = 0; t < ctx->dna_diff[group]->count && score < max_score; t++) {
        int tt = ctx->dna_diff[group]->data[t];
        int fresh = (s->emit_status[tt] & ES_SELECTED) == 0;
        score += fresh ? ctx->group_score[tt] : 0;
    }
    return score;
}

static void
beam_add_all_for_enabled (por_context *ctx, search_context_t *s, int group,
                          bool update_scores)
{

    if (POR_WEAK == WEAK_NONE && USE_MC) {
        // include all never coenabled transitions without searching from them!

        for (int i = 0; i < ctx->group_nce[group]->count; i++) {
            int nce = ctx->group_nce[group]->data[i];
            HREassert (ctx->group_status[nce] & GS_DISABLED, "Error in maybe-coenabled relation: %d - %d are coenabled.", group, nce);

            if (s->emit_status[nce] & ES_READY) continue;
            if (s->emit_status[nce] & ES_SELECTED) {
                s->emit_status[nce] |= ES_READY; // skip in search
                continue;
            }

            if (!NO_HEUR)
                update_ns_scores (ctx, s, nce);
            s->score_disabled += 1;
            s->score_visible += is_visible (ctx, nce);
        }
    }

    // V proviso only for LTL
    if ((PINS_LTL || SAFETY) && is_visible(ctx, group)) {
        if (NO_V) { // Use Peled's stronger visibility proviso:
            s->enabled->count = ctx->enabled_list->count; // selects all enabled
            return;
        }
        Debugf ("visible=[ ");
        if (NO_DYN_VIS) {
            select_all (ctx, s, ctx->visible->lists[VISIBLE], update_scores);
        } else {
            int newNDS = ctx->visible_nes->set[group] ^ s->visible_nds; // g in NES ==> NDS C Ts
            int newNES = ctx->visible_nds->set[group] ^ s->visible_nes; // g in NDS ==> NES C Ts
            if (newNES || newNDS) {
                for (size_t i = 0; i < ctx->visible_nds->types; i++) {
                    if ((1 << i) & newNDS) {
                        select_all (ctx, s, ctx->visible_nds->lists[i], update_scores);
                    }
                    if ((1 << i) & newNES) {
                        select_all (ctx, s, ctx->visible_nes->lists[i], update_scores);
                    }
                }
                s->visible_nds |= newNDS;
                s->visible_nes |= newNES;
            }
        }
        Debugf ("] ");
    }

    ci_list **accords = POR_WEAK ? ctx->not_left_accords : ctx->not_accords;
    for (int j = 0; j < accords[group]->count; j++) {
        int dependent_group = accords[group]->data[j];
        select_group (ctx, s, dependent_group, update_scores);
    }

    if (POR_WEAK == WEAK_VALMARI) {
        Debugf ("weak=[ ");
        int         cost_ndss = beam_is_key (ctx, s, group, INT32_MAX);
        int         cost_nes  = INT32_MAX;
        int         selected_ns = -1;
        if (cost_ndss != 0)
            selected_ns = beam_cheapest_ns (ctx, s, group, &cost_nes); //maybe -1
        if (cost_ndss <= cost_nes) {
            if (s->has_key == KEY_VISIBLE || s->has_key == KEY_ANY)
                s->has_key = !is_visible(ctx, group) ? KEY_INVISIBLE : KEY_VISIBLE;
            if (cost_ndss > 0) {
                select_all (ctx, s, ctx->dna_diff[group], true);
            }
        } else if (cost_nes > 0){
            select_all (ctx, s, ctx->ns[selected_ns], true);
        }
        Debugf ("] ");
    }
}

static inline int
beam_cmp (search_context_t *s1, search_context_t *s2)
{
    //if (s1->score != s2->score)
        return s1->enabled->count - s2->enabled->count;
    //int a = s1->disabled_score + s1->score- s1->work_disabled;
    //int b = s2->disabled_score + s2->score - s2->work_disabled;
    //return (a << 15) / s1->work_disabled - (b << 15) / s2->work_disabled;
}

bool check_sort (por_context *ctx) {
    beam_t             *beam = (beam_t *) ctx->beam_ctx;
    for (int i = 0; i < beam->beam_used; i++)
        if (i > 0 && beam_cmp(beam->search[i-1], beam->search[i]) > 0) return false;
    return true;
}

/**
 * Sorts BEAM search contexts.
 */
static inline bool
beam_sort (por_context *ctx)
{
    beam_t             *beam = (beam_t *) ctx->beam_ctx;
    search_context_t   *s = beam->search[0];

    int bubble = 1;
    while (bubble < beam->beam_used && beam_cmp(s, beam->search[bubble]) > 0) {
        beam->search[bubble - 1] = beam->search[bubble];
        bubble++;
    }
    beam->search[bubble - 1] = s;

    // didn't move and no more work
    if (beam->search[0] == s && s->work_disabled->count == 0 && s->work_enabled->count == 0) {
        Debugf ("bailing out, no more work (selected: %d)\n", s->enabled->count);
        HRE_ASSERT (check_sort(ctx), "unsorted!");
        return false;
    }
    return true;
}

/**
 * Analyze NS is the function called to find the smallest persistent set
 * It builds stubborn sets in multiple search contexts, by using a beam
 * search it switches search context each time the score of the current context
 * (based on heuristic function h(x) (nes_score)) isn't the lowest score anymore
 *
 * Assumes sorted beam contexts array
 */
static inline void
beam_search (por_context *ctx)
{
    if (ctx->enabled_list->count <= 1) return;
    beam_t             *beam = (beam_t *) ctx->beam_ctx;

    Debugf ("BEAM search (re)started (enabled: %d)\n", ctx->enabled_list->count);
    do {
        search_context_t   *s = beam->search[0];
        // quit the current search when all transitions are included
        if (s->enabled->count >= ctx->enabled_list->count) {
            s->work_enabled->count = s->work_disabled->count = 0;
            Debugf ("BEAM-%d abandoning search |ss|=|en|=%d\n", s->idx, ctx->enabled_list->count);
            break;
        }

        if (!s->initialized) { // init search (expensive)
            memset(s->emit_status, 0, sizeof(char[ctx->ngroups]));
            if (!NO_HEUR) {
                memcpy(s->nes_score, ctx->nes_score, sizeof(int[NS_SIZE(ctx)]));
                update_ns_scores (ctx, s, s->group);
            }
            s->initialized = 1;
            Debugf ("BEAM-%d Initializing scores for group %d\n", s->idx, s->group);
        }

        // while there are no enabled, but some disabled transitions:
        int             group;
        while (s->work_enabled->count == 0 && s->work_disabled->count > 0) {
            group = s->work_disabled->data[--s->work_disabled->count];
            if (s->emit_status[group] & ES_READY) continue;
            s->emit_status[group] |= ES_SELECTED | ES_READY;

            Debugf ("BEAM-%d investigating group %d (disabled) --> ", s->idx, group);
            int         cost;
            int         selected_ns = beam_cheapest_ns (ctx, s, group, &cost);
            HREassert (selected_ns != -1, "No NES found for group %d", group);
            if (cost != 0) { // only if cheapest NES has cost
                select_all (ctx, s, ctx->ns[selected_ns], true);
            }
            Debugf (" (ns %d (%s))\n", selected_ns % ctx->nguards,
                    selected_ns < ctx->nguards ? "disabled" : "enabled");
        }

        // if the current search context has enabled transitions, handle all of them
        while (s->work_enabled->count > 0 && beam_cmp(s, beam->search[1]) <= 0) {
            group = s->work_enabled->data[--s->work_enabled->count];
            if (s->emit_status[group] & ES_READY) continue;
            s->emit_status[group] |= ES_SELECTED | ES_READY;

            Debugf ("BEAM-%d investigating group %d (enabled) --> ", s->idx, group);
            beam_add_all_for_enabled (ctx, s, group, true); // add DNA
            Debugf ("\n");
        }
    } while (beam_sort(ctx));
}

static inline int
beam_min_key_group (por_context* ctx, search_context_t *s, bool invisible)
{
    size_t min_score = INT32_MAX;
    int min_group = -1;
    for (int i = 0; i < s->enabled->count; i++) {
        int group = s->enabled->data[i];
        if (invisible && is_visible(ctx, group)) continue;

        size_t score = beam_is_key (ctx, s, group, min_score);
        if (score == 0) {
            Debugf ("BEAM %d has key: %d (all NDSs already included)\n", s->idx, group);
            return -1;
        }
        if (score < min_score) {
            min_score = score;
            min_group = group;
        }
    }
    return min_group;
}

static inline int
beam_min_invisible_group (por_context* ctx, search_context_t *s)
{
    size_t min_score = INT32_MAX;
    int min_group = -1;
    for (int i = 0; i < ctx->enabled_list->count; i++) {
        int group = ctx->enabled_list->data[i];
        if ((s->emit_status[group] & ES_SELECTED) || is_visible(ctx, group)) continue;

        size_t              score = 0;
        ci_list **accords = POR_WEAK ? ctx->not_left_accords : ctx->not_accords;
        for (int j = 0; j < accords[group]->count; j++) {
            int dependent = accords[group]->data[j];
            int fresh = (s->emit_status[dependent] & ES_SELECTED) == 0;
            score += fresh ? ctx->group_score[dependent] : 0;
        }
        if (score == 0) return group;
        if (score < min_score) {
            min_score = score;
            min_group = group;
        }
    }
    return min_group;
}

static inline void
beam_ensure_invisible_and_key (por_context* ctx)
{
    if (!POR_WEAK && !(SAFETY || PINS_LTL)) return;

    Debugf ("ADDING KEY <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
    beam_t             *beam = (beam_t *) ctx->beam_ctx;
    while (true) {
        search_context_t   *s = beam->search[0];
        if (s->enabled->count == ctx->enabled_list->count) {
            Debugf ("BEAM %d needs no (invisible) key\n", s->idx);
            break;
        }

        bool need_invisible = (SAFETY || PINS_LTL) &&
                                 ctx->visible_enabled != ctx->enabled_list->count;
        if (need_invisible && s->has_key == KEY_INVISIBLE) {
            Debugf ("BEAM %d has invisible key\n", s->idx);
            break;
        }
        if (!need_invisible && s->has_key != KEY_NONE) {
            Debugf ("BEAM %d has key (%s)\n", s->idx, keyNames[s->has_key]);
            break;
        }

        bool has_invisible = s->score_vis_en != s->enabled->count;
        if (need_invisible && !has_invisible) {
            int i_group = beam_min_invisible_group (ctx, s);
            Debugf ("BEAM %d adding invisible key: %d", s->idx, i_group);
            select_group (ctx, s, i_group, true);
            if (POR_WEAK) { // make it also key
                Debugf (" +strong[ ");
                select_all (ctx, s, ctx->dna_diff[i_group], true);
                Debugf ("]\n");
            }
            s->has_key = KEY_INVISIBLE;
        } else if (POR_WEAK) {
            int key_group = -1;
            if (need_invisible && has_invisible) {
                key_group = beam_min_key_group (ctx, s, true);
                s->has_key = KEY_INVISIBLE;
            } else { // only visible available:
                key_group = beam_min_key_group (ctx, s, false);
                s->has_key = KEY_VISIBLE;
            }
            if (key_group != -1) {
                Debugf ("BEAM %d making %d key: ", s->idx, key_group);
                select_all (ctx, s, ctx->dna_diff[key_group], true);
                Debugf (".\n");
            }
        } else {
            s->has_key = KEY_ANY;
            Debugf ("BEAM %d key is available and I proviso met\n", s->idx);
            break;
        }

        beam_sort (ctx);
        beam_search (ctx); // may switch context
    }
    Debugf (">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
}

/**
 * Visible transtions:      Tv
 * Invisible transtions:    Ti = T \ Tv
 * Stubborn set:            Ts
 * Keys in stubborn set:    Tk
 *
 * V  = Tv n Ts != {}     ==>  Tv C Ts
 * L1 = Ti n en(s) != {}  ==>  Tk n Ti != {}
 * L2 = s closes cycle    ==>  Tv C Ts
 *
 * Whether s closes a cycle is determined by the search algorithm, which may
 * employ DFS with the condition s in stack, or a more complicated search
 * algorithm such as the color proviso.
 *
 * Premature check whether L1 and L2 hold, i.e. before ignoring condition is
 * known (the premise of L2).
 * For safety (!PINS_LTL), we limit the proviso to L2. For details see
 * implementation notes in the header.
 */
static inline int
check_L2_proviso (por_context* ctx, search_context_t *s)
{ // all visible selected: satisfies (the conclusion of) L2
    return s->score_visible == bms_count(ctx->visible,VISIBLE);
}

static inline void
beam_enforce_L2 (por_context* ctx)
{
    beam_t             *beam = (beam_t *) ctx->beam_ctx;
    search_context_t   *s1 = beam->search[0];
    while (true) {
        search_context_t   *s = beam->search[0];
        if (check_L2_proviso(ctx, s)) {
            if (s == s1) {
                Debugf ("BEAM %d has all visibles and finished search (previously emitted BEAM %d)\n", s->idx, s1->idx);
                return;
            } else { // if context changed, it should include emitted transitions:
                Debugf ("BEAM %d adding previously emitted from BEAM %d: ", s->idx, s1->idx);
                bool new = select_all(ctx, s, s1->emitted, true);
                Debugf (".\n");
                if (!new) return;
            }
        } else {
            Debugf ("BEAM %d adding all visibles: ", s->idx);
            bool new = select_all (ctx, s, ctx->visible->lists[VISIBLE], true);
            Debugf (".\n");
            HREassert (new);
        }
        beam_sort (ctx);
        beam_search (ctx); // may switch context
        beam_ensure_invisible_and_key (ctx);
    }
}

static inline int
beam_emit (por_context* ctx, int* src, TransitionCB cb, void* uctx)
{
    // selected in winning search context
    beam_t             *beam = (beam_t *) ctx->beam_ctx;
    // if no enabled transitions, return directly
    if (beam->beam_used == 0) return 0;
    search_context_t   *s = beam->search[0];
    int emitted = 0;
    // if the score is larger then the number of enabled transitions, emit all
    if (s->enabled->count >= ctx->enabled_list->count) {
        // return all enabled
        proviso_t provctx = {cb, uctx, 0, 0, 1};
        emitted = emit_all (ctx, s->enabled, &provctx, src);
    } else if (!PINS_LTL && !SAFETY) { // deadlocks are easy:
        proviso_t provctx = {cb, uctx, 0, 0, 1};
        emitted = emit_new (ctx, s->enabled, &provctx, src);
    } else { // otherwise enforce that all por_proviso flags are true
        proviso_t provctx = {cb, uctx, 0, 0, 0};

        search_context_t   *s = beam->search[0];
        provctx.force_proviso_true = !NO_L12 && !NO_V && check_L2_proviso(ctx, s);
        emitted = emit_new (ctx, s->enabled, &provctx, src);

        // emit more if we need to fulfill a liveness / safety proviso
        if ( ( PINS_LTL && provctx.por_proviso_false_cnt != 0) ||
             (!PINS_LTL && provctx.por_proviso_true_cnt  == 0) ) {

            provctx.force_proviso_true = 1;
            Debugf ("BEAM %d may cause ignoring\n", s->idx);
            if (!NO_L12 && !NO_V && ctx->visible_enabled < ctx->enabled_list->count - 1) {
                // enforce L2 (include all visible transitions)
                beam_enforce_L2 (ctx);
                if (beam->search[0]->enabled->count == ctx->enabled_list->count) {
                    emitted += emit_new (ctx, ctx->enabled_list, &provctx, src);
                } else {
                    emitted += emit_new (ctx, s->enabled, &provctx, src);
                }
            } else {
                Debugf ("BEAM %d emitting all\n", s->idx);
                emitted += emit_new (ctx, ctx->enabled_list, &provctx, src);
            }
        }
    }
    return emitted;
}

static int score_cmp (const void *a, const void *b, void *arg) {
    return beam_cmp((search_context_t *)a, (search_context_t *)b);
    (void) arg;
}

/**
 * For each state, this function sets up the current guard values etc
 * This setup is then reused by the analysis function
 */
static void
beam_setup (model_t model, por_context* ctx, int* src)
{
    por_init_transitions (model, ctx, src);
    por_transition_costs (ctx);

    beam_t         *beam = ctx->beam_ctx;
    int             c = RANDOM ? clock() : 0;
    beam->beam_used = ctx->enabled_list->count;
    if (MAX_BEAM != -1 && beam->beam_used > MAX_BEAM)
        beam->beam_used = MAX_BEAM;
    Debugf ("Initializing searches: ");
    for (int i = 0; i < beam->beam_used; i++) {
        int enabled = i;
        if (RANDOM) enabled = (enabled + c) % beam->beam_used;
        int group = ctx->enabled_list->data[enabled];

        search_context_t *search = beam->search[i];
        search->has_key = KEY_NONE;
        search->visible_nds = 0;
        search->visible_nes = 0;
        search->work_disabled->count = 0;
        search->work_enabled->count = 1;
        search->work_enabled->data[0] = group;
        search->enabled->count = 1;
        search->enabled->data[0] = group;
        search->emitted->count = 0;
        search->score_disabled = 0;
        search->initialized = 0;

        int visible = is_visible(ctx, group);
        search->score_visible = visible;
        search->score_vis_en = visible;
        search->idx = i;
        search->group = group;
        Debugf ("BEAM-%d=%d, ", i, group);
    }
    Debugf ("\n");
    if (SAFETY || PINS_LTL) {
        qsortr (beam->search, beam->beam_used, sizeof(void *), score_cmp, ctx);
    }
}

static int
por_beam_search_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    por_context* ctx = ((por_context*)GBgetContext(self));
    beam_setup (self, ctx, src);
    beam_search (ctx);
    beam_ensure_invisible_and_key (ctx);
    return beam_emit (ctx, src, cb, user_context);
}

/**
 * SCC Algorithm
 */

typedef struct scc_state_s {
    int              group;
    int              lowest;
} scc_state_t;

typedef struct scc_context_s {
    search_context_t*search;            // context for each SCC search
    int              index;             // depth index
    int             *group_index;
    dfs_stack_t      tarjan;
    dfs_stack_t      stack;
    ci_list         *scc_list;
    ci_list        **stubborn_list;
    ci_list         *bad_scc;
    int              current_scc;
} scc_context_t;

static int SCC_SCC = -1;
static const int SCC_NEW = 0;

typedef enum {
    SCC_NO,
    SCC_OLD,
    SCC_BAD,
    SCC_CUR
} scc_type_e;

static inline scc_type_e
scc_is_scc (scc_context_t *scc, int group)
{
    int index = scc->group_index[group];
    if (index >= 0)
        return SCC_NO;
    if (scc->current_scc < index && index < 0) { // old SCC
        for (int i = 0; i < scc->bad_scc->count; i++) {
            if (index == scc->bad_scc->data[i]) return SCC_BAD;
        }
        return SCC_OLD;
    }
    return SCC_CUR;
}

static inline int
scc_expand (por_context *ctx, int group)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    ci_list *successors;
    if (ctx->group_status[group] & GS_DISABLED) {
        int         cost;
        int         ns = beam_cheapest_ns (ctx, scc->search, group, &cost);
        successors = ctx->ns[ns];
    } else if (POR_WEAK) {
        successors = ctx->not_left_accords[group];
    } else {
        successors = ctx->not_accords[group];
    }
    int count = 0;
    for (int j = 0; j < successors->count; j++) {
        int next_group = successors->data[j];
        switch (scc_is_scc(scc, next_group)) {
        case SCC_BAD: return -1;
        case SCC_NO: {
            scc_state_t next = { next_group, -1 };
            dfs_stack_push (scc->stack, (int*)&next);
            count++;
        }
        default: break;
        }
    }
    return count;
}

static inline void
reset_ns_scores (por_context* ctx, search_context_t *s, int group)
{
    if (NO_HEUR) return;

    // change the heuristic function according to selected group
    for (int k = 0 ; k < ctx->group2ns[group]->count; k++) {
        int ns = ctx->group2ns[group]->data[k];
        s->nes_score[ns] += ctx->group_score[group] << 1; // Make extra expensive
    }
}

static inline int
scc_ensure_key (por_context* ctx, int root)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    int lowest = scc->group_index[root];

    // collect accepting states
    scc->stubborn_list[1]->count = 0;
    scc_state_t *x;
    for (int i = scc->scc_list->count - 1; i >= 0; i--) {
        int index = scc->scc_list->data[i];
        x = (scc_state_t *)dfs_stack_index (scc->tarjan, index);
        if (x->lowest < lowest)
            break;
        HREassert (!(ctx->group_status[x->group] & GS_DISABLED));
        scc->stubborn_list[1]->data[ scc->stubborn_list[1]->count++ ] = x->group;
    }

    Debug ("Found %d enabled transitions in SCC",scc->stubborn_list[1]->count);

    // if no enabled transitions, return directly
    if (!POR_WEAK || scc->stubborn_list[1]->count == 0 ||
            scc->stubborn_list[1]->count == ctx->enabled_list->count) {
        return 0;
    }

    // mark SCC as lowest
    for (int i = 0; ; i++) {
        x = (scc_state_t *)dfs_stack_peek(scc->tarjan, i);
        scc->group_index[x->group] = lowest; // mark as current SCC
        if (x->group == root) break;
    }

    // Check
    for (int j=0; j < scc->stubborn_list[1]->count; j++) {
        bool allin = true;

        int group = scc->stubborn_list[1]->data[j];
        for (int g = 0; g < ctx->group2guard[group]->count && allin; g++) {
            int nds = ctx->group2guard[group]->data[g] + ctx->nguards;
            for (int k = 0; k < ctx->ns[nds]->count && allin; k++) {
                int ndsgroup = ctx->ns[nds]->data[k];
                allin &= scc->group_index[ndsgroup] == lowest;
            }
        }
        if (allin) {
            Debug ("Found key");
            return 0; // all ok!
        }
    }

    // strongly expand one enabled state:
    int count = 0;
    int group = scc->stubborn_list[1]->data[0];
    for (int j = 0; j < ctx->not_accords[group]->count; j++) {
        int next_group = ctx->not_accords[group]->data[j];
        switch (scc_is_scc(scc, next_group)) {
        case SCC_BAD:
            // mark SCC as SCC
            for (int i = 0; ; i++) {
                x = (scc_state_t *)dfs_stack_peek(scc->tarjan, i);
                scc->group_index[x->group] = SCC_SCC; // mark as current SCC
                if (x->group == root) break;
            }
            SCC_SCC--;
            return -1;
        case SCC_NO:
            if (scc->group_index[next_group] >= lowest) break; // skip
            scc_state_t next = { next_group, -1 };
            dfs_stack_push (scc->stack, (int*)&next);
            count++;
        default: break;
        }
    }
    return count;
}

static inline bool
scc_root (por_context* ctx, int root)
{
    scc_state_t *x;
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;

    int count = 0;
    do {x = (scc_state_t *)dfs_stack_pop(scc->tarjan);
        reset_ns_scores (ctx, scc->search, x->group);
        scc->group_index[x->group] = SCC_SCC; // mark as current SCC
        count++;
    } while (x->group != root);

    // stubborn list was set by scc_ensure_key
    bool found_enabled = scc->stubborn_list[1]->count > 0;
    if (found_enabled) {
        //remove accepting
        scc->scc_list->count -= scc->stubborn_list[1]->count;

        Debug ("Found stubborn SCC of size %d,%d!", scc->stubborn_list[1]->count, count);
        int count = scc->stubborn_list[0]->count;
        if (scc->stubborn_list[1]->count < count) {
            swap (scc->stubborn_list[0], scc->stubborn_list[1]);
            scc->bad_scc->data[scc->bad_scc->count++] = SCC_SCC; // remember stubborn SCC
            Debug ("Update stubborn set %d --> %d!", count < INT32_MAX ? count : -1, scc->stubborn_list[0]->count);
        }
    }
    SCC_SCC--; // update to next SCC layer
    return found_enabled;
}

static void
scc_search (por_context* ctx)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    scc_state_t *state, *pred;
    scc->scc_list->count = 0;

    while (scc->stubborn_list[0]->count != ctx->enabled_list->count) {
        state = (scc_state_t *)dfs_stack_top(scc->stack);
        if (state != NULL) {
            switch (scc_is_scc(scc, state->group)) {
            case SCC_BAD: return;
            case SCC_OLD:
            case SCC_CUR:
                dfs_stack_pop (scc->stack);
                continue;
            case SCC_NO: break;
            }

            if (scc->group_index[state->group] == SCC_NEW) {
                HREassert (state->lowest == -1);
                // assign index
                scc->group_index[state->group] = ++scc->index;
                state->lowest = scc->index;
                // add to tarjan stack
                if (!(ctx->group_status[state->group] & GS_DISABLED))
                    scc->scc_list->data[ scc->scc_list->count++ ] = dfs_stack_size(scc->tarjan);
                dfs_stack_push (scc->tarjan, &state->group);

                // push successors
                dfs_stack_enter (scc->stack);
                int count = scc_expand(ctx, state->group);
                if (count == -1) break; // bad SCC encountered
            } else if (dfs_stack_nframes(scc->stack) > 0){
                pred = (scc_state_t *)dfs_stack_peek_top (scc->stack, 1);
                if (scc->group_index[state->group] < pred->lowest) {
                    pred->lowest = scc->group_index[state->group];
                }
                dfs_stack_pop (scc->stack);
            }
        } else {
            state = (scc_state_t *)dfs_stack_peek_top (scc->stack, 1);
            if (scc->group_index[state->group] == state->lowest) {
                int to_explore = scc_ensure_key(ctx, state->group);
                if (to_explore == -1) { // bad state
                    dfs_stack_leave (scc->stack);
                    dfs_stack_pop (scc->stack);
                    Debug ("Failed adding key");
                    break;
                } else if (to_explore > 0) {
                    Debug ("Continuing search");
                    continue;
                } else {
                    Debug ("Key added nothing or not needed");
                    dfs_stack_leave (scc->stack);
                }
            }

            dfs_stack_leave (scc->stack);
            state = (scc_state_t *)dfs_stack_top (scc->stack);

            update_ns_scores (ctx, scc->search, state->group); // remove from NS scores

            // detected an SCC
            if (scc->group_index[state->group] == state->lowest) {
                bool found = scc_root (ctx, state->group);
                if (found) break;
            } else if (dfs_stack_nframes(scc->stack) > 0) {
                // (after recursive return call) update index
                pred = (scc_state_t *)dfs_stack_peek_top (scc->stack, 1);
                if (state->lowest < pred->lowest) {
                    pred->lowest = state->lowest;
                }
            }
        }
    }
    HREassert (scc->stubborn_list[0]->count > 0 &&
               scc->stubborn_list[0]->count != INT32_MAX);
}

static void
empty_stack (scc_context_t *scc, dfs_stack_t stack)
{
    while (dfs_stack_size(stack) != 0) {
        scc_state_t *s = (scc_state_t *)dfs_stack_pop (stack);
        if (s == NULL) {
            dfs_stack_leave (stack);
        } else {
            scc->group_index[s->group] = SCC_NEW;
        }
    }
}

static void
scc_analyze (por_context* ctx)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    scc->stubborn_list[0]->count = INT32_MAX;

    Debug ("%s", "");
    scc->bad_scc->count = 0; // not SCC yet
    SCC_SCC = -1;
    for (int j=0; j < ctx->enabled_list->count; j++) {
        int group = ctx->enabled_list->data[j];
        if (scc->group_index[group] == SCC_NEW) {
            Debug ("SCC search from %d", group);
            scc->index = 0;
            // SCC from current search will be leq the current value of SCC_SCC
            scc->current_scc = SCC_SCC;
            scc_state_t next = { group, -1 };
            dfs_stack_push (scc->stack, (int*)&next);
            scc_search (ctx);
            empty_stack (scc, scc->stack);  // clear stacks (and indices)
            empty_stack (scc, scc->tarjan); //    for next run
            int count = scc->stubborn_list[0]->count;
            if (alg == POR_SCC1 || count == 1 || count == ctx->enabled_list->count) {
                break; // early exit (one iteration or can't do better)
            }
        }
    }
}

static int
scc_emit (por_context *ctx, int *src, TransitionCB cb, void *uctx)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;

    if (ctx->enabled_list->count == scc->stubborn_list[0]->count) {
        // return all enabled
        proviso_t provctx = {cb, uctx, 0, 0, 1};
        return GBgetTransitionsAll(ctx->parent, src, hook_cb, &provctx);
    } else {
        proviso_t provctx = {cb, uctx, 0, 0, 0};
        search_context_t *s = scc->search;
        provctx.force_proviso_true = !NO_L12 && check_L2_proviso(ctx,s); // TODO

        int c = 0;
        for (int z = 0; z < scc->stubborn_list[0]->count; z++) {
            int i = scc->stubborn_list[0]->data[z];
            c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, &provctx);
        }
        return c;
    }
}

/**
 * For each state, this function sets up the current guard values etc
 * This setup is then reused by the analysis function
 */
static void
scc_setup (model_t model, por_context *ctx, int *src)
{
    por_init_transitions (model, ctx, src);

    por_transition_costs (ctx);

    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    memcpy(ctx->nes_score, ctx->nes_score, NS_SIZE(ctx) * sizeof(int));
    memset(scc->group_index, SCC_NEW, ctx->ngroups * sizeof(int));
}

static int
por_scc_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    por_context* ctx = ((por_context*)GBgetContext(self));
    scc_setup (self, ctx, src);
    if (ctx->enabled_list->count == 0) return 0;
    scc_analyze (ctx);
    int emitted = scc_emit (ctx, src, cb, user_context);
    return emitted;
}

/**
 * DELETION algorithm.
 */

/**
 * Sets in deletion algorithm
 * Some are maintained only as stack or as set with cardinality counter
 * A "stack set" can both be iterated over and performed inclusion tests on,
 * however it does not support element removal (as it messes up the stack).
 */
typedef enum {
    DEL_N,  // set
    DEL_K,  // count set (stack content may be corrupted)
    DEL_E,  // set (EMITTED)
    DEL_Z,  // stack set
    DEL_R,  // set
    DEL_VD,
    DEL_VE,
    DEL_COUNT
} del_t;

typedef struct del_ctx_s {
    bms_t              *del;
    ci_list            *Kprime;
    ci_list            *Nprime;
    ci_list            *Dprime;
    int                *nes_score;
    int                *group_score;
    int                 invisible_enabled;
    bool                has_invisible;
    int                 del_nes;
    int                 del_nds;

} del_ctx_t;

static del_ctx_t *
deletion_create (por_context* ctx)
{
    del_ctx_t *delctx = RTmalloc (sizeof(del_ctx_t));
    delctx->del = bms_create (ctx->ngroups, DEL_COUNT);
    delctx->Kprime = ci_create(ctx->ngroups);
    delctx->Nprime = ci_create(ctx->ngroups);
    delctx->Dprime = ci_create(ctx->ngroups);
    delctx->group_score  = RTmallocZero(ctx->ngroups * sizeof(int));
    delctx->nes_score    = RTmallocZero(NS_SIZE(ctx) * sizeof(int));
    return delctx;
}

static inline bool
del_enabled (por_context* ctx, int u)
{
    return ctx->group_status[u] == GS_ENABLED;
}

static void
deletion_setup (model_t model, por_context* ctx, int* src, bool reset)
{
    del_ctx_t       *delctx = (del_ctx_t *) ctx->del_ctx;
    bms_t           *del = delctx->del;
    por_init_transitions (model, ctx, src);
    if (ctx->enabled_list->count == 0) return;

    // use -1 for deactivated NESs
    for (int ns = 0; ns < ctx->nguards; ns++) { // guard should be false
        delctx->nes_score[ns] = 0 - (ctx->label_status[ns] != 0); // 0 for false!
    }
    for (int ns = ctx->nguards; ns < NS_SIZE(ctx); ns++) { // guard should be true
        delctx->nes_score[ns] = 0 - (ctx->label_status[ns - ctx->nguards] == 0); // 0 for true!
    }

    // initially all active ns's are in:
    for (int t = 0; t < ctx->ngroups; t++) {
        delctx->group_score[t] = 0;
        if (del_enabled(ctx,t)) continue;
        for (int i = 0; i < ctx->group_has[t]->count; i++) {
            int ns = ctx->group_has[t]->data[i];
            delctx->group_score[t] += delctx->nes_score[ns] >= 0;
        }
    }

    // K = {}; Z := {}; KP := {}; TP := {}; R := {}
    // N := T
    bms_clear_lists (del);
    if (reset) {
        bms_set_all (del, DEL_N);
    } else {
        bms_and_or_all (del, DEL_R, DEL_E, DEL_N); // save emitted and revert
    }
    ci_clear (delctx->Kprime);
    ci_clear (delctx->Nprime);
    ci_clear (delctx->Dprime);

    //  K := A
    delctx->invisible_enabled = 0;
    for (int i = 0; i < ctx->enabled_list->count; i++) {
        int group = ctx->enabled_list->data[i];
        bms_push_new (del, DEL_K, group);

        bool d = bms_has(ctx->visible, NO_DYN_VIS ? VISIBLE : VISIBLE_NDS, group);
        bms_push_if (del, DEL_VD, group, d);
        bool e = NO_DYN_VIS && bms_has(ctx->visible, VISIBLE_NES, group);
        bms_push_if (del, DEL_VE, group, e);
        delctx->invisible_enabled += !is_visible(ctx, group);
    }
    delctx->has_invisible = delctx->invisible_enabled != 0;
    Debug ("Deletion init |en| = %d \t|R| = %d",
             ctx->enabled_list->count, bms_count(del, DEL_R));
}

/**
 * del_nes and del_nds indicate whether the visibles have already been deleted
 */
static inline bool
deletion_delete (por_context* ctx, int *del_nes, int *del_nds)
{
    del_ctx_t       *delctx = (del_ctx_t *) ctx->del_ctx;
    bms_t           *del = delctx->del;

    // search the deletion space:
    while (bms_count(del, DEL_Z) != 0 && bms_count(del, DEL_K) != 0) {
        int z = bms_pop (del, DEL_Z);
        Debug ("Checking z = %d", z);

        if (bms_has(del,DEL_R,z)) return true;

        // First, the enabled transitions x that are still stubborn and
        // belong to DNA_u, need to be removed from key stubborn and put to Z.
        for (int i = 0; i < ctx->not_accords[z]->count && bms_count(del, DEL_K) > 0; i++) {
            int x = ctx->not_accords[z]->data[i];
            if (bms_has(del,DEL_K,x)) {
                if (!bms_has(del,DEL_N,x)) {
                    if (bms_has(del,DEL_R,x)) return true;
                    bms_push_new (del, DEL_Z, x);
                }
                if (bms_rem(del, DEL_K, x)) ci_add (delctx->Kprime, x);
                delctx->invisible_enabled -= !is_visible(ctx, x);
                if (delctx->has_invisible && delctx->invisible_enabled == 0 && !NO_V) {
                    return true;
                }
            }
        }
        if (bms_count(del, DEL_K) == 0) return true;

        // Second, the enabled transitions x that are still stubborn and
        // belong to DNB_u need to be removed from other stubborn and put to Z.
        for (int i = 0; i < ctx->not_left_accordsn[z]->count; i++) {
            int x = ctx->not_left_accordsn[z]->data[i];
            if (del_enabled(ctx,x) && bms_has(del,DEL_N,x)) {
                if (!bms_has(del,DEL_K,x)) {
                    if (bms_has(del,DEL_R,x)) return true;
                    bms_push_new (del, DEL_Z, x);
                }
                if (bms_rem(del, DEL_N, x)) ci_add (delctx->Nprime, x);
            }
        }

        // Third, if a visible is deleted, then remove all enabled visible
        if ((SAFETY || PINS_LTL) && (NO_V ? del_enabled(ctx,z) : is_visible(ctx,z))) {
            int newNDS = ctx->visible_nes->set[z] ^ *del_nds; // z in NES ==> Ts \ NDS(z)
            if (NO_DYN_VIS || newNDS) {
                for (int i = 0; i < del->lists[DEL_VD]->count; i++) {
                    int x = del->lists[DEL_VD]->data[i];
                    if (!NO_DYN_VIS && !(ctx->visible_nds->set[x] & newNDS))
                        continue;
                    if (bms_has(del, DEL_N, x) || bms_has(del, DEL_K, x)) {
                        if (bms_rem(del, DEL_N, x)) ci_add (delctx->Nprime, x);
                        if (bms_rem(del, DEL_K, x)) {
                            ci_add (delctx->Kprime, x);
                            if (bms_count(del, DEL_K) == 0) return true;
                            delctx->invisible_enabled -= !is_visible(ctx, x);
                            if (delctx->has_invisible && delctx->invisible_enabled == 0) return true;
                        }
                        if (bms_has(del,DEL_R,x)) return true; // enabled
                        bms_push_new (del, DEL_Z, x);
                    }
                }
                if (NO_DYN_VIS) {
                    *del_nds = *del_nes = (1 << ctx->visible_nds->types) - 1; // all
                } else {
                    *del_nds |= newNDS;
                }
            }
            int newNES = ctx->visible_nds->set[z] ^ *del_nes; // z in NDS ==> Ts \ NES(z)
            if (!NO_DYN_VIS && newNES) {
                for (int i = 0; i < del->lists[DEL_VE]->count; i++) {
                    int x = del->lists[DEL_VE]->data[i];
                    if (!NO_DYN_VIS && !(ctx->visible_nes->set[x] & newNES))
                        continue;
                    if (bms_has(del, DEL_N, x) || bms_has(del, DEL_K, x)) {
                        if (bms_rem(del, DEL_N, x)) ci_add (delctx->Nprime, x);
                        if (bms_rem(del, DEL_K, x)) {
                            ci_add (delctx->Kprime, x);
                            if (bms_count(del, DEL_K) == 0) return true;
                            delctx->invisible_enabled -= !is_visible(ctx, x);
                            if (delctx->has_invisible && delctx->invisible_enabled == 0) return true;
                        }
                        if (bms_has(del,DEL_R,x)) return true; // enabled
                        bms_push_new (del, DEL_Z, x);
                    }
                }
                *del_nes |= newNES;
            }
        }

        ci_add (delctx->Dprime, z);
        // Fourth, the disabled transitions x, whose NES was stubborn
        // before removal of z, need to be put to Z.
        bool inR = false;
        for (int i = 0; i < ctx->group2ns[z]->count; i++) {
            int ns = ctx->group2ns[z]->data[i];
            if (delctx->nes_score[ns] == -1) continue; // -1 is inactive!

            // not the first transition removed from NES?
            int score = delctx->nes_score[ns]++;
            if (score != 0) continue;

            for (int i = 0; i < ctx->group_hasn[ns]->count; i++) {
                int x = ctx->group_hasn[ns]->data[i];
                if (!del_enabled(ctx,x) ||
                        (POR_WEAK==WEAK_VALMARI && !bms_has(del,DEL_K,x))) {
                    delctx->group_score[x]--;
                    HREassert (delctx->group_score[x] >= 0, "Wrong counting!");
                    if (delctx->group_score[x] == 0 && bms_has(del,DEL_N,x)) {
                        bms_push_new (del, DEL_Z, x);
                        HREassert (!bms_has(del, DEL_K, x)); // x is disabled
                        if (bms_rem(del, DEL_N, x)) ci_add (delctx->Nprime, x);
                        inR |= bms_has(del, DEL_R, x);
                    }
                }
            }
        }
        if (inR) return true; // revert
    }
    return bms_count(del, DEL_K) == 0;
}

static inline void
deletion_analyze (por_context *ctx)
{
    if (ctx->enabled_list->count == 0) return;
    del_ctx_t          *delctx = (del_ctx_t *) ctx->del_ctx;
    bms_t              *del = delctx->del;
    int                 del_nes = false;
    int                 del_nds = false;

    for (int i = 0; i < ctx->enabled_list->count && bms_count(del, DEL_K) > 1; i++) {
        int v = ctx->enabled_list->data[i];
        if (bms_has(del, DEL_R, v)) continue;

        if (bms_rem(del, DEL_K, v)) ci_add (delctx->Kprime, v);
        if (bms_rem(del, DEL_N, v)) ci_add (delctx->Nprime, v);
        bms_push_new (del, DEL_Z, v);

        Debug ("Deletion start from v = %d: |E| = %d \t|K| = %d", v, ctx->enabled_list->count, bms_count(del, DEL_K));

        int             del_nes_old = del_nes;
        int             del_nds_old = del_nds;
        bool revert = deletion_delete (ctx, &del_nes, &del_nds);

        while (bms_count(del, DEL_Z) != 0) bms_pop (del, DEL_Z);

        // Reverting deletions if necessary
        if (revert) {
            Debug ("Deletion rollback: |T'| = %d \t|K'| = %d \t|D'| = %d",
                     ci_count(delctx->Nprime), ci_count(delctx->Kprime), ci_count(delctx->Dprime));
            bms_add (del, DEL_R, v); // fail transition!
            while (ci_count(delctx->Kprime) != 0) {
                int x = ci_pop (delctx->Kprime);
                bool seen = bms_push_new (del, DEL_K, x);
                delctx->invisible_enabled += !is_visible(ctx, x);
                HREassert (seen, "DEL_K messed up");
            }
            while (ci_count(delctx->Nprime) != 0) {
                int x = ci_pop (delctx->Nprime);
                del->set[x] = del->set[x] | (1<<DEL_N);
            }
            while (ci_count(delctx->Dprime) != 0) {
                int x = ci_pop (delctx->Dprime);
                for (int i = 0; i < ctx->group2ns[x]->count; i++) {
                    int ns = ctx->group2ns[x]->data[i];
                    delctx->nes_score[ns] -= (delctx->nes_score[ns] >= 0);
                    if (delctx->nes_score[ns] != 0) continue; // NES not readded

                    for (int i = 0; i < ctx->group_hasn[ns]->count; i++) {
                        int x = ctx->group_hasn[ns]->data[i];
                        delctx->group_score[x] += !del_enabled(ctx,x);
                    }
                }
            }
            del_nes &= del_nes_old; // remain only true if successfully removed before
            del_nds &= del_nds_old; // remain only true if successfully removed before
        } else {
            ci_clear (delctx->Kprime);
            ci_clear (delctx->Nprime);
            ci_clear (delctx->Dprime);
        }
    }

    delctx->del_nes = del_nes;
    delctx->del_nds = del_nds;
}

static inline int
deletion_emit_new (por_context *ctx, proviso_t *provctx, int* src)
{
    del_ctx_t       *delctx = (del_ctx_t *) ctx->del_ctx;
    bms_t           *del = delctx->del;
    int c = 0;
    for (int z = 0; z < ctx->enabled_list->count; z++) {
        int i = ctx->enabled_list->data[z];
        if (por_is_stubborn(ctx,i) && !bms_has(del,DEL_E,i)) {
            del->set[i] |= 1<<DEL_E | 1<<DEL_R;
            c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, provctx);
        }
    }
    return c;
}

static inline bool
del_all_stubborn (por_context *ctx, ci_list *list)
{
    for (int i = 0; i < list->count; i++)
        if (!por_is_stubborn(ctx, list->data[i])) return false;
    return true;
}

static inline int
deletion_emit (model_t model, por_context *ctx, int *src, TransitionCB cb,
               void *uctx)
{
    del_ctx_t          *delctx = (del_ctx_t *) ctx->del_ctx;
    bms_t              *del = delctx->del;
    proviso_t provctx = {cb, uctx, 0, 0, 0};

    if (PINS_LTL || SAFETY) {
        if (NO_L12) {
            provctx.force_proviso_true = del_all_stubborn(ctx,ctx->enabled_list);
        } else { // Deletion guarantees that I holds, but does V hold?
            provctx.force_proviso_true = !delctx->del_nds && !delctx->del_nes;
            HRE_ASSERT (provctx.force_proviso_true ==
                        del_all_stubborn(ctx,ctx->visible->lists[VISIBLE]));
        }
    }

    int emitted = deletion_emit_new (ctx, &provctx, src);

    // emit more if we need to fulfill a liveness / safety proviso
    if ( ( PINS_LTL && provctx.por_proviso_false_cnt != 0) ||
         (!PINS_LTL && provctx.por_proviso_true_cnt  == 0) ) {
        if (NO_L12) {
            for (int i = 0; i < ctx->enabled_list->count; i++) {
                int x = ctx->enabled_list->data[i];
                del->set[x] |= (1 << DEL_N);
            }
            emitted += deletion_emit_new (ctx, &provctx, src);
        } else {
            for (int i = 0; i < del->lists[DEL_VD]->count; i++) {
                int x = del->lists[DEL_VD]->data[i];
                del->set[x] |= 1<<DEL_R;
            }
            for (int i = 0; i < del->lists[DEL_VE]->count && !NO_DYN_VIS; i++) {
                int x = del->lists[DEL_VE]->data[i];
                del->set[x] |= 1<<DEL_R;
            }
            deletion_setup (model, ctx, src, false);
            deletion_analyze (ctx);

            emitted += deletion_emit_new (ctx, &provctx, src);
        }
    }

    return emitted;
}

static int
por_deletion_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    por_context* ctx = ((por_context*)GBgetContext(self));
    deletion_setup (self, ctx, src, true);
    deletion_analyze (ctx);
    return deletion_emit (self, ctx, src, cb, user_context);
}

static void
list_invert (ci_list *list)
{
    for (int i = 0; i < list->count / 2; i++) {
        swap (list->data[i], list->data[list->count - i - 1]);
    }
}

static search_context_t *
create_search_context (por_context *ctx)
{
    search_context_t *search;
    search = RTmallocZero(sizeof(search_context_t));
    search->emit_status = RTmallocZero(sizeof(char[ctx->ngroups]));
    search->work_enabled = ci_create(ctx->ngroups);
    search->work_disabled = ci_create(ctx->ngroups);
    search->enabled = ci_create(ctx->ngroups);
    search->emitted = ci_create(ctx->ngroups);
    search->nes_score = RTmallocZero(sizeof(int[NS_SIZE(ctx)]));
    return search;
}


static scc_context_t *
create_scc_ctx (por_context* ctx)
{
    scc_context_t *scc = RTmallocZero (sizeof(scc_context_t));
    scc->group_index = RTmallocZero ((ctx->ngroups) * sizeof(int));
    scc->scc_list = RTmallocZero ((ctx->ngroups + 1) * sizeof(int));
    scc->stubborn_list = RTmallocZero (sizeof(ci_list *[2]));
    scc->stubborn_list[0] = ci_create (ctx->ngroups);
    scc->stubborn_list[1] = ci_create (ctx->ngroups);
    scc->bad_scc = ci_create (ctx->ngroups);
    scc->stack = dfs_stack_create (2); // only integers for groups
    scc->tarjan = dfs_stack_create (1); // only integers for group
    scc->search = create_search_context (ctx);
    return scc;
}

/**
 * Function used to setup the beam search
 */
static beam_t *
create_beam_context (por_context *ctx)
{
    beam_t         *beam = RTmallocZero(sizeof(beam_t));
    beam->search = RTmallocZero(sizeof(search_context_t *[ctx->ngroups]));
    for (int i = 0 ; i < ctx->ngroups; i++) {
        beam->search[i] = create_search_context (ctx);
    }
    return beam;
}

/**
 * Ample-set search
 */

typedef struct process_s {
    int                 first_group;
    int                 last_group;
    ci_list            *enabled;
} process_t;

typedef struct ample_ctx_s {
    size_t              num_procs;
    process_t          *procs;
} ample_ctx_t;

static inline size_t
ample_emit (por_context *ctx, ci_list *list, int *src, proviso_t *provctx)
{
    size_t emitted = 0;
    for (int j = 0; j < list->count; j++) {
        int group = list->data[j];
        emitted += GBgetTransitionsLong (ctx->parent, group, src, hook_cb, provctx);
    }
    return emitted;
}

static int
ample_one (model_t self, int *src, TransitionCB cb, void *uctx)
{
    por_context *ctx = ((por_context *)GBgetContext(self));

    ample_ctx_t *ample = (ample_ctx_t *)ctx->ample_ctx;
    por_init_transitions (ctx->parent, ctx, src);

    size_t              p = 0;
    process_t          *proc = &ample->procs[p];
    ci_clear (proc->enabled);
    for (int i = 0; i < ctx->enabled_list->count; i++) {
        int             group = ctx->enabled_list->data[i];
        if (proc->last_group < group) {
            proc = &ample->procs[++p];
            ci_clear (proc->enabled);
        }
        ci_add (proc->enabled, group);
    }
    HREassert (p <= ample->num_procs);

    proviso_t provctx = {cb, uctx, 0, 0, 0};
    for (size_t p = 0; p < ample->num_procs; p++) {
        process_t          *proc = &ample->procs[p];

        bool                emit_p = true;
        for (int j = 0; j < proc->enabled->count && emit_p; j++) {
            int group = proc->enabled->data[j];

            if (ctx->visible->set[group])
                emit_p = false;
            for (int j = 0; j < ctx->not_accords[group]->count && emit_p; j++) {
                int dep = ctx->not_accords[group]->data[j];
                if (dep < proc->first_group || dep > proc->last_group ) {
                    emit_p = false;
                }
            }
        }

        if (emit_p) {
            size_t              emitted = 0;
            emitted += ample_emit (ctx, proc->enabled, src, &provctx);

            // emit more if ignoring proviso is violated
            if ( ( PINS_LTL && provctx.por_proviso_false_cnt != 0) ||
                 (!PINS_LTL && provctx.por_proviso_true_cnt  == 0) ) {
                provctx.force_proviso_true = 1;
                for (size_t o = 0; o < ample->num_procs; o++) {
                    process_t          *other = &ample->procs[o];
                    if (other == proc) continue; // already emitted
                    emitted += ample_emit (ctx, other->enabled, src, &provctx);
                }
            }
            return emitted;
        }
    }

    provctx.force_proviso_true = 1;
    return ample_emit (ctx, ctx->enabled_list, src, &provctx);
}


static ample_ctx_t *
create_ample_ctx (por_context *ctx)
{
    ample_ctx_t *ample = RTmalloc(sizeof(ample_ctx_t));
    ample->procs = RTmalloc(sizeof(process_t[ctx->ngroups])); // enough

    matrix_t group_deps;
    matrix_t *deps = GBgetDMInfo (ctx->parent);
    dm_create(&group_deps, ctx->ngroups, ctx->ngroups);
    for (int i = 0; i < ctx->ngroups; i++) {
        for (int j = 0; j < ctx->nslots; j++) {
            if (dm_is_set(deps, i, j)) {
                for (int h = 0; h <= i; h++) {
                    if (dm_is_set(deps, h, j)) {
                        dm_set (&group_deps, i, h);
                        dm_set (&group_deps, h, i);
                    }
                }
            }
        }
    }
    if (log_active(infoLong) && HREme(HREglobal()) == 0) dm_print (stdout, &group_deps);

    // find processes:
    size_t pbegin = 0;
    ample->num_procs = 0;
    for (int i = pbegin; i <= ctx->ngroups; i++) {
        size_t num_set = 0;
        if (i != ctx->ngroups) {
            HREassert (dm_is_set(&group_deps, i, i), "Cannot detect processes for ample set POR in incomplete gorup/slot dependency matrix (diagonal not set in derived group/group relation).");
            for (int j = pbegin; j <= i; j++) {
                num_set += dm_is_set(&group_deps, i, j);
            }
        }
        if (num_set < (i - pbegin + 1) / 2) { // heuristic
            // end of process
            size_t pend = i - 1;
            ample->procs[ample->num_procs].first_group = pbegin;
            ample->procs[ample->num_procs].last_group = pend;
            ample->num_procs++;
            Print1 (info, "Ample: Process %zu, starts from group %zu and ends with group %zu.", ample->num_procs, pbegin, pend);

            // remove dependencies with other groups to avoid confusion in detection
            for (size_t j = pbegin; j <= pend; j++) {
                for (int h = 0; h < ctx->ngroups; h++) {
                    dm_unset (&group_deps, j, h);
                    dm_unset (&group_deps, h, j);
                }
            }
            pbegin = pend + 1;
        }
    }

    for (size_t i = 0; i < ample->num_procs; i++) {
        process_t          *proc = &ample->procs[i];
        proc->enabled = ci_create (proc->last_group - proc->first_group + 1);
    }

    if (!dm_is_empty(&group_deps) && HREme(HREglobal()) == 0) {
        dm_print (stdout, &group_deps);
        Abort ("Ample set heuristic for process identification failed, please verify the results with --labels.")
    }

    return ample;
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

static inline bool
guard_of (por_context *ctx, int i, matrix_t *m, int j)
{
    for (int g = 0; g < ctx->group2guard[i]->count; g++) {
        int guard = ctx->group2guard[i]->data[g];
        if (dm_is_set (m, guard, j)) {
            return true;
        }
    }
    return false;
}

static inline bool
all_guards (por_context *ctx, int i, matrix_t *m, int j)
{
    for (int g = 0; g < ctx->group2guard[i]->count; g++) {
        int guard = ctx->group2guard[i]->data[g];
        if (!dm_is_set (m, guard, j)) {
            return false;
        }
    }
    return true;
}

/**
 * Setup the partial order reduction layer.
 * Reads available dependencies, i.e. read/writes dependencies when no
 * NDA/NES/NDS is provided.
 */
model_t
GBaddPOR (model_t model)
{
    if (GBgetAcceptingStateLabelIndex(model) != -1) {
        Print1  (info, "POR layer: model may be a buchi automaton.");
        Print1  (info, "POR layer: use LTSmin's own LTL layer (--ltl) for correct POR.");
    }

    // check support for guards, fail without
    if (!GBhasGuardsInfo(model)) {
        Print1 (info, "Frontend doesn't have guards. Ignoring --por.");
        return model;
    }
    if (NO_HEUR_BEAM) NO_HEUR = MAX_BEAM = 1;
    if (!RANDOM && (NO_HEUR || MAX_BEAM != -1)) {
        if (__sync_bool_compare_and_swap(&RANDOM, 0, 1)) { // static variable
            Warning (info, "Using random selection instead of heuristics / multiple Beam searches.");
        }
    }

    // do the setup
    model_t             pormodel = GBcreateBase ();

    por_context *ctx = RTmalloc (sizeof *ctx);
    ctx->parent = model;
    ctx->visible = NULL; // initialized on demand

    sl_group_t *guardLabels = GBgetStateLabelGroupInfo (model, GB_SL_GUARDS);
    sl_group_t* sl_guards = GBgetStateLabelGroupInfo(model, GB_SL_ALL);
    ctx->nguards = guardLabels->count;
    ctx->nlabels = pins_get_state_label_count(model);
    ctx->ngroups = pins_get_group_count(model);
    ctx->nslots = pins_get_state_variable_count (model);
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
    NO_MC |= label_mce_matrix == NULL;
    if (NO_MC && !NO_MCNDS) {
        if (__sync_bool_compare_and_swap(&NO_MCNDS, 0, 1)) { // static variable
            Warning (info, "No maybe-coenabled matrix found. Turning off NESs from NDS+MC.");
        }
    }

    if (!NO_MC) {
        HREassert (dm_ncols(label_mce_matrix) >= ctx->nguards &&
                   dm_nrows(label_mce_matrix) >= ctx->nguards);
        dm_create(&ctx->gnce_matrix, ctx->nguards, ctx->ngroups);
        dm_create(&ctx->nce, ctx->ngroups, ctx->ngroups);
        for (int g = 0; g < ctx->nguards; g++) {
            // iterate over all guards
            for (int gg = 0; gg < ctx->nguards; gg++) {
                // find all guards that may not be co-enabled
                if (dm_is_set(label_mce_matrix, g, gg)) continue;

                // gg may not be co-enabled with g, find all
                // transition groups in which it is used
                for (int tt = 0; tt < ctx->guard2group[gg]->count; tt++) {
                    dm_set(&ctx->gnce_matrix, g, ctx->guard2group[gg]->data[tt]);

                    for (int t = 0; t < ctx->guard2group[g]->count; t++) {
                        dm_set(&ctx->nce, ctx->guard2group[g]->data[t],
                                            ctx->guard2group[gg]->data[tt]);
                    }
                }
            }
        }
        ctx->guard_nce             = (ci_list **) dm_rows_to_idx_table(&ctx->gnce_matrix);
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
                        (dm_is_set( p_dm, i, k) && dm_is_set( p_dm_w, j, k)) ) {
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

        matrix_t not_left_accords;
        dm_create(&not_left_accords, ctx->ngroups, ctx->ngroups);
        for (int i = 0; i < ctx->ngroups; i++) {
            for (int j = 0; j < ctx->ngroups; j++) {
                if (i == j) continue;

                // j may disable i (OK)
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
                        dm_set( &not_left_accords, i, j );
                        continue;
                    }
                }

                if (NO_COMMUTES) {
                    // !DNA (OK)
                    if (!dm_is_set(&ctx->not_accords_with, i, j)) continue;
                } else {
                    // i may disable j (NOK)
                    if (dm_is_set(&nds, j, i)) {
                        dm_set( &not_left_accords, i, j );
                        continue;
                    }
                    // i may enable j (NOK)
                    if (guard_of(ctx, j, &ctx->label_nes_matrix, i)) {
                        dm_set( &not_left_accords, i, j );
                        continue;
                    }
                    // i,j commute (OK)
                    if ( dm_is_set(commutes, i , j) ) continue;
                }

                // is even dependent? Front-end might miss it.
                for (int k = 0; k < ctx->nslots; k++) {
                    if ((dm_is_set( p_dm_w, i, k) && dm_is_set( p_dm, j, k)) ||
                        (dm_is_set( p_dm, i, k) && dm_is_set( p_dm_w, j, k)) ) {
                        dm_set( &not_left_accords, i, j );
                        break;
                    }
                }
            }
        }
        ctx->not_left_accords = (ci_list **) dm_rows_to_idx_table(&not_left_accords);
        ctx->not_left_accordsn= (ci_list **) dm_cols_to_idx_table(&not_left_accords);

        matrix_t dna_diff;
        dm_create(&dna_diff, ctx->ngroups, ctx->ngroups);
        for (int i = 0; i < ctx->ngroups; i++) {
            for (int j = 0; j < ctx->ngroups; j++) {
                if ( dm_is_set(&ctx->not_accords_with, i , j) &&
                    !dm_is_set(&not_left_accords, i , j) ) {
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
    switch (alg) {
    case POR_SCC:
    case POR_SCC1: GBsetNextStateAll   (pormodel, por_scc_all);  break;
    case POR_AMPLE:GBsetNextStateAll   (pormodel, ample_one); break;
    case POR_AMPLE1:GBsetNextStateAll   (pormodel, ample_one); break;
    case POR_HEUR: GBsetNextStateAll   (pormodel, por_beam_search_all); break;
    case POR_DEL:  GBsetNextStateAll   (pormodel, por_deletion_all);    break;
    default: Abort ("Unknown POR algorithm: '%s'", key_search(por_algorithm, alg));
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

    ctx->beam_ctx  = create_beam_context (ctx);
    ctx->scc_ctx = create_scc_ctx (ctx);
    ctx->del_ctx = deletion_create (ctx);
    if (alg == POR_AMPLE || alg == POR_AMPLE1) {
        ctx->ample_ctx = create_ample_ctx (ctx);
    }

    return pormodel;
}

bool
por_is_stubborn (por_context *ctx, int group)
{
    switch (alg) {
        case POR_SCC:
        case POR_SCC1: Abort ("Unimplemented SCC + check");
        case POR_HEUR: {
            beam_t             *beam = (beam_t *) ctx->beam_ctx;
            search_context_t   *s = beam->search[0];
            return s->enabled->count >= ctx->enabled_list->count ||
                  (s->emit_status[group] & ES_SELECTED);
        }
        case POR_DEL: {
            del_ctx_t *del_ctx = (del_ctx_t *)ctx->del_ctx;
            bms_t* del = del_ctx->del;
            return bms_has(del, DEL_N, group) || bms_has(del, DEL_K, group);
        }
    default: Abort ("Unknown POR algorithm: '%s'", key_search(por_algorithm, alg));
    }
}
