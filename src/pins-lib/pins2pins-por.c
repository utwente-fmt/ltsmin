#include <hre/config.h>

#include <limits.h>
#include <stdlib.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins2pins-por.h>
#include <pins-lib/pins-util.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>

int POR_WEAK = 0; //extern

static int NO_HEUR = 0;
static int NO_DNA = 0;
static int NO_NES = 0;
static int NO_NDS = 0;
static int NO_MDS = 0;
static int NO_MC = 0;
static int NO_DYNLAB = 0;
static int NO_V = 0;
static int NO_L12 = 0;
static int PREFER_NDS = 0;
static int RANDOM = 0;
static int DYN_RANDOM = 0;
static const char *algorithm = "heur";

typedef enum {
    POR_NONE,
    POR_HEUR,
    POR_DEL,
    POR_SCC,
    POR_SCC1,
} por_alg_t;

static por_alg_t alg = -1;

static si_map_entry por_algorithm[]={
    {"none",    POR_NONE},
    {"",        POR_HEUR},
    {"heur",    POR_HEUR},
    {"del",     POR_DEL},
    {"scc",     POR_SCC},
    {"scc1",    POR_SCC1},
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
        if (opt->shortName != 'p') return;
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
    }
    Abort("unexpected call to por_popt");
}

struct poptOption por_options[]={
    {NULL, 0, POPT_ARG_CALLBACK, (void *)por_popt, 0, NULL, NULL},
    { "por", 'p', POPT_ARG_STRING | POPT_ARGFLAG_OPTIONAL | POPT_ARGFLAG_SHOW_DEFAULT,
      &algorithm, 0, "enable partial order reduction", "<|heur|del|scc>" },

    /* HIDDEN OPTIONS FOR EXPERIMENTATION */

    { "check-por" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &PINS_POR , PINS_POR_CHECK , "verify partial order reduction peristent sets" , NULL },
    { "no-dna" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_DNA , 1 , "without DNA" , NULL },
    { "no-nes" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_NES , 1 , "without NES" , NULL },
    { "no-heur" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_HEUR , 1 , "without heuristic" , NULL },
    { "no-mds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MDS , 1 , "without MDS" , NULL },
    { "no-nds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_NDS , 1 , "without NDS (for dynamic label info)" , NULL },
    { "no-mc" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MC , 1 , "without MC (for NDS)" , NULL },
    { "no-dynamic-labels" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_DYNLAB , 1 , "without dynamic labels" , NULL },
    { "no-V" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_V , 1 , "without V proviso, instead use Peled's visibility proviso, or V'     " , NULL },
    { "no-L12" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_L12 , 1 , "without L1/L2 proviso, instead use Peled's cycle proviso, or L2'   " , NULL },
    { "prefer-nds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &PREFER_NDS , 1 , "prefer MC+NDS over NES" , NULL },
    { "por-random" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &RANDOM , 1 , "randomize necessary sets (static)" , NULL },
    { "por-dynamic" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &DYN_RANDOM , 1 , "randomize necessary sets (dynamic)" , NULL },
    { "weak" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &POR_WEAK , 1 , "Weak stubborn set theory" , NULL },
    POPT_TABLEEND
};

/**
 * Initialize the structures to record dynamically visible groups.
 * Executed only once after the higher PINS layer sets the group/label
 * visibilities.
 */
static inline void
init_dynamic_labels (por_context* pctx)
{
    if (pctx->marked_list != NULL) return; // only for first call!

    model_t model = pctx->parent;
    int groups = dm_nrows(GBgetDMInfo(model));
    int labels = pins_get_state_label_count (model);

    // record visible labels
    pctx->marked_list = RTmallocZero ((groups + 1) * sizeof(int));
    pctx->label_list = RTmallocZero ((labels + 1) * sizeof(int));
    for (int i = 0; i < labels; i++) {
        if (pctx->label_visibility[i]) {
            pctx->label_list->data[pctx->label_list->count++] = i;
        }
    }

    if (NO_DYNLAB) {
        // mark groups visible based on NES / NDS
        // (which may be based on read/write dependencies, if NO_NDS/NO_NES)
        for (int i = 0; i < pctx->label_list->count; i++) {
            int label = pctx->label_list->data[i];
            for (int j = 0; j < groups; j++) {
                pctx->group_visibility[j] =
                    dm_is_set (&pctx->gnds_matrix, label, j) ||
                    dm_is_set (&pctx->gnes_matrix, label, j);
            }
        }

        // Erase visible labels
        pctx->label_list->count = 0;
    }

    // record visible groups
    for (int i = 0; i < groups; i++) {
        if (pctx->group_visibility[i]) {
            pctx->visible_list->data[pctx->visible_list->count++] = i;
        }
    }
}

/**
 * For each call we need to update the visible group info from the label info
 * based on the disabledness / enabledness of the labels.
 */
static void
mark_dynamic_labels (por_context* pctx)
{
    init_dynamic_labels (pctx);
    pctx->marked_list->count = 0;
    for (int i = 0; i < pctx->label_list->count; i++) {
        int label = pctx->label_list->data[i];
        for (int j = 0; j < pctx->guard_dep[label]->count; j++) {
            int group = pctx->guard_dep[label]->data[j];
            if (pctx->group_visibility[group] || pctx->dynamic_visibility[group])
                continue;
            if (pctx->label_status[label]) {
                pctx->dynamic_visibility[group] |= dm_is_set (&pctx->gnds_matrix, label, group);
            } else {
                pctx->dynamic_visibility[group] |= dm_is_set (&pctx->gnes_matrix, label, group);
            }
            // already statically marked
            pctx->marked_list->data[pctx->marked_list->count] = group;
            pctx->marked_list->count += pctx->dynamic_visibility[group];
        }
    }
    Debug("Dynamically visible labels: %d", pctx->marked_list->count);
}

/**
 * Undo previous dynamically visible groups
 */
static void
unmark_dynamic_labels (por_context* pctx)
{
    for (int i = 0; i < pctx->marked_list->count; i++) {
        int group = pctx->marked_list->data[i];
        pctx->dynamic_visibility[group] = 0;
    }
}

static inline int
is_visible (por_context* ctx, int group)
{
    return ctx->group_visibility[group] || ctx->dynamic_visibility[group];
}

typedef struct scc_state_s {
    int              group;
    int              lowest;
} scc_state_t;

typedef struct scc_context_s {
    search_context  *search;           // context for each SCC search
    int              index;         // depth index
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


// number of necessary sets (halves if MC is absent, because no NDSs then)
static inline int
NS_SIZE (por_context* ctx)
{
    return NO_MC ? ctx->nguards : ctx->nguards * 2;
}

static inline int
visible_cost (por_context* ctx)
{
    return NO_V ? ctx->enabled_list->count * ctx->ngroups :
                  ctx->visible_enabled * ctx->ngroups + // enabled transitions
                          ctx->visible_list->count +    // disabled transitions
                          ctx->marked_list->count - ctx->visible_enabled;
}

static void
por_init_transitions (model_t model, por_context* ctx, int* src)
{
    // fill guard status, request all guard values
    GBgetStateLabelsAll (model, src, ctx->label_status);

    // fill dynamic groups
    mark_dynamic_labels (ctx);

    ctx->visible_enabled = 0;
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
        } else {
            // disabled
            ctx->group_score[i] = 1;
        }
    }
}

void
por_transition_costs (por_context* ctx)
{
    if (NO_HEUR) return;

    // set score for enable transitions
    for (int i = 0; i < ctx->enabled_list->count; i++) {
        int group = ctx->enabled_list->data[i];
        if (PINS_LTL && is_visible (ctx, group)) {
            // V proviso only for LTL!
            ctx->group_score[group] = visible_cost (ctx);
        } else {
            ctx->group_score[group] = ctx->ngroups;
        }
    }

    // fill nes score
    // heuristic score h(x): 1 for disabled transition, n for enabled transition, n^2 for visible transition
    for (int i = 0; i < NS_SIZE(ctx); i++) {
        ctx->nes_score[i] = 0;
        for (int j = 0; j < ctx->ns[i]->count; j++) {
            int group = ctx->ns[i]->data[j];
            ctx->nes_score[i] += ctx->group_score[group];
        }
    }
}

static scc_context_t *
create_scc_ctx (por_context* ctx)
{
    scc_context_t *scc = RTmallocZero (sizeof(scc_context_t));
    scc->group_index = RTmallocZero ((ctx->ngroups) * sizeof(int));
    scc->scc_list = RTmallocZero ((ctx->ngroups + 1) * sizeof(int));
    scc->stubborn_list = RTmallocZero (2 * sizeof(ci_list *));
    scc->stubborn_list[0] = RTmallocZero ((ctx->ngroups + 1) * sizeof(int));
    scc->stubborn_list[1] = RTmallocZero ((ctx->ngroups + 1) * sizeof(int));
    scc->bad_scc = RTmallocZero ((ctx->ngroups + 1) * sizeof(int));
    scc->stack = dfs_stack_create (2); // only integers for groups
    scc->tarjan = dfs_stack_create (1); // only integers for group
    scc->search = &ctx->search[0];
    ctx->search_order[0] = 0;
    return scc;
}

/**
 * For each state, this function sets up the current guard values etc
 * This setup is then reused by the analysis function
 */
static void
scc_setup (model_t model, por_context* ctx, int* src)
{
    por_init_transitions (model, ctx, src);

    por_transition_costs (ctx);

    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    memset(scc->search->emit_status, 0, sizeof(emit_status_t[ctx->ngroups]));
    memcpy(scc->search->nes_score, ctx->nes_score, NS_SIZE(ctx) * sizeof(int));
    memset(scc->group_index, SCC_NEW, ctx->ngroups * sizeof(int));
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

    // select an enabled transition group
    int beam_idx = 0;
    for(int i=0; i<ctx->enabled_list->count; i++) {
        int group = ctx->enabled_list->data[i];
        // add to beam search
        ctx->search[beam_idx].work[0] = group;
        ctx->search[beam_idx].has_key = 0;
        // init work_enabled/work_disabled
        ctx->search[beam_idx].work_enabled = 1;
        ctx->search[beam_idx].work_disabled = ctx->ngroups;
        // init score
        ctx->search[beam_idx].score = 0; // 0 = uninitialized
        memset(ctx->search[beam_idx].emit_status, 0, sizeof(emit_status_t[ctx->ngroups]));
        if (!NO_HEUR)
            memcpy(ctx->search[beam_idx].nes_score, ctx->nes_score, NS_SIZE(ctx) * sizeof(int));
        // reset counts
        ctx->search[beam_idx].visibles_selected = 0;
        ctx->search[beam_idx].enabled_selected = 0;
        ctx->search[beam_idx].ve_selected = 0;
        ctx->search[beam_idx].idx = beam_idx;

        ctx->search_order[beam_idx] = beam_idx;
        beam_idx++;
    }
    // reset beam
    ctx->beam_used = beam_idx;
    // clear emit status
    ctx->emit_score = 1;
    ctx->emit_limit = beam_idx; // if |persistent| = |en|, we can stop the search
}

/**
 * The function update_score is called whenever a new group is added to the stubborn set
 * It takes care of updating the heuristic function for the nes based on the new group selection
 */
static inline void
update_ns_scores (por_context* ctx, search_context *s, int group)
{
    if (NO_HEUR) return;

    // change the heuristic function according to selected group
    for(int k=0 ; k< ctx->group2ns[group]->count; k++) {
        int ns = ctx->group2ns[group]->data[k];
        // note: this selects some nesses that aren't used, but take the extra work
        // instead of accessing the guards to verify the work
        s->nes_score[ns] -= ctx->group_score[group];
    }
}

/**
 * Mark a group selected, update counters and NS scores.
 */
static inline void
select_group (por_context* ctx, int group)
{
    // get current search context
    search_context *s = &ctx->search[ ctx->search_order[0] ];

    // already selected?
    if (s->emit_status[group] & ES_SELECTED) {
        Printf (debug, "(%d), ", group);
        return;
    }

    s->emit_status[group] |= ES_SELECTED;

    update_ns_scores (ctx, s, group);

    // and add to work array and update counts
    if (ctx->group_status[group] & GS_DISABLED) {
        s->work[s->work_disabled--] = group;
    } else {
        s->work[s->work_enabled++] = group;
        s->ve_selected += is_visible(ctx, group);
        s->enabled_selected++;
    }
    s->visibles_selected += is_visible(ctx, group);
    Printf (debug, "%d, ", group);
}

static inline void
select_one_invisible (por_context* ctx)
{
    // Valmari's L1 proviso requires one invisible transition (to include quiescent runs)
   for(int i=0; i<ctx->enabled_list->count; i++) {
       int group = ctx->enabled_list->data[i];
       if (!is_visible(ctx, group)) {
           select_group (ctx, group);
           return;
       }
   }
   HREassert (false, "Called select_one_invisible without enabled invisible transitions.");
}

static inline void
select_all_visible (por_context* ctx)
{
    // Valmari's V-proviso: implicate all visible groups
   for(int i=0; i<ctx->visible_list->count; i++) {
       int group = ctx->visible_list->data[i];
       select_group (ctx, group);
   }
   for (int i = 0; i < ctx->marked_list->count; i++) {
       int group = ctx->marked_list->data[i];
       select_group (ctx, group);
   }
}

/**
 * Based on the heuristic and find the cheapest NS (NES/NDS) for a disabled
 * group.
 */
static inline int
find_cheapest_ns (por_context* ctx, search_context *s, int group)
{
    int n_guards = ctx->nguards;

    // for a disabled transition we need to add the necessary set
    // lookup which set has the lowest score on the heuristic function h(x)
    int selected_ns = -1;
    int selected_score = INT32_MAX;
    int count = ctx->group_has[group]->count;

    if (DYN_RANDOM)
        randperm (ctx->random, count, ctx->seed++);

    // for each possible nes for the current group
    for (int k = 0; k < count; k++) {

        int ns = ctx->group_has[group]->data[ ctx->random[k] ];

        // check the score by the heuristic function h(x)
        if (NO_HEUR || s->nes_score[ns] < selected_score) {
            // check guard status for ns (nes for disabled and nds for enabled):
            if ((ns < n_guards && (ctx->label_status[ns] == 0))  ||
                (ns >= n_guards && (ctx->label_status[ns-n_guards] != 0)) ) {

                // make this the current best
                selected_ns = ns;
                selected_score = s->nes_score[ns];
                // if score is 0 it can't improve, break the loop
                if (NO_HEUR || selected_score == 0) return selected_ns;
            }
        }
    }
    if (selected_ns == -1) Abort ("selected nes -1");
    return selected_ns;
}

/**
 * Sorts BEAM search contexts.
 */
static bool
beam_sort (por_context *ctx)
{
    search_context *s = &ctx->search[ ctx->search_order[0] ];

    // if it can't move, we found the best score
    if (ctx->beam_used > 1 && s->score > ctx->search[ctx->search_order[1]].score) {
        // if score is one, and we have no more work, we're ready too
        if (s->score == ctx->emit_score && s->work_disabled == ctx->ngroups) {
            Printf (debug, "bailing out, no disabled work and |ss|=1\n");
            return false;
        }

        // bubble current context down the search, continue with other context
        // this is known by the above conditions
        ctx->search_order[0] = ctx->search_order[1];
        // continue with 2
        int bubble = 2;
        while(bubble < ctx->beam_used && s->score >= ctx->search[ctx->search_order[bubble]].score) {
            ctx->search_order[bubble-1] = ctx->search_order[bubble];
            bubble++;
        }
        bubble--;

        if (bubble < ctx->beam_used)
            ctx->search_order[bubble] = s->idx;
    } else if (s->work_disabled == ctx->ngroups) {
        Printf (debug, "bailing out, no disabled work\n");
        return false;
    }
    return true;
}

static void
add_enabled (por_context *ctx, int group)
{
    ci_list **accords = POR_WEAK ? ctx->not_left_accords : ctx->not_accords;
    for (int j = 0; j < accords[group]->count; j++) {
        int dependent_group = accords[group]->data[j];
        select_group (ctx, dependent_group);
    }
}

/**
 * Analyze NS is the function called to find the smallest persistent set
 * It builds stubborn sets in multiple search contexts, by using a beam
 * search it switches search context each time the score of the current context
 * (based on heuristic function h(x) (nes_score)) isn't the lowest score anymore
 */
static inline void
bs_analyze (por_context* ctx)
{
    // if no search context is used, there are no transitions, nothing to analyze
    if (ctx->beam_used == 0) return;

    // infinite loop searching in multiple context, will bail out
    // when persistent set is found
    do {
        // start searching in multiple contexts
        // the search order is a sorted array based on the score of the search context
        // start with the context in search_order[0] = best current score
        search_context *s = &ctx->search[ ctx->search_order[0] ];

        // while there are disabled transitions:
        while(s->work_enabled == 0 && s->work_disabled < ctx->ngroups) {
            // one disabled transition less, increase the count (work_disabled = n -> no disabled transitions)
            s->work_disabled++;
            int current_group = s->work[s->work_disabled];

            // bail out if already ready
            if (s->emit_status[current_group] & ES_READY) continue;

            // mark as selected and ready
            s->emit_status[current_group] |= ES_SELECTED | ES_READY;

            Printf (debug, "BEAM-%d investigating group %d (disabled) --> ", s->idx, current_group);

            int selected_ns = find_cheapest_ns (ctx, s, current_group);

            // add the selected nes to work
            for(int k=0; k < ctx->ns[selected_ns]->count; k++) {
                int group = ctx->ns[selected_ns]->data[k];
                select_group (ctx, group);
            }
            Printf (debug, " (ns %d (%s))\n", selected_ns % ctx->nguards,
                    selected_ns < ctx->nguards ? "disabled" : "enabled");
        }

        // if the current search context has enabled transitions, handle all of them
        while (s->work_enabled > 0) {
            // one less enabled transition (work_enabled = 0 -> no enabled transitions)
            s->work_enabled--;
            int current_group = s->work[s->work_enabled];
            Printf (debug, "BEAM-%d investigating group %d (enabled) --> ", s->idx, current_group);

            // select and mark as ready
            s->emit_status[current_group] |= ES_SELECTED | ES_READY;

            // this is a bit strange. the first group added by setup must be selected too
            // the condition here enforces that only the first enabled transition is selected.
            // after this, the score is > emit_score-1. but this might change when the
            // emit_score is changed in the initialization. Should select_group be called at setup?
            // other idea is to init the search context only here, because we might need to
            // do a lot more work if we do it all at once and then just need one
            if (s->score == ctx->emit_score-1)
                update_ns_scores (ctx, s, current_group);

            // update the search score
            s->score += 1;

            // V proviso only for LTL
            if (PINS_LTL) {
                if (is_visible(ctx, current_group)) {
                    if (NO_V) { // Use Peled's stronger visibility proviso:
                        s->score += ctx->ngroups; // selects all groups in this search context
                    } else {
                        select_all_visible (ctx);
                    }
                }
            }

            // quit the current search when emit_limit is reached
            // this block is just to skip useless work, everything is emitted anyway
            if (s->score >= ctx->emit_limit) {
                s->work_enabled = 0;
                s->work_disabled = ctx->ngroups;
                Printf (debug, " (quitting |ss|=|en|)\n");
                break;
            }

            // push all dependent unselected ctx->ngroups
            add_enabled (ctx, current_group);
            Printf (debug, "\n");
        }
    } while (beam_sort(ctx));
}

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
        int ns = find_cheapest_ns (ctx, scc->search, group);
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
reset_ns_scores (por_context* ctx, search_context *s, int group)
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

    Warning (debug, "Found %d enabled transitions in SCC",scc->stubborn_list[1]->count);

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
            Warning (debug, "Found key");
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

        Warning (debug, "Found stubborn SCC of size %d,%d!", scc->stubborn_list[1]->count, count);
        int count = scc->stubborn_list[0]->count;
        if (scc->stubborn_list[1]->count < count) {
            swap (scc->stubborn_list[0], scc->stubborn_list[1]);
            scc->bad_scc->data[scc->bad_scc->count++] = SCC_SCC; // remember stubborn SCC
            Warning (debug, "Update stubborn set %d --> %d!", count < INT32_MAX ? count : -1, scc->stubborn_list[0]->count);
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
                    Warning (debug, "Failed adding key");
                    break;
                } else if (to_explore > 0) {
                    Warning (debug, "Continuing search");
                    continue;
                } else {
                    Warning (debug, "Key added nothing or not needed");
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

    Warning (debug, "%s", "");
    scc->bad_scc->count = 0; // not SCC yet
    SCC_SCC = -1;
    for (int j=0; j < ctx->enabled_list->count; j++) {
        int group = ctx->enabled_list->data[j];
        if (scc->group_index[group] == SCC_NEW) {
            Warning (debug, "SCC search from %d", group);
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

/**
 * These functions emits the persistent set with cycle proviso
 * To ensure this proviso extra communication with the algorithm is required.
 * The algorithm annotates each transition with the por_proviso flag.
 * For ltl, all selected transition groups in the persistent set must
 * have this por_proviso flag set to true, otherwise enabled(s) will be returned.
 * For safety, the proviso needs to hold for at least on emitted state.
 * The client may (should) set the proviso always to true for deadlocks.
 */

typedef struct proviso_hook_context {
    TransitionCB    cb;
    void*           user_context;
    int             por_proviso_true_cnt;
    int             por_proviso_false_cnt;
    int             force_proviso_true;     // feedback to algorithm that proviso already holds
} proviso_hook_context_t;

void hook_cb (void*context,transition_info_t *ti,int*dst,int*cpy) {
    proviso_hook_context_t* infoctx = (proviso_hook_context_t*)context;
    transition_info_t ti_new = GB_TI (ti->labels, ti->group);
    ti_new.por_proviso = infoctx->force_proviso_true;
    infoctx->cb(infoctx->user_context, &ti_new, dst,cpy);
    // catch transition info status
    if (infoctx->force_proviso_true || ti_new.por_proviso) {
        infoctx->por_proviso_true_cnt++;
    } else {
        infoctx->por_proviso_false_cnt++;
    }
}

static inline int
emit_new_selected (por_context* ctx, proviso_hook_context_t* provctx, int* src)
{
    search_context *s = &ctx->search[ctx->search_order[0]];
    int c = 0;
    for (int z = 0; z < ctx->enabled_list->count; z++) {
        int i = ctx->enabled_list->data[z];
        if (s->emit_status[i] & ES_EMITTED) continue;

        if ((s->emit_status[i] & ES_SELECTED) || s->score >= ctx->emit_limit) {
            s->emit_status[i] |= ES_EMITTED;
            c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, provctx);
        }
    }
    return c;
}

/**
 * Premature check whether L1 and L2 hold, i.e. before ignoring condition is
 * known (the premise of L2).
 * For safety (!ctx->ltl), we limit the proviso to L2. For details see
 * implementation notes in the header.
 */
static inline int
check_L1_L2_proviso (por_context* ctx)
{
    search_context *s = &ctx->search[ctx->search_order[0]];
    return s->visibles_selected ==
       ctx->visible_list->count + ctx->marked_list->count && // all visible selected: satisfies (the conclusion of) L2
     (!PINS_LTL || // safety!
      ctx->visible_enabled == ctx->enabled_list->count ||    // no invisible is enabled: satisfies (the premise of) L1
      s->ve_selected != s->enabled_selected);                // one invisible enabled selected: satisfies (the conclusion of) L1
}

static inline int
bs_emit (por_context* ctx, int* src, TransitionCB cb, void* uctx)
{
    // if no enabled transitions, return directly
    if (ctx->beam_used == 0) return 0;
    // selected in winning search context
    search_context *s = &ctx->search[ctx->search_order[0]];
    // if the score is larger then the number of enabled transitions, emit all
    if (s->score >= ctx->emit_limit) {
        // return all enabled
        proviso_hook_context_t provctx = {cb, uctx, 0, 0, 1};
        return GBgetTransitionsAll(ctx->parent, src, hook_cb, &provctx);
    // otherwise: try the persistent set, but enforce that all por_proviso flags are true
    } else {
        proviso_hook_context_t provctx = {cb, uctx, 0, 0, 0};
        provctx.force_proviso_true = !NO_L12 && check_L1_L2_proviso (ctx);

        int emitted = emit_new_selected (ctx, &provctx, src);

        // emit more if we need to fulfill a liveness / safety proviso
        if ( ( PINS_LTL && provctx.por_proviso_false_cnt != 0) ||
             (!PINS_LTL && provctx.por_proviso_true_cnt  == 0) ) {

            if (!NO_L12) {
                ctx->beam_used = 1; // fix to current (partly emitted) search ctx

                // enforce L2 (include all visible transitions)
                select_all_visible (ctx);
                bs_analyze (ctx);

                provctx.force_proviso_true = check_L1_L2_proviso (ctx);
                // enforce L1 (one invisible transition)
                // not to be worried about when using V'
                if (PINS_LTL && !NO_V && !provctx.force_proviso_true) {
                    select_one_invisible (ctx);
                    bs_analyze (ctx);
                }
            } else {
                s->score = ctx->emit_limit; // force all enabled
            }
            emitted += emit_new_selected (ctx, &provctx, src);
        }
        return emitted;
    }
}

static int
scc_emit (por_context* ctx, int* src, TransitionCB cb, void* uctx)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;

    if (ctx->enabled_list->count == scc->stubborn_list[0]->count) {
        // return all enabled
        proviso_hook_context_t provctx = {cb, uctx, 0, 0, 1};
        return GBgetTransitionsAll(ctx->parent, src, hook_cb, &provctx);
    } else {
        proviso_hook_context_t provctx = {cb, uctx, 0, 0, 0};
        provctx.force_proviso_true = !NO_L12 && check_L1_L2_proviso (ctx);

        int c = 0;
        for (int z = 0; z < scc->stubborn_list[0]->count; z++) {
            int i = scc->stubborn_list[0]->data[z];
            c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, &provctx);
        }
        return c;
    }
}

static inline void
bs_ensure_key (por_context* ctx)
{
    // if no enabled transitions, return directly
    search_context *s = &ctx->search[ctx->search_order[0]];
    if (!POR_WEAK || ctx->beam_used == 0 || s->score >= ctx->emit_limit) return;

    if (s->has_key)
        Warning (debug, "Key included");

    while ( !s->has_key ) {
        size_t min_score = INT32_MAX;
        int min_group = -1;
        ctx->nds_list[0]->count = 0;
        for (int i = 0; i < ctx->enabled_list->count; i++) {
            int group = ctx->enabled_list->data[i];
            if ( !(s->emit_status[group] & ES_SELECTED) ) continue;

            // check open NDSs
            size_t score = 0;
            ctx->nds_list[1]->count = 0;
            // First: all nds for the group's guards
            for (int g = 0; g < ctx->not_accords[group]->count && score < min_score; g++) {
                int gg = ctx->not_accords[group]->data[g];
                int fresh = (s->emit_status[gg] & ES_SELECTED) == 0;
                score += fresh ? ctx->group_score[gg] : 0;
                ctx->nds_list[1]->data[ctx->nds_list[1]->count] = gg;
                ctx->nds_list[1]->count += fresh;
            }

            if (score == 0) {
                Warning (debug, "Key is %d (all NDSs included)", group);
                return; // OK (all NDS's are in the SS)
            }

            if (score < min_score) {
                min_score = score;
                min_group = group;
                swap (ctx->nds_list[0], ctx->nds_list[1]);
            }
        }

        // add all the NDS's for the transition for which it is cheapest
        HREassert (ctx->nds_list[0]->count != 0);
        Printf (debug, "Adding NDSs: ");
        for (int g = 0; g < ctx->nds_list[0]->count; g++) {
            int gg = ctx->nds_list[0]->data[g];
            select_group (ctx, gg);
        }
        s->has_key = 1;
        Warning (debug, "\nKey is %d (forced inclusion of NDSs: %s)", min_group);

        bs_analyze (ctx); // may select a different search context!
        s = &ctx->search[ctx->search_order[0]];
    }
}

/**
 * Implementation of the deletion algorithm.
 */

typedef struct del_context_s {
    char          *set;
    ci_list       **lists;
} del_context_t;

/**
 * Sets in deletion algorithm
 * Some are maintained only as stack or as set with cardinality counter
 * A "stack set" can both be iterated over and performed inclusion tests on,
 * however it does not support element removal (as it messes up the stack).
 */
typedef enum {
    DEL_N,  // set
    DEL_K,  // count set (stack content may be corrupted)
    DEL_C,  // stack
    DEL_Z,  // stack set
    DEL_R,  // set
    DEL_KP, // stack
    DEL_NP, // stack
    DEL_DP, // stack
    DEL_COUNT
} del_t;

static del_context_t *
create_del_ctx (por_context* ctx)
{
    del_context_t *del = RTmallocZero (sizeof(del_context_t));
    del->set = RTmallocZero ((ctx->ngroups + NS_SIZE(ctx)));
    del->lists = RTmallocZero (DEL_COUNT * sizeof(ci_list)); // see del_t
    for (int i = 0; i < DEL_COUNT; i++)
        del->lists[i] = RTmallocZero ((ctx->ngroups + NS_SIZE(ctx) + 1) * sizeof(int));
    return del;
}

static inline bool
del_enabled (por_context* ctx, int u)
{
    return ctx->group_status[u] == GS_ENABLED;
}

static inline bool
del_has (del_context_t *del, del_t set, int u)
{
    return (del->set[ u ] & (1<<set)) != 0;
}

static inline int
del_count (del_context_t *del, del_t set)
{
    return del->lists[set]->count;
}

// Messes up stack, only stack counter is maintained. Not to be combined with pop!
static inline bool
del_rem (del_context_t *del, del_t set, int u)
{
    int seen = del_has (del, set, u);
    del->set[ u ] &= ~(1 << set);
    del->lists[set]->count -= seen;
    return seen;
}

static inline bool
del_add (del_context_t *del, del_t set, int u)
{
    int seen = del_has (del, set, u);
    del->set[ u ] |= 1<<set;
    del->lists[set]->count += !seen;
    return !seen;
}

static inline void
del_clear (del_context_t *del, del_t set)
{
    del->lists[set]->count = 0;
}

static inline void
del_push (del_context_t *del, del_t set, int u)
{
    del->lists[set]->data[ del->lists[set]->count++ ] = u;
}

static inline bool
del_push_new (del_context_t *del, del_t set, int u)
{
    int seen = del_has (del, set, u);
    del->set[ u ] |= 1<<set;
    if (!seen)
        del_push (del, set, u);
    return !seen;
}

static inline int
del_pop (del_context_t *del, del_t set)
{
    HREassert (del_count(del, set) != 0, "Pop on empty set stack %d", set);
    int v = del->lists[set]->data[ --del->lists[set]->count ];
    del->set[ v ] &= ~(1 << set);
    return v;
}

static void
deletion_setup (model_t model, por_context* ctx, int* src)
{
    por_init_transitions (model, ctx, src);
    if (ctx->enabled_list->count == 0) return;

    del_context_t *del = ctx->del_ctx;

    // use -1 for deactivated NESs
    for (int ns = 0; ns < ctx->nguards; ns++) { // guard should be false
        ctx->nes_score[ns] = 0 - (ctx->label_status[ns] != 0); // 0 for false!
    }
    for (int ns = ctx->nguards; ns < NS_SIZE(ctx); ns++) { // guard should be true
        ctx->nes_score[ns] = 0 - (ctx->label_status[ns - ctx->nguards] == 0); // 0 for true!
    }

    // initially all active ns's are in:
    for (int t = 0; t < ctx->ngroups; t++) {
        ctx->group_score[t] = 0;
        if (del_enabled(ctx,t)) continue;
        for (int i = 0; i < ctx->group_has[t]->count; i++) {
            int ns = ctx->group_has[t]->data[i];
            ctx->group_score[t] += ctx->nes_score[ns] >= 0;
        }
    }

    // K = {}; C := {}; Z := {}; KP := {}; TP := {}; R := {}
    // N := T
    for (int i = 0; i < ctx->ngroups; i++) {
        del->set[i] = 0;
        del_add (del, DEL_N, i); // other stubborn
    }
    for (int i = 0; i < DEL_COUNT; i++) {
        del_clear (del, i);
    }

    // C := A \ R; K := A
    for (int i = 0; i < ctx->enabled_list->count; i++) {
        int group = ctx->enabled_list->data[i];
        del_push_new (del, DEL_K, group);
        if (!del_has(del, DEL_R, group)) {
            del_push (del, DEL_C, group);
        }
    }
    Warning (debug, "Deletion init |en| = %d \t|R| = %d", ctx->enabled_list->count, del_count(del, DEL_R));
}

static inline bool
deletion_delete (por_context* ctx, int v)
{
    del_context_t *del = ctx->del_ctx;

    // search the deletion space:
    while (del_count(del, DEL_Z) != 0 && del_count(del, DEL_K) != 0) {
        int z = del_pop (del, DEL_Z);
        Warning (debug, "Checking u = %d", z);

        if (del_has(del,DEL_R,z)) return true;

        // Firstly, the enabled transitions x that are still stubborn and
        // belong to DNA_u, need to be removed from key stubborn and put to Z.
        for (int i = 0; i < ctx->not_accords[z]->count; i++) {
            int x = ctx->not_accords[z]->data[i];
            if (del_has(del,DEL_K,x)) {
                if (!del_has(del,DEL_N,x)) {
                    if (del_has(del,DEL_R,x)) return true;
                    del_push_new (del, DEL_Z, x);
                }
                if (del_rem(del, DEL_K, x)) del_push (del, DEL_KP, x);
            }
        }
        if (del_count(del, DEL_K) == 0) return true;

        // Secondly, the enabled transitions x that are still stubborn and
        // belong to DNB_u need to be removed from other stubborn and put to Z.
        for (int i = 0; i < ctx->not_left_accordsn[z]->count; i++) {
            int x = ctx->not_left_accordsn[z]->data[i];
            if (del_enabled(ctx,x) && del_has(del,DEL_N,x)) {
                if (!del_has(del,DEL_K,x)) {
                    if (del_has(del,DEL_R,x)) return true;
                    del_push_new (del, DEL_Z, x);
                }
                if (del_rem(del, DEL_N, x)) del_push (del, DEL_NP, x);
            }
        }

        del_push (del, DEL_DP, z);
        // Thirdly, the disabled transitions x, whose NES was stubborn
        // before removal of u, need to be put to Z.
        for (int i = 0; i < ctx->group2ns[z]->count; i++) {
            int ns = ctx->group2ns[z]->data[i];
            if (ctx->nes_score[ns] == -1) continue; // -1 is inactive!

            // not the first transition removed from NES?
            int score = ctx->nes_score[ns]++;
            if (score != 0) continue;

            for (int i = 0; i < ctx->group_hasn[ns]->count; i++) {
                int x = ctx->group_hasn[ns]->data[i];
                if (!del_enabled(ctx,x)) {
                    ctx->group_score[x]--;
                    HREassert (ctx->group_score[x] >= 0, "Wrong counting!");
                    if (ctx->group_score[x] == 0 && del_has(del,DEL_N,x)) {
                        del_push_new (del, DEL_Z, x);
                        HREassert (!del_has(del, DEL_K, x)); // x is disabled
                        if (del_rem(del, DEL_N, x)) del_push (del, DEL_NP, x);
                    }
                }
            }
        }
    }
}

static inline void
deletion_analyze (por_context* ctx)
{
    if (ctx->enabled_list->count == 0) return;
    del_context_t *del = ctx->del_ctx;

    while (del_count(del, DEL_C) != 0 && del_count(del, DEL_K) > 1) {
        int v = del_pop (del, DEL_C);
        if (del_rem(del, DEL_K, v)) del_push (del, DEL_KP, v);
        if (del_rem(del, DEL_N, v)) del_push (del, DEL_NP, v);
        del_push_new (del, DEL_Z, v);

        Warning (debug, "Deletion start from v = %d: |C| = %d \t|K| = %d", v, del_count(del, DEL_C), del_count(del, DEL_K));

        bool revert = deletion_delete (ctx, v);

        while (del_count(del, DEL_Z) != 0) del_pop (del, DEL_Z);

        // Reverting deletions if necessary
        if (revert) {
            Warning (debug, "Deletion rollback: |T'| = %d \t|K'| = %d \t|D'| = %d",
                     del_count(del, DEL_NP), del_count(del, DEL_KP), del_count(del, DEL_DP));
            del_add (del, DEL_R, v); // fail transition!
            while (del_count(del, DEL_KP) != 0) {
                int x = del_pop (del, DEL_KP);
                bool seen = del_push_new (del, DEL_K, x);
                HREassert (seen, "DEL_K messed up");
            }
            while (del_count(del, DEL_NP) != 0) {
                int x = del_pop (del, DEL_NP);
                del->set[x] = del->set[x] | (1<<DEL_N);
            }
            while (del_count(del, DEL_DP) != 0) {
                int x = del_pop (del, DEL_DP);
                for (int i = 0; i < ctx->group2ns[x]->count; i++) {
                    int ns = ctx->group2ns[x]->data[i];
                    ctx->nes_score[ns] -= (ctx->nes_score[ns] >= 0);
                    if (ctx->nes_score[ns] != 0) continue; // NES not readded

                    for (int i = 0; i < ctx->group_hasn[ns]->count; i++) {
                        int x = ctx->group_hasn[ns]->data[i];
                        ctx->group_score[x] += !del_enabled(ctx,x);
                    }
                }
            }
        } else {
            del_clear (del, DEL_KP);
            del_clear (del, DEL_NP);
            del_clear (del, DEL_DP);
        }
    }
}

static inline int
deletion_emit_new (por_context* ctx, proviso_hook_context_t* provctx, int* src)
{
    del_context_t *del = ctx->del_ctx;
    int c = 0;
    for (int z = 0; z < ctx->enabled_list->count; z++) {
        int i = ctx->enabled_list->data[z];
        if (del_has(del,DEL_N,i)) { // && !del_has(del,DEL_R,i)
            //del->set[i] |= 1<<DEL_R;
            c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, provctx);
        }
    }
    return c;
}

static inline int
deletion_emit (por_context* ctx, int* src, TransitionCB cb, void* uctx)
{
    proviso_hook_context_t provctx = {cb, uctx, 0, 0, 0};
    provctx.force_proviso_true = !NO_L12 && check_L1_L2_proviso (ctx);

    int emitted = deletion_emit_new (ctx, &provctx, src);

    //TODO: LTL and safety

    return emitted;
}


/**
 * Same but for Safety / Liveness
 */
static int
por_deletion_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    por_context* ctx = ((por_context*)GBgetContext(self));
    deletion_setup (self, ctx, src);
    deletion_analyze (ctx);
    int emitted = deletion_emit (ctx, src, cb, user_context);
    unmark_dynamic_labels (ctx);
    return emitted;
}

/**
 * Same but for Safety / Liveness
 */
static int
por_beam_search_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    por_context* ctx = ((por_context*)GBgetContext(self));
    beam_setup (self, ctx, src);
    bs_analyze (ctx);
    bs_ensure_key (ctx);
    int emitted = bs_emit (ctx, src, cb, user_context);
    unmark_dynamic_labels (ctx);
    return emitted;
}

/**
 * Same but for Safety / Liveness using SCC algorithm
 */
static int
por_scc_search_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    por_context* ctx = ((por_context*)GBgetContext(self));
    scc_setup (self, ctx, src);
    if (ctx->enabled_list->count == 0) return 0;
    scc_analyze (ctx);
    int emitted = scc_emit (ctx, src, cb, user_context);
    unmark_dynamic_labels (ctx);
    return emitted;
}

static void
list_invert (ci_list *list)
{
    for (int i = 0; i < list->count / 2; i++) {
        swap (list->data[i], list->data[list->count - i - 1]);
    }
}

static void
list_randomize (ci_list *list, int seed)
{
    int rand[list->count];
    randperm (rand, list->count, seed);
    for (int i = 0; i < list->count; i++) {
        swap (list->data[i], list->data[ rand[i] ]);
    }
}

/**
 * Function used to setup the beam search
 * Initialize and allocate all memory that is reused all the time
 * Setup pointers to functions used in beam search
 *
 * Additionally, it combines gaurd info with MCE for quick NES/NDS access.
 */
static void *
bs_init_beam_context (model_t model)
{
    por_context* ctx = (por_context*) GBgetContext(model);

    // get number of transition groups
    ctx->ngroups = dm_nrows(GBgetDMInfo(model));
    sl_group_t *guardLabels = GBgetStateLabelGroupInfo (model, GB_SL_GUARDS);
    HREassert (guardLabels->sl_idx[0] == 0);
    // get the number of guards
    ctx->nguards = guardLabels->count;
    // get the number of labels
    ctx->nlabels = pins_get_state_label_count(model);

    Debug ("Groups %d, labels %d, guards %d\n", ctx->ngroups, ctx->nlabels, ctx->nguards);

    // setup a fixed maximum of number of search contexts
    // since we use at most one search context per transition group, use n
    const int BEAM_WIDTH = ctx->ngroups;

    // set number of guards used
    // alloc search contexts
    ctx->search = RTmallocZero( BEAM_WIDTH * sizeof(search_context) );
    // set #emitted to zero, meaning that a new analysis should start
    ctx->emitted = 0;
    // init group status array
    ctx->group_status = RTmallocZero(ctx->ngroups * sizeof (group_status_t) );
    ctx->group_score  = RTmallocZero(ctx->ngroups * sizeof(int) );
    // init guard_status array
    ctx->label_status = RTmallocZero(ctx->nlabels * sizeof(int) );

    // init beam_width/beam_used
    ctx->beam_width = BEAM_WIDTH;
    ctx->beam_used = BEAM_WIDTH;

    // init search_order;
    ctx->search_order = RTmallocZero(BEAM_WIDTH * sizeof(int) );

    // init nes score template
    ctx->nes_score = RTmallocZero( (ctx->nguards * 2) * sizeof(int) );

    // get local variables pointing to nes/nds
    ci_list** nes = ctx->guard_nes;
    ci_list** nds = ctx->guard_nds;

    // setup global group_in/group_has relation
    // idea, combine nes and nds in one data structure (ns, necessary set)
    // each guard is either disabled (use nes) or enabled (nds)
    // setup: ns = [0...n_guards-1] nes, [n_guards..2n_guards-1] nds
    // this way the search algorithm can skip the nes/nds that isn't needed based on
    // guard_status, using <n or >=n as conditions
    matrix_t group_in;
    dm_create(&group_in, ctx->nguards * 2, ctx->ngroups);
    for (int i=0; i < ctx->nguards; i++) {
        for (int j=0; j < nes[i]->count; j++) {
            dm_set(&group_in, i, nes[i]->data[j]);
        }
        for (int j=0; j < nds[i]->count; j++) {
            int group = nds[i]->data[j];
            dm_set(&group_in, i+ctx->nguards, group);
        }
        Printf (debug, "Guard %4d: NESs %4d, NDSs %4d\n", i, nes[i]->count, nds[i]->count);
    }
    // build tables ns, and group in
    ctx->ns = (ci_list**) dm_rows_to_idx_table(&group_in);
    ctx->group2ns = (ci_list**) dm_cols_to_idx_table(&group_in);
    dm_free(&group_in);

    // group has relation
    // mapping [0...n_guards-1] disabled guard (nes)
    // mapping [n_guards..2*n_guards-1] enabled guard (nds)
    // group has relation is more difficult because nds
    // needs not co-enabled info and transition groups

    // setup group has relation
    matrix_t group_has;
    dm_create(&group_has, ctx->ngroups, ctx->nguards * 2);
    for (int i=0; i < ctx->nguards; i++) {
        // nes
        for (int k=0; k < ctx->guard2group[i]->count; k++) {
            dm_set(&group_has, ctx->guard2group[i]->data[k], i);
        }
        // nds
        if (!NO_MC) { // add NDS range:
            for (int k=0; k < ctx->guard_nce[i]->count; k++) {
                dm_set(&group_has, ctx->guard_nce[i]->data[k], i + ctx->nguards);
            }
            Printf (debug, "Guard %4d: !MCs %4d\n", i, ctx->guard_nce[i]->count);
        }
    }
    // build table group has
    ctx->group_has = (ci_list**) dm_rows_to_idx_table(&group_has);
    ctx->group_hasn = (ci_list**) dm_cols_to_idx_table(&group_has);
    dm_free(&group_has);

    if (PREFER_NDS) {
        HREassert (!RANDOM, "--por-random incompatible with --prefer-nds");
        for (int i=0; i < ctx->ngroups; i++) {
            list_invert (ctx->group_has[i]);
        }
    }
    if (RANDOM) {
        HREassert (!PREFER_NDS, "--por-random incompatible with --prefer-nds");
        for (int i=0; i < ctx->ngroups; i++) {
            list_randomize (ctx->group_has[i], i);
        }
    }

    // init for each search context
    for(int i=0 ; i < BEAM_WIDTH; i++) {
        // init default search order
        ctx->search_order[i] = i;
        // init emit status
        ctx->search[i].emit_status = RTmallocZero( ctx->ngroups * sizeof(emit_status_t) );
        // init work_array
        // note, n+1 to initialize work_disabled on n
        ctx->search[i].work = RTmallocZero( (ctx->ngroups+1) * sizeof(int) );
        // init work_enabled/work_disabled
        ctx->search[i].work_enabled = 0;
        ctx->search[i].work_disabled = ctx->ngroups;
        // init score
        ctx->search[i].score = 0;
        // note, n+1 to initialize work_disabled on n
        ctx->search[i].nes_score = RTmallocZero( (ctx->nguards * 2) * sizeof(int) );
    }

    return ctx;
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

    Print1 (info,"Initializing partial order reduction layer..");

    // check support for guards, fail without
    if (!GBhasGuardsInfo(model)) {
        Print1 (info, "Frontend doesn't have guards. Ignoring --por.");
        return model;
    }

    // do the setup
    model_t             pormodel = GBcreateBase ();

    por_context *ctx = RTmalloc (sizeof *ctx);
    ctx->parent = model;

    // initializing dependency lookup table ( (t, t') \in D relation)
    Print1 (info, "Initializing dependency lookup table.");

    matrix_t           *p_dm = NULL;
    matrix_t           *p_dm_w = GBgetDMInfoMayWrite (model);
    int id = GBgetMatrixID (model, LTSMIN_MATRIX_ACTIONS_READS);
    if (id == SI_INDEX_FAILED) {
        p_dm = GBgetDMInfo (model);
    } else {
        p_dm = GBgetMatrix (model, id);
    }

    int groups = dm_nrows( p_dm );
    int len = dm_ncols( p_dm );

    // guard to group dependencies
    sl_group_t* sl_guards = GBgetStateLabelGroupInfo(model, GB_SL_ALL);
    int guards = sl_guards->count;
    matrix_t        *p_sl = GBgetStateLabelInfo(model);
    // len is unchanged
    matrix_t guard_is_dependent;
    dm_create(&guard_is_dependent, guards, groups);
    for(int i=0; i < guards; i++) {
        for(int j=0; j < groups; j++) {
            for(int k=0; k < len; k++) {
                if (dm_is_set( p_sl, sl_guards->sl_idx[i], k ) && dm_is_set( p_dm_w, j, k )) {
                    dm_set( &guard_is_dependent, i, j );
                    break;
                }
            }
        }
    }
    ctx->guard_dep   = (ci_list **) dm_rows_to_idx_table(&guard_is_dependent);

    // extract inverse relation, transition group to guard
    matrix_t gg_matrix;
    dm_create(&gg_matrix, groups, guards);
    for(int i=0; i < groups; i++) {
        guard_t* g = GBgetGuard(model, i);
        HREassert(g != NULL, "GUARD RETURNED NULL %d", i);
        for(int j=0; j < g->count; j++) {
            dm_set(&gg_matrix, i, g->guard[j]);
        }
    }
    ctx->guard2group            = (ci_list **) dm_cols_to_idx_table(&gg_matrix);
    ctx->group2guard            = (ci_list **) dm_rows_to_idx_table(&gg_matrix);
    dm_free(&gg_matrix);

    // mark minimal necessary enabling set
    matrix_t *p_gnes_matrix = NO_NES ? NULL : GBgetGuardNESInfo(model);
    NO_NES = p_gnes_matrix == NULL;

    if (p_gnes_matrix != NULL) {
        HREassert (dm_nrows(p_gnes_matrix) == guards && dm_ncols(p_gnes_matrix) == groups);
        // copy p_gnes_matrix to gnes_matrix, then optimize it
        dm_copy(p_gnes_matrix, &ctx->gnes_matrix);
    } else {
        dm_create(&ctx->gnes_matrix, guards, groups);
        for(int i=0; i < guards; i++) {
            for(int j=0; j < groups; j++) {
                dm_set (&ctx->gnes_matrix, i, j);
            }
        }
    }
    // optimize nes
    // remove all transition groups that do not write to this guard
    for(int i=0; i < guards; i++) {
        for(int j=0; j < groups; j++) {
            // if guard i has group j in the nes, make sure
            // the group writes to the same part of the state
            // vector the guard reads from, otherwise
            // this value can be removed
            if (dm_is_set(&ctx->gnes_matrix, i, j)) {
                if (!dm_is_set(&guard_is_dependent, i, j))
                    dm_unset(&ctx->gnes_matrix, i, j);
            }
        }
    }
    ctx->guard_nes   = (ci_list **) dm_rows_to_idx_table(&ctx->gnes_matrix);

    // same for nds
    matrix_t *p_gnds_matrix = NO_NDS ? NULL : GBgetGuardNDSInfo(model);
    NO_NDS = p_gnds_matrix == NULL;

    if (p_gnds_matrix != NULL) {
        HREassert (dm_nrows(p_gnds_matrix) == guards && dm_ncols(p_gnds_matrix) == groups);
        // copy p_gnds_matrix to gnes_matrix, then optimize it
        dm_copy(p_gnds_matrix, &ctx->gnds_matrix);
    } else {
        dm_create(&ctx->gnds_matrix, guards, groups);
        for(int i=0; i < guards; i++) {
            for(int j=0; j < groups; j++) {
                dm_set (&ctx->gnds_matrix, i, j);
            }
        }
    }

    // optimize nds matrix
    // remove all transition groups that do not write to this guard
    for(int i=0; i < guards; i++) {
        for(int j=0; j < groups; j++) {
            // if guard i has group j in the nes, make sure
            // the group writes to the same part of the state
            // vector the guard reads from, otherwise
            // this value can be removed
            if (dm_is_set(&ctx->gnds_matrix, i, j)) {
                if (!dm_is_set(&guard_is_dependent, i, j))
                    dm_unset(&ctx->gnds_matrix, i, j);
            }
        }
    }
    ctx->guard_nds   = (ci_list **) dm_rows_to_idx_table(&ctx->gnds_matrix);
    ctx->group_nds   = (ci_list **) dm_cols_to_idx_table(&ctx->gnds_matrix);

    matrix_t nds;
    dm_create(&nds, groups, groups);
    for (int i = 0; i < groups; i++) {
        for (int j = 0; j < groups; j++) {
            if (guard_of(ctx, i, &ctx->gnds_matrix, j)) {
                dm_set( &nds, i, j);
            }
        }
    }
    ctx->nds     = (ci_list **) dm_rows_to_idx_table(&nds);
    ctx->ndsn     = (ci_list **) dm_cols_to_idx_table(&nds);

    // extract guard not co-enabled and guard-nes information
    // from guard may-be-co-enabled with guard relation:
    // for a guard g, find all guards g' that may-not-be co-enabled with it
    // then, for each g', mark all groups in gnce_matrix
    matrix_t *p_gce_matrix = NO_MC ? NULL : GBgetGuardCoEnabledInfo(model);
    NO_MC = p_gce_matrix == NULL;

    if (!NO_MC) {
        HREassert (dm_ncols(p_gce_matrix) == guards && dm_nrows(p_gce_matrix) == guards);
        dm_create(&ctx->gnce_matrix, guards, groups);
        dm_create(&ctx->nce, groups, groups);
        for (int g = 0; g < guards; g++) {
            // iterate over all guards
            for (int gg = 0; gg < guards; gg++) {
                // find all guards that may not be co-enabled
                if (dm_is_set(p_gce_matrix, g, gg)) continue;

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
    matrix_t *not_accords_with = NO_DNA ? NULL : GBgetDoNotAccordInfo(model);
    NO_DNA = not_accords_with == NULL;

    HREassert (NO_DNA || (dm_nrows(not_accords_with) == groups &&
                          dm_ncols(not_accords_with) == groups));

    // Combine Do Not Accord with dependency and other information
    dm_create(&ctx->not_accords_with, groups, groups);
    for (int i = 0; i < groups; i++) {
        for (int j = 0; j < groups; j++) {
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
                for (int k = 0; k < len; k++) {
                    if ((dm_is_set( p_dm_w, i, k) && dm_is_set( p_dm, j, k)) ||
                        (dm_is_set( p_dm, i, k) && dm_is_set( p_dm_w, j, k)) ) {
                        dm_set( &ctx->not_accords_with, i, j );
                        break;
                    }
                }
            }
        }
    }

    // set lookup tables
    ctx->not_accords = (ci_list **) dm_rows_to_idx_table(&ctx->not_accords_with);

    // free temporary matrices
    dm_free(&guard_is_dependent);

    matrix_t *commutes = GBgetCommutesInfo (model);
    if (POR_WEAK && (commutes == NULL || NO_NES || PINS_LTL)) {
        Print1 (info, "LTL used (unimplemented) or no dependency info for weak relations, switching to strong stubborn sets.");
        POR_WEAK = 0;
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
            Print1 (info, "No must-disable matrix available for weak sets");
        }

        matrix_t not_left_accords;
        dm_create(&not_left_accords, groups, groups);
        for (int i = 0; i < groups; i++) {
            for (int j = 0; j < groups; j++) {
                if (i == j) {
                    dm_set(&not_left_accords, i, j);
                } else {
                    if (must_disable != NULL && !NO_MDS) {
                        if (guard_of(ctx, i, must_disable, j)) {
                            continue;
                        }
                    }

                    if (guard_of(ctx, i, &ctx->gnes_matrix, j) ||
                            dm_is_set(&nds, j, i)) {
                        dm_set( &not_left_accords, i, j );
                        continue;
                    }

                    if ( dm_is_set(commutes, i , j) ) {
                        continue; // actions commute with each other
                    }

                    // is even dependent? Front-end might miss it.
                    for (int k = 0; k < len; k++) {
                        if ((dm_is_set( p_dm_w, i, k) && dm_is_set( p_dm, j, k)) ||
                            (dm_is_set( p_dm, i, k) && dm_is_set( p_dm_w, j, k)) ) {
                            dm_set( &not_left_accords, i, j );
                            break;
                        }
                    }
                }
            }
        }
        ctx->not_left_accords = (ci_list **) dm_rows_to_idx_table(&not_left_accords);
        ctx->not_left_accordsn= (ci_list **) dm_cols_to_idx_table(&not_left_accords);
    }

    // init por model
    Print1 (info, "Initializing dependency lookup table done.");
    GBsetContext (pormodel, ctx);

    GBsetNextStateLong  (pormodel, por_long);
    GBsetNextStateShort (pormodel, por_short);
    switch (alg) {
    case POR_SCC:
    case POR_SCC1: GBsetNextStateAll   (pormodel, por_scc_search_all);  break;
    case POR_HEUR: GBsetNextStateAll   (pormodel, por_beam_search_all); break;
    case POR_DEL:  GBsetNextStateAll   (pormodel, por_deletion_all);     break;
    default: Abort ("Unknown POR algorithm: '%s'", key_search(por_algorithm, alg));
    }

    GBinitModelDefaults (&pormodel, model);

    if (GBgetPorGroupVisibility(pormodel) == NULL) {
        // reserve memory for group visibility, will be provided by ltl layer or tool
        ctx->group_visibility = RTmallocZero( groups * sizeof(int) );
        GBsetPorGroupVisibility  (pormodel, ctx->group_visibility);
    } else {
        ctx->group_visibility = GBgetPorGroupVisibility(pormodel);
    }
    if (GBgetPorStateLabelVisibility(pormodel) == NULL) {
        // reserve memory for group visibility, will be provided by ltl layer or tool
        ctx->label_visibility = RTmallocZero( guards * sizeof(int) );
        GBsetPorStateLabelVisibility  (pormodel, ctx->label_visibility);
    } else {
        ctx->label_visibility = GBgetPorStateLabelVisibility (pormodel);
    }

    ctx->dynamic_visibility = RTmallocZero( groups * sizeof(int) );
    ctx->enabled_list = RTmallocZero ((groups + 1) * sizeof(int));
    ctx->visible_list = RTmallocZero ((groups + 1) * sizeof(int));
    ctx->nds_list[0] = RTmallocZero ((guards + 1) * sizeof(int));
    ctx->nds_list[1] = RTmallocZero ((guards + 1) * sizeof(int));
    ctx->marked_list = NULL;
    ctx->label_list = NULL;
    ctx->seed = 73783467;
    ctx->random = RTmallocZero (guards*2 * sizeof(int));
    if (!DYN_RANDOM)
        for (int i = 0; i < guards*2; i++) ctx->random[i] = i;

    int                 s0[len];
    GBgetInitialState (model, s0);
    GBsetInitialState (pormodel, s0);

    // after complete initialization
    bs_init_beam_context (pormodel);

    // SCC search
    ctx->scc_ctx = create_scc_ctx (ctx);

    ctx->del_ctx = create_del_ctx (ctx);

    return pormodel;
}

