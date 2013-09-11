#include <hre/config.h>

#include <limits.h>
#include <stdlib.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins2pins-por.h>
#include <pins-lib/pins-util.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>

static int NO_HEUR = 0;
static int NO_DNA = 0;
static int NO_NES = 0;
static int NO_NDS = 0;
static int NO_MC = 0;
static int NO_DYNLAB = 0;
static int NO_V = 0;
static int NO_L12 = 0;
static int USE_SCC = 0;
static int PREFER_NDS = 0;
static int RANDOM = 0;
static int DYN_RANDOM = 0;
static int WEAK = 0; // TODO: implement combination with LTL
static int DELETION = 0;

struct poptOption por_options[]={
    { "por" , 'p' , POPT_ARG_VAL , &PINS_POR , PINS_POR_ON , "enable partial order reduction" , NULL },

    /* HIDDEN OPTIONS FOR EXPERIMENTATION */

    { "check-por" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &PINS_POR , PINS_POR_CHECK , "verify partial order reduction peristent sets" , NULL },
    { "no-dna" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_DNA , 1 , "without DNA" , NULL },
    { "no-nes" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_NES , 1 , "without NES" , NULL },
    { "no-heur" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_HEUR , 1 , "without heuristic" , NULL },
    { "no-nds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_NDS , 1 , "without NDS (for dynamic label info)" , NULL },
    { "no-mc" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_MC , 1 , "without MC (for NDS)" , NULL },
    { "no-dynamic-labels" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_DYNLAB , 1 , "without dynamic labels" , NULL },
    { "no-V" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_V , 1 , "without V proviso, instead use Peled's visibility proviso, or V'     " , NULL },
    { "no-L12" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_L12 , 1 , "without L1/L2 proviso, instead use Peled's cycle proviso, or L2'   " , NULL },
    { "por-scc" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &USE_SCC , 1 , "use an incomplete SCC-based stubborn set algorithm" , NULL },
    { "deletion" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &DELETION , 1 , "use the stubborn set deletion algorithm" , NULL },
    { "prefer-nds" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &PREFER_NDS , 1 , "prefer MC+NDS over NES" , NULL },
    { "por-random" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &RANDOM , 1 , "randomize necessary sets (static)" , NULL },
    { "por-dynamic" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &DYN_RANDOM , 1 , "randomize necessary sets (dynamic)" , NULL },
    { "weak" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &WEAK , 1 , "Weak stubborn set theory" , NULL },
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
} scc_context_t;

static const int SCC_SCC = -1;
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
        guard_t* gt = GBgetGuard (model, i);
        for (int j = 0; j < gt->count; j++) {
            if (ctx->label_status[gt->guard[j]] == 0) {
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
    // Add do-not-accord for LTL and accord left for not LTL:
    for (int j=0; j < ctx->not_accords_tg_tg[group]->count; j++) {
        int dependent_group = ctx->not_accords_tg_tg[group]->data[j];
        select_group (ctx, dependent_group);
    }

    if (!WEAK) return;

    // In the weak stubborn set, the selected enabled group might become
    // disabled by some non-stubborn transition. This is allowed as long
    // as it is never enabled again (by some non-stubborn transition).
    // In other words: either a NES or an NDS need to be added for each guard.
    search_context *s = &ctx->search[ ctx->search_order[0] ];

    int is_key = 1; // key transitions have all their nds's included
    for (int g = 0; g < ctx->group2guard[group]->count; g++) {
        int guard = ctx->group2guard[group]->data[g];

        int disabled_score = s->nes_score[guard];
        int enabled_score = s->nes_score[guard + ctx->nguards];
        int ns = disabled_score < enabled_score ? guard : guard+ctx->nguards;
        is_key &= ns >= ctx->nguards;

        for (int k=0; k < ctx->ns[ns]->count; k++) {
            int ns_group = ctx->ns[ns]->data[k];
            select_group (ctx, ns_group);
        }
    }
    s->has_key |= is_key;
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

static inline void
scc_expand (por_context* ctx, int group)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    ci_list *successors;
    if (ctx->group_status[group] & GS_DISABLED) {
        int ns = find_cheapest_ns (ctx, scc->search, group);
        successors = ctx->ns[ns];
    } else {
        successors = ctx->not_accords_tg_tg[ group ];
    }
    for (int j=0; j < successors->count; j++) {
        int next_group = successors->data[j];
        if (scc->group_index[next_group] != SCC_SCC) {
            scc_state_t next = { next_group, -1 };
            dfs_stack_push (scc->stack, (int*)&next);
        }
    }
}

static inline void
scc_root (por_context* ctx, int root)
{
    scc_state_t *x;
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    scc->scc_list->count = 0;
    scc->stubborn_list[1]->count = 0;
    do {x = (scc_state_t *)dfs_stack_pop(scc->tarjan);
        if (!(ctx->group_status[x->group] & GS_DISABLED)) {
            scc->stubborn_list[1]->data[ scc->stubborn_list[1]->count++ ] = x->group;
        }
        scc->scc_list->data[ scc->scc_list->count++ ] = x->group;
        scc->group_index[x->group] = SCC_SCC; // mark SCC
    } while (x->group != root);
    if (scc->stubborn_list[1]->count > 0 &&
        scc->stubborn_list[1]->count < scc->stubborn_list[0]->count) {
        swap (scc->stubborn_list[0], scc->stubborn_list[1]);
    }
}

static void
scc_search (por_context* ctx)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;
    scc_state_t *state, *pred;

    while (true) {
        state = (scc_state_t *)dfs_stack_top(scc->stack);
        if (state != NULL) {
            if (scc->group_index[state->group] == SCC_SCC) {
                dfs_stack_pop (scc->stack);
                continue;
            }

            if (scc->group_index[state->group] == SCC_NEW) {
                HREassert (state->lowest == -1);
                // assign index
                scc->group_index[state->group] = ++scc->index;
                state->lowest = scc->index;
                // add to tarjan stack
                dfs_stack_push (scc->tarjan, &state->group);

                // push successors
                dfs_stack_enter (scc->stack);
                scc_expand (ctx, state->group);
            } else {
                pred = (scc_state_t *)dfs_stack_peek_top (scc->stack, 1);
                if (scc->group_index[state->group] < pred->lowest) {
                    pred->lowest = scc->group_index[state->group];
                }
                dfs_stack_pop (scc->stack);
            }
        } else {
            dfs_stack_leave (scc->stack);
            state = (scc_state_t *)dfs_stack_pop (scc->stack);

            // detected an SCC
            if (scc->group_index[state->group] == state->lowest) {
                scc_root (ctx, state->group);
                if (scc->stubborn_list[0]->count > 0 &&
                        scc->stubborn_list[0]->count != INT32_MAX) break;
            }
            update_ns_scores (ctx, scc->search, state->group); // remove from NS scores

            if (dfs_stack_nframes(scc->stack) > 0) {
                // (after recursive return call) update index
                pred = (scc_state_t *)dfs_stack_peek_top (scc->stack, 1);
                if (state->lowest < pred->lowest) {
                    pred->lowest = state->lowest;
                }
            } else {
                HREassert (scc->stubborn_list[0]->count > 0 &&
                           scc->stubborn_list[0]->count != INT32_MAX);
                break;
            }
        }
    }
}

static void
empty_stack (dfs_stack_t stack)
{
    while (dfs_stack_size(stack) != 0) {
        void *s = dfs_stack_pop (stack);
        if (s == NULL) {
            dfs_stack_leave (stack);
        }
    }
}

static void
scc_analyze (por_context* ctx)
{
    scc_context_t *scc = (scc_context_t *)ctx->scc_ctx;

    scc->index = 0;
    empty_stack (scc->stack);
    empty_stack (scc->tarjan);
    scc->stubborn_list[0]->count = INT32_MAX;

    for (int j=0; j < ctx->enabled_list->count; j++) {
        int group = ctx->enabled_list->data[j];
        if (scc->group_index[group] == SCC_NEW) {
            scc_state_t next = { group, -1 };
            dfs_stack_push (scc->stack, (int*)&next);
            scc_search (ctx);
            if (scc->stubborn_list[0]->count > 0 &&
                    scc->stubborn_list[0]->count != INT32_MAX) {
                return;
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

typedef struct ltl_hook_context {
    TransitionCB    cb;
    void*           user_context;
    int             por_proviso_true_cnt;
    int             por_proviso_false_cnt;
    int             force_proviso_true;     // feedback to algorithm that proviso already holds
} ltl_hook_context_t;

void ltl_hook_cb (void*context,transition_info_t *ti,int*dst) {
    ltl_hook_context_t* infoctx = (ltl_hook_context_t*)context;
    transition_info_t ti_new = GB_TI (ti->labels, ti->group);
    ti_new.por_proviso = infoctx->force_proviso_true;
    infoctx->cb(infoctx->user_context, &ti_new, dst);
    // catch transition info status
    if (infoctx->force_proviso_true || ti_new.por_proviso) {
        infoctx->por_proviso_true_cnt++;
    } else {
        infoctx->por_proviso_false_cnt++;
    }
}

static inline int
emit_new_selected (por_context* ctx, ltl_hook_context_t* ltlctx, int* src)
{
    search_context *s = &ctx->search[ctx->search_order[0]];
    int c = 0;
    for (int z = 0; z < ctx->enabled_list->count; z++) {
        int i = ctx->enabled_list->data[z];
        if (s->emit_status[i] & ES_EMITTED) continue;

        if ((s->emit_status[i] & ES_SELECTED) || s->score >= ctx->emit_limit) {
            s->emit_status[i] |= ES_EMITTED;
            c += GBgetTransitionsLong (ctx->parent, i, src, ltl_hook_cb, ltlctx);
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
        ltl_hook_context_t ltlctx = {cb, uctx, 0, 0, 1};
        return GBgetTransitionsAll(ctx->parent, src, ltl_hook_cb, &ltlctx);
    // otherwise: try the persistent set, but enforce that all por_proviso flags are true
    } else {
        ltl_hook_context_t ltlctx = {cb, uctx, 0, 0, 0};
        ltlctx.force_proviso_true = !NO_L12 && check_L1_L2_proviso (ctx);

        int emitted = emit_new_selected (ctx, &ltlctx, src);

        // emit more if we need to fulfill a liveness / safety proviso
        if ( ( PINS_LTL && ltlctx.por_proviso_false_cnt != 0) ||
             (!PINS_LTL && ltlctx.por_proviso_true_cnt  == 0) ) {

            if (!NO_L12) {
                ctx->beam_used = 1; // fix to current (partly emitted) search ctx

                // enforce L2 (include all visible transitions)
                select_all_visible (ctx);
                bs_analyze (ctx);

                ltlctx.force_proviso_true = check_L1_L2_proviso (ctx);
                // enforce L1 (one invisible transition)
                // not to be worried about when using V'
                if (PINS_LTL && !NO_V && !ltlctx.force_proviso_true) {
                    select_one_invisible (ctx);
                    bs_analyze (ctx);
                }
            } else {
                s->score = ctx->emit_limit; // force all enabled
            }
            emitted += emit_new_selected (ctx, &ltlctx, src);
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
        ltl_hook_context_t ltlctx = {cb, uctx, 0, 0, 1};
        return GBgetTransitionsAll(ctx->parent, src, ltl_hook_cb, &ltlctx);
    } else {
        ltl_hook_context_t ltlctx = {cb, uctx, 0, 0, 0};
        ltlctx.force_proviso_true = !NO_L12 && check_L1_L2_proviso (ctx);

        int c = 0;
        for (int z = 0; z < scc->stubborn_list[0]->count; z++) {
            int i = scc->stubborn_list[0]->data[z];
            c += GBgetTransitionsLong (ctx->parent, i, src, ltl_hook_cb, &ltlctx);
        }
        return c;
    }
}

/**
 * Default functions for long and short
 * Note: these functions don't work for partial order reduction,
 *       because partial order reduction selects a subset of the transition
 *       group and doesn't know beforehand whether to emit this group or not
 */
static int
por_long (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self;
    (void)group;
    (void)src;
    (void)cb;
    (void)user_context;
    Abort ("Using Partial Order Reduction in combination with --grey or -reach? Long call failed.");
}

static int
por_short (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self;
    (void)group;
    (void)src;
    (void)cb;
    (void)user_context;
    Abort ("Using Partial Order Reduction in combination with -reach or --cached? Short call failed.");
}

static inline void
ensure_key (por_context* ctx)
{
    // if no enabled transitions, return directly
    if (!WEAK || ctx->beam_used == 0) return;

    while ( 1 ) {
        search_context *s = &ctx->search[ctx->search_order[0]]; // search context

        if (s->has_key) return; // OK

        size_t min_score = INT32_MAX;
        int min_group = -1;
        for (int i = 0; i < ctx->enabled_list->count; i++) {
            int group = ctx->enabled_list->data[i];
            if ( !((s->emit_status[group] & ES_SELECTED) ||
                    s->score >= ctx->emit_limit) )
                continue;

            // check open NDSs
            size_t nds_score = 0;
            for (int g = 0; g < ctx->group2guard[group]->count; g++) {
                int nds = ctx->group2guard[group]->data[g] + ctx->nguards;
                nds_score += s->nes_score[nds];
            }

            if (nds_score == 0) return; // OK (all NDS's are in the SS)

            if (nds_score < min_score) {
                min_score = nds_score;
                min_group = group;
            }
        }

        // add all the NDS's for the transition for which it is cheapest
        HREassert (min_group != -1);
        for (int g = 0; g < ctx->group2guard[min_group]->count; g++) {
            int nds = ctx->group2guard[min_group]->data[g] + ctx->nguards;

            // add the selected ndss to work
            for(int k=0; k < ctx->ns[nds]->count; k++) {
                int group = ctx->ns[nds]->data[k];
                select_group (ctx, group);
            }
        }

        bs_analyze (ctx); // may select a different search context!
    }
}

/**
 * Implementation of the deletion algorithm from an algorithm from Henri Hansen
 * obtained via private communication.
 * The elements of the set T of all enabled and disabled transitions (A U O) and
 * disabled guards G are maintained as integers as follows:
 * for t in T:
 * t in {0, ... ngroups-1} <=> t is a transition of group t
 * t in {ngroups, ... ngroups+nguards } <=> t is the guard (t - ngroups)
 *
 * The guards here are actually extended with the necessary disabling and
 * not coenabled relations, so instead of disabled guards, we have necessary
 * sets (ns) which are either enabling directly or indirectly because they
 * are not coenabled and need to be disabled.
 * ns in {1, ... nguards -1, nguards, ... nguards*2 - 1}
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
    DEL_T,  // set
    DEL_K,  // count set (stack content may be corrupted)
    DEL_C,  // stack
    DEL_Z,  // stack set
    DEL_R,  // set
    DEL_KP, // stack
    DEL_TP, // stack
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
del_G (por_context* ctx, int u)
{
    return u >= ctx->ngroups;
}

static inline bool
del_O (por_context* ctx, int u)
{
    return u < ctx->ngroups && ctx->group_status[u] == GS_DISABLED;
}

static inline bool
del_A (por_context* ctx, int u)
{
    return u < ctx->ngroups && ctx->group_status[u] == GS_ENABLED;
}

static inline bool
del_has (del_context_t *del, del_t set, int u)
{
    return (del->set[ u ] & (1<<set)) != 0;
}

static bool
del_nGT (por_context* ctx, int u)
{
    del_context_t *del = ctx->del_ctx;
    return !(del_G(ctx,u) && del_has(del,DEL_T,u));
}

static inline int
del_count (del_context_t *del, del_t set)
{
    return del->lists[set]->count;
}

typedef bool (*check_f)(por_context* ctx, int u);

static inline bool
del_apply_all_succ (por_context* ctx, int u, check_f check)
{
    if (del_G(ctx,u)) {
        // for all g in G exists g ~~> b with b in O U A (including NDS)
        int guard = u - ctx->ngroups;
        for (int i = 0; i < ctx->ns[guard]->count; i++) {
            int x = ctx->ns[guard]->data[i];
            if (!check(ctx,x)) return false;
        }
    } else { // A U O:
        if (del_A(ctx, u)) {
            // for all a in A exists if (a,b) in C then a~~>b
            for (int i = 0; i < ctx->not_commutes[u]->count; i++) {
                int x = ctx->not_commutes[u]->data[i];
                if (!check(ctx,x)) return false;
            }
        } else { // del_O:
            // for all o = (g_1, .. ,g_k, e) in O exists g_i s.t. o ~~> g_i
            // (including NCE)
            for (int i = 0; i < ctx->group_has[u]->count; i++) {
                int ns = ctx->group_has[u]->data[i];
                if ( ((ns  < ctx->nguards && ctx->label_status[ns] == 0) ||
                      (ns >= ctx->nguards && ctx->label_status[ns - ctx->nguards] != 0)) &&
                        !check(ctx,ns+ctx->ngroups)) return false;
            }
        }
        // for all a,b in A U O exists a ~~> b iff (a,b) in NDS
        for (int i = 0; i < ctx->nds[u]->count; i++) {
            int x = ctx->nds[u]->data[i];
            if (!check(ctx,x)) return false;
        }
    }
    return true;
}

static inline bool
del_apply_all_pred (por_context* ctx, int u, check_f check)
{
    if (del_G(ctx,u)) {
        // for all o = (g_1, .. ,g_k, e) in O exists g_i ~i~> o (including NCE)
        int ns = u - ctx->ngroups;
        for (int i = 0; i < ctx->group_hasn[ns]->count; i++) {
            int x = ctx->group_hasn[ns]->data[i];
            if (del_O(ctx,x) && !check(ctx,x)) return false;
        }
    } else { // A U O:
        // for all a in A exists if (a,b) in C then b~i~>a
        for (int i = 0; i < ctx->not_commutes[u]->count; i++) { // symmetric relation
            int x = ctx->not_commutes[u]->data[i];
            if (del_A(ctx,x) && !check(ctx,x)) return false;
        }

        // for all g in G exists b ~i~> g with b in O U A (including NDS)
        for (int i = 0; i < ctx->group2ns[u]->count; i++) {
            int ns = ctx->group2ns[u]->data[i];
            if ( ((ns  < ctx->nguards && ctx->label_status[ns] == 0) ||
                  (ns >= ctx->nguards && ctx->label_status[ns - ctx->nguards] != 0)) &&
                    !check(ctx,ns+ctx->ngroups)) return false;
        }

        // for all a,b in A U O exists a ~i~> b iff (a,b) in NDS
        for (int i = 0; i < ctx->ndsn[u]->count; i++) {
            int x = ctx->ndsn[u]->data[i];
            if (!check(ctx,x)) return false;
        }
    }
    return true;
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

// assumes Z \ T = {} (Z is not reevaluated as in deletion algorithm)
static bool
del_add_inT_toZ (por_context *ctx, int u)
{
    del_context_t *del = ctx->del_ctx;
    if (del_has(del, DEL_T, u))
        del_push_new (del, DEL_Z, u);
    return true;
}

static bool
del_rem_fromK (por_context *ctx, int u)
{
    del_context_t *del = ctx->del_ctx;
    if (del_rem(del, DEL_K, u)) del_push (del, DEL_KP, u);
    return true;
}


static void
deletion_setup (model_t model, por_context* ctx, int* src)
{
    por_init_transitions (model, ctx, src);
    if (ctx->enabled_list->count == 0) return;

    del_context_t *del = ctx->del_ctx;

    // K = {}; C := {}; Z := {}; KP := {}; TP := {}; R := {}
    // T := A U G U O;
    for (int i = 0; i < ctx->ngroups + NS_SIZE(ctx); i++) {
        del->set[i] = 0;
        del_add (del, DEL_T, i);
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
}

static inline void
deletion_analyze (por_context* ctx)
{
    if (ctx->enabled_list->count == 0) return;
    del_context_t *del = ctx->del_ctx;

    Warning (debug, "Deletion init: |C| = %d \t|K| = %d", del_count(del, DEL_C), del_count(del, DEL_K));

    // while C != {} /\ K != empty
    while (del_count(del, DEL_C) != 0 && del_count(del, DEL_K) != 0) {
        // C := C \ {v} for some v in C
        int v = del_pop (del, DEL_C);

        // Z := {v}
        del_push_new (del, DEL_Z, v);
        Warning (debug, "\nDeletion from v = %d", v);

        // search the deletion space:
        // while Z != {} /\ K != {}
        bool RfromT = false;
        Printf (debug, "Removing from T: ");
        while (del_count(del, DEL_Z) != 0 && del_count(del, DEL_K) != 0) {
            // Z := Z \ {u} for some u in Z
            int u = del_pop (del, DEL_Z);

            // if u in A U G \/ u* n G n T = {}
            if (del_A(ctx,u) || del_G(ctx,u) ||
                    del_apply_all_succ(ctx,u,del_nGT)) {

                if (del_has(del,DEL_R,u)) {
                    RfromT = true;
                    break;
                }

                // K := K \ {u}; T := T \ {u};
                if (del_rem(del, DEL_K, u)) del_push (del, DEL_KP, u);
                if (del_rem(del, DEL_T, u)) {
                    del_push (del, DEL_TP, u);
                    Printf (debug, "%d, ", u);
                }

                // Z := (Z U *u) n T
                del_apply_all_pred (ctx, u, del_add_inT_toZ);       (void) DEL_T; (void) DEL_Z;

                // if u not in G; K := K \ u*
                if (!del_G(ctx,u)) {
                    del_apply_all_succ (ctx, u, del_rem_fromK);     (void) DEL_K;
                }
            }
        }
        Printf (debug, "\n");

        while (del_count(del, DEL_Z) != 0) del_pop (del, DEL_Z);

        // Reverting deletions if necessary
        // if K == {} \/ R \ T != {}
        if (del_count(del, DEL_K) == 0 || RfromT) {
            Warning (debug, "Deletion rollback: |T'| = %d \t|K'| = %d", del_count(del, DEL_TP), del_count(del, DEL_KP));
            while (del_count(del, DEL_KP) != 0) {
                int x = del_pop (del, DEL_KP);
                bool seen = del_push_new (del, DEL_K, x);
                HREassert (seen, "DEL_K messed up");
            }
            while (del_count(del, DEL_TP) != 0) {
                int x = del_pop (del, DEL_TP);
                del->set[x] = del->set[x] | (1<<DEL_T);
            }
        } else {
            del_clear (del, DEL_KP);
            del_clear (del, DEL_TP);
        }
    }
}

static inline int
deletion_emit_new (por_context* ctx, ltl_hook_context_t* ltlctx, int* src)
{
    del_context_t *del = ctx->del_ctx;
    int c = 0;
    for (int z = 0; z < ctx->enabled_list->count; z++) {
        int i = ctx->enabled_list->data[z];
        if (del_has(del,DEL_T,i) && !del_has(del,DEL_R,i)) {
            del->set[i] |= 1<<DEL_R;
            c += GBgetTransitionsLong (ctx->parent, i, src, ltl_hook_cb, ltlctx);
        }
    }
    return c;
}

static inline int
deletion_emit (por_context* ctx, int* src, TransitionCB cb, void* uctx)
{
    ltl_hook_context_t ltlctx = {cb, uctx, 0, 0, 0};
    ltlctx.force_proviso_true = !NO_L12 && check_L1_L2_proviso (ctx);

    int emitted = deletion_emit_new (ctx, &ltlctx, src);

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
    ensure_key (ctx);
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

    matrix_t           *p_dm = GBgetDMInfo (model);
    matrix_t           *p_dm_w = GBgetDMInfoWrite (model);

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
                if (!dm_is_set(p_gce_matrix, g, gg)) {
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
        }
        ctx->guard_nce             = (ci_list **) dm_rows_to_idx_table(&ctx->gnce_matrix);
        ctx->group_nce             = (ci_list **) dm_rows_to_idx_table(&ctx->nce);
    }

    // extract accords with matrix
    matrix_t *not_accords_with = NO_DNA ? NULL : GBgetDoNotAccordInfo(model);
    NO_DNA = not_accords_with == NULL;

    HREassert (NO_DNA || (dm_nrows(not_accords_with) == groups &&
                          dm_ncols(not_accords_with) == groups));

    WEAK = PINS_LTL ? 0 : WEAK;

    // extract COMMUTES MATRIX FOR WEAK LTL
    matrix_t *commutes = GBgetCommutesInfo (model);
    WEAK &= commutes != NULL;

    // Combine Do Not Accord with dependency and other information
    dm_create(&ctx->not_accords_with, groups, groups);
    for(int i=0; i < groups; i++) {
        for(int j=0; j < groups; j++) {
            if (i == j) {
                dm_set(&ctx->not_accords_with, i, j);
            } else {

                if ( !NO_MC && dm_is_set(&ctx->nce, i , j) ) {
                    continue; // transitions never coenabled!
                }

                if (WEAK) {
                    for (int g = 0; g < ctx->group2guard[j]->count; g++) {
                        if (dm_is_set(&ctx->gnds_matrix, ctx->group2guard[j]->data[g], i)) {
                            dm_set( &ctx->not_accords_with, i, j );
                            continue;
                        }
                    }

                    if ( dm_is_set(commutes, i , j) ) {
                        continue; // actions commute with each other
                    }
                } else {
                    if ( !NO_DNA && !dm_is_set(not_accords_with, i , j) ) {
                        continue; // transitions accord with each other
                    }
                }

                // is dependent?
                for (int k=0; k < len; k++)
                {
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
    ctx->not_accords_tg_tg     = (ci_list **) dm_rows_to_idx_table(&ctx->not_accords_with);
    ctx->guard_nes             = (ci_list **) dm_rows_to_idx_table(&ctx->gnes_matrix);
    ctx->guard_nds             = (ci_list **) dm_rows_to_idx_table(&ctx->gnds_matrix);
    ctx->guard_dep             = (ci_list **) dm_rows_to_idx_table(&guard_is_dependent);

    // free temporary matrices
    dm_free(&guard_is_dependent);


    // init por model
    Print1 (info, "Initializing dependency lookup table done.");
    GBsetContext (pormodel, ctx);

    GBsetNextStateLong  (pormodel, por_long);
    GBsetNextStateShort (pormodel, por_short);
    if (USE_SCC) {
        GBsetNextStateAll   (pormodel, por_scc_search_all);
    } else if (DELETION) {
        GBsetNextStateAll   (pormodel, por_deletion_all);

        HREassert (commutes != NULL, "No commutes matrix");
        matrix_t not_commutes;
        dm_create(&not_commutes, groups, groups);
        for (int i = 0; i < groups; i++) {
            for (int j = 0; j < groups; j++) {
                if (i == j) {
                    dm_set(&not_commutes, i, j);
                } else {
                    if ( dm_is_set(commutes, i , j) ) {
                        continue; // actions commute with each other
                    }

                    // is dependent?
                    for (int k = 0; k < len; k++)
                    {
                        if ((dm_is_set( p_dm_w, i, k) && dm_is_set( p_dm, j, k)) ||
                            (dm_is_set( p_dm, i, k) && dm_is_set( p_dm_w, j, k)) ) {
                            dm_set( &not_commutes, i, j );
                            break;
                        }
                    }
                }
            }
        }
        ctx->not_commutes     = (ci_list **) dm_rows_to_idx_table(&not_commutes);

        matrix_t nds;
        dm_create(&nds, groups, groups);
        for (int i = 0; i < groups; i++) {
            for (int j = 0; j < groups; j++) {
                for (int g = 0; g < ctx->group2guard[j]->count; g++) {
                    int guard = ctx->group2guard[j]->data[g];
                    if (dm_is_set (&ctx->gnds_matrix, guard, i)) {
                        dm_set( &nds, i, j );
                        continue;
                    }
                }
            }
        }
        ctx->nds     = (ci_list **) dm_rows_to_idx_table(&nds);
        ctx->ndsn     = (ci_list **) dm_cols_to_idx_table(&nds);
    } else {
        GBsetNextStateAll   (pormodel, por_beam_search_all);
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

