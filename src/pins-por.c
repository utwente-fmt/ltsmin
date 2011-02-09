#include <config.h>
#include <stdlib.h>
#include <limits.h>

#include "dm/dm.h"
#include "greybox.h"
#include "runtime.h"
#include "limits.h"


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
    Fatal(1,error,"Using Partial Order Reduction in combination with --grey or --reach? Long call failed.");
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
    Fatal(1,error,"Using Partial Order Reduction in combination with --reach or --cached? Short call failed.");
}


/**
 * Beam search algorithm for persistent sets
 * Using the NES and NDS information
 */
typedef enum {
    GS_DISABLED     = 0x01,
    GS_VISIBLE      = 0x02,
    GS_CLOSED       = 0x04,
    GS_INACTIVE     = 0x08,
} group_status_t;

typedef enum {
    ES_SELECTED     = 0x01,
    ES_READY        = 0x02,
    ES_EMITTED      = 0x04,
} emit_status_t;

/**
 * ci = count, integer
 * frequently used to setup mappings
 */
typedef struct ci_list
{
    int count;
    int data[];
} ci_list;

/**
 * The analysis has global context (por_context)
 * and multiple search contexts (search_context).
 * Each search context is a search in a different state
 * started form a different initial transition
 * The beam search will always start working on the
 * search context with the lowest score.
 */
typedef struct search_context
{
    emit_status_t *emit_status;                     // status of each transition group
    int *work;                                      // list of size n+1, enabled transitions start at 0 upwards,
                                                    // disabled transitions start at n downwards
                                                    // [enabled0, enabled1, .. | free | workn-1, workn]
    int  work_enabled;                              // number of enabled groups in work
    int  work_disabled;                             // number of disabled groups in work

    int  score;                                     // search weight
    int *nes_score;                                 // nes score
} search_context;


/**
 * Additional context with por_model returned by this layer
 * Contains dependency relation, co-enabled information
 * of guards, necessary enabling/disabling sets (nes/nds) etc
 */
typedef struct por_context {
    model_t             parent;                     // parent PINS model
    int                 nguards;                    // number of guards
    matrix_t            is_dependent;               // is dependent matrix groups to groups
    matrix_t            guard_is_dependent;         // is dependent matrix guards to groups
    int**               is_dependent_tg_tg;         // mapping from transition group to dependent transition groups
    int**               is_dependent_guard_tg;      // mapping from guard to dependent transition groups
    int**               guard_tg;                   // mapping from guard to transition group
    int**               guard_nce;                  // mapping from guards to transition groups that may not be co-enabled
    int**               guard_nes;                  // transition groups that form a nes for a guard (guard -> [t1, t2, t..])
    int**               guard_nds;                  // transition groups that form a nds for a guard

    /**
     * The global data used for the search
     * This data is setup one time for each state that is processed
     */
    int             *guard_status;              // status of the guards in current state
    group_status_t  *group_status;              // status of the transition groups in the current state
    int             *group_score;               // score assigned to each group by heuristic function
    int              beam_width;                // maximum width of the beam search
    int              beam_used;                 // number of search contexts in use

    // global nes/nds
    int             *nes_score;                 // Template for the nes_score (TODO: check)
    ci_list        **ns;                        // nes/nds combined
    ci_list        **group_in;                  // mapping group to each nes/nds in which it is used
    ci_list        **group_has;                 // mapping group to each nes/nds for it

    int              emit_limit;                // maximum number of transition groups that can be emitted
    int              emit_score;                // directly emit when search finishes with this score
    int              emitted;                   // number of already emitted transitions

    // location in search array (extra indirection for quick switching between contexts)
    int             *search_order;
    search_context  *search;                    // context for each search

    // LTL specific
    int*             group_visibility;
} por_context;

/**
 * Function used to setup the beam search
 * Initialize and allocate all memory that is reused all the time
 * Setup pointers to functions used in beam search
 */
static void* bs_init_context(model_t model)
{
    // get number of transition groups
    int n = dm_nrows(GBgetDMInfo(model));
    // get the number of guards
    int n_guards = (GBgetStateLabelGroupInfo(model, GB_SL_GUARDS))->count;

    // setup a fixed maximum of number of search contexts
    // since we use at most one search context per transition group, use n
    const int BEAM_WIDTH = n;
    por_context* context = (por_context*) GBgetContext(model);
    // set number of guards used
    context->nguards = n_guards;
    // alloc search contexts
    context->search = RTmallocZero( BEAM_WIDTH * sizeof(search_context) );
    // set #emitted to zero, meaning that a new analysis should start
    context->emitted = 0;
    // init group status array
    context->group_status = RTmallocZero(n * sizeof (group_status_t) );
    context->group_score  = RTmallocZero(n * sizeof(int) );
    // init guard_status array
    context->guard_status = RTmallocZero(n_guards * sizeof(int) );

    // init beam_width/beam_used
    context->beam_width = BEAM_WIDTH;
    context->beam_used = BEAM_WIDTH;

    // init search_order;
    context->search_order = RTmallocZero(BEAM_WIDTH * sizeof(int) );

    // get por_context
    por_context* pctx = (por_context *)GBgetContext(model);

    // init nes score template
    context->nes_score = RTmallocZero( (n_guards * 2) * sizeof(int) );

    // get local variables pointing to nes/nds
    ci_list** nes = (ci_list**)pctx->guard_nes;
    ci_list** nds = (ci_list**)pctx->guard_nds;

    // setup global group_in/group_has relation
    // idea, combine nes and nds in one data structure (ns, necessary set)
    // each guard is either disabled (use nes) or enabled (nds)
    // setup: ns = [0...n_guards-1] nes, [n_guards..2n_guards-1] nds
    // this way the search algorithm can skip the nes/nds that isn't needed based on
    // guard_status, using <n or >=n as conditions
    matrix_t group_in;
    dm_create(&group_in, n_guards * 2, n); // mapping from nes/nds (2*n_guards) to transition group (n)
    for(int i=0; i < n_guards; i++) {
        for(int j=0; j < nes[i]->count; j++) {
            dm_set(&group_in, i, nes[i]->data[j]);
        }
        for(int j=0; j < nds[i]->count; j++) {
            dm_set(&group_in, i+n_guards, nds[i]->data[j]);
        }
    }
    // build tables ns, and group in
    context->ns = (ci_list**) dm_rows_to_idx_table(&group_in);
    context->group_in = (ci_list**) dm_cols_to_idx_table(&group_in);
    dm_free(&group_in);

    // group has relation
    // mapping [0...n_guards-1] disabled guard (nes)
    // mapping [n_guards..2*n_guards-1] enabled guard (nds)
    // group has relation is more difficult because nds
    // needs not co-enabled info and transition groups

    // get local variables pointing to mappings guard->tg, guard->nce
    ci_list** g_tg = (ci_list**)pctx->guard_tg;
    ci_list** g_nce = (ci_list**)pctx->guard_nce;

    // setup group has relation
    matrix_t group_has;
    dm_create(&group_has, n, n_guards * 2);
    for(int i=0; i < n_guards; i++) {
        // nes
        for( int k=0; k < g_tg[i]->count; k++) {
            dm_set(&group_has, g_tg[i]->data[k], i);
        }
        // nds
        for( int k=0; k < g_nce[i]->count; k++) {
            dm_set(&group_has, g_nce[i]->data[k], i+n_guards);
        }
    }
    // build table group has
    context->group_has = (ci_list**) dm_rows_to_idx_table(&group_has);
    dm_free(&group_has);

    // init for each search context
    for(int i=0 ; i < BEAM_WIDTH; i++) {
        // init default search order
        context->search_order[i] = i;
        // init emit status
        context->search[i].emit_status = RTmallocZero( n * sizeof(emit_status_t) );
        // init work_array
        // note, n+1 to initialize work_disabled on n
        context->search[i].work = RTmallocZero( (n+1) * sizeof(int) );
        // init work_enabled/work_disabled
        context->search[i].work_enabled = 0;
        context->search[i].work_disabled = n;
        // init score
        context->search[i].score = 0;
        // note, n+1 to initialize work_disabled on n
        context->search[i].nes_score = RTmallocZero( (n_guards * 2) * sizeof(int) );
    }

    return (void*)context;
}

/**
 * For each state, this function sets up the current guard values etc
 * This setup is then reused by the analysis function
 */
static int bs_setup(model_t model, por_context* pctx, int* src)
{
    // init globals
    int n_guards = pctx->nguards;

    // get number of transition
    int n = dm_nrows(GBgetDMInfo(model));

    // clear global tables
    memset(pctx->group_status, 0, n * sizeof(group_status_t));
    memset(pctx->nes_score, 0, (n_guards * 2) * sizeof(int));

    // fill guard status, request all guard values,
    GBgetStateLabelsAll(model, src, pctx->guard_status);

    // fill group status
    for(int i=0; i<n; i++) {
        guard_t* gt = GBgetGuard(model, i);
        // mark groups as disabled
        for(int j=0; j < gt->count; j++) {
            if (pctx->guard_status[gt->guard[j]] == 0) {
                pctx->group_status[i] |= GS_DISABLED;
                break;
            }
        }
    }
    // fill group score
    for(int i=0; i<n; i++) {
        if (pctx->group_status[i] & GS_DISABLED) {
            pctx->group_score[i] = 1;
        } else {
            // note: n*n won't work when n is very large (overflow)
            pctx->group_score[i] = pctx->group_status[i] & GS_VISIBLE ? n : n*n;
        }
    }
    // fill nes score
    // heuristic score h(x): 1 for disabled transition, n for enabled transition, n^2 for visible transition
    for(int i=0; i < n_guards; i++) {
        int idx = pctx->guard_status[i] ? i + n_guards : i;
        pctx->nes_score[idx] = 0;
        for(int j=0; j < pctx->ns[idx]->count; j++) {
            pctx->nes_score[idx] += pctx->group_score[ pctx->ns[idx]->data[j] ];
        }
    }

    // reset search order
    for(int i=0; i < pctx->beam_used; i++) {
        pctx->search_order[i] = i;
    }

    // clear emit status
    pctx->emit_score = 1;
    pctx->emit_limit = 0;

    // select an enabled transition group
    int beam_idx = 0;
    for(int z=0; z<n; z++) {
        if (!pctx->group_status[z]&GS_DISABLED) {
            // increase emit limit
            pctx->emit_limit++;
            // add to beam search
            pctx->search[beam_idx].work[0] = z;
            // init work_enabled/work_disabled
            pctx->search[beam_idx].work_enabled = 1;
            pctx->search[beam_idx].work_disabled = n;
            // init score
            pctx->search[beam_idx].score = 0;
            memset(pctx->search[beam_idx].emit_status, 0, n * sizeof(emit_status_t));
            memcpy(pctx->search[beam_idx].nes_score, pctx->nes_score, n_guards * 2 * sizeof(int));

            beam_idx++;
        }
    }
    // reset beam
    pctx->beam_used = beam_idx;

    return 0;
}

/**
 * The function select_group is called whenever a new group is added to the stubborn set
 * It takes care of updating the heuristic function for the nes based on the new group selection
 */
static inline void
select_group (model_t model, por_context* pctx, int group)
{
    // get index of current search context
    int so = pctx->search_order[0];

    // change the heuristic function according to selected group
    for(int k=0 ; k< pctx->group_in[group]->count; k++) {
        int nes = pctx->group_in[group]->data[k];
        // note: this selects some nesses that aren't used, but take the extra work
        // instead of accessing the guards to verify the work
        pctx->search[so].nes_score[nes] -= pctx->group_score[group];
    }
    return;
    (void)model;
}

/**
 * Analyze NS is the function called to find the smallest persistent set
 * It builds stubborn sets in multiple search contexts, by using a beam
 * search it switches search context each time the score of the current context
 * (based on heuristic function h(x) (nes_score)) isn't the lowest score anymore
 */
static inline int
bs_analyze(model_t model, por_context* pctx, int* src)
{
    // if no search context is used, there are no transitions, nothing to analyze
    if (pctx->beam_used == 0) return 0;

    // get number of transition groups
    int n = dm_nrows(GBgetDMInfo(model));

    // init globals
    int n_guards = pctx->nguards;

    // start searching in multiple contexts
    // the search order is a sorted array based on the score of the search context
    // start with the context in search_order[0] = best current score
    int idx = pctx->search_order[0];
    search_context *s = pctx->search;

    // infinite loop searching in multiple context, will bail out
    // when persistent set is found

    // the score of each search context is a sum:
    // disabled transition group 0
    // enabled  transition group 1
    // visible  transition group N
    while(1) {
        // while there are disabled transitions:
        while(s[idx].work_enabled == 0 && s[idx].work_disabled < n) {
            // one disabled transition less, increase the count (work_disabled = n -> no disabled transitions)
            s[idx].work_disabled++;
            int w = s[idx].work_disabled;
            int current_group = s[idx].work[w];

            // bail out if already ready
            if (s[idx].emit_status[current_group] & ES_READY) continue;

            // mark as selected and ready
            s[idx].emit_status[current_group] |= ES_SELECTED | ES_READY;

            // for a disabled transition we need to add the necessary enabling set
            // lookup which set has the lowest score on the heuristic function h(x)
            int selected_nes = -1;
            int selected_score = INT_MAX;

            // for each possible nes for the current group
            for(int k=0 ; k< pctx->group_has[current_group]->count; k++) {
                // get the nes
                int nes = pctx->group_has[current_group]->data[k];
                // check the score by the heuristic function h(x)
                if (s[idx].nes_score[nes] < selected_score) {
                    // check nes is indeed for this group
                    if ((nes < n_guards && (pctx->guard_status[nes] == 0))  ||
                        (nes >= n_guards && (pctx->guard_status[nes-n_guards] != 0)) ) {
                        // make this the current best
                        selected_nes = nes;
                        selected_score = s[idx].nes_score[nes];
                        // if score is 0 it can't improve, break the loop
                        if (selected_score == 0) break;
                    }
                }
            }
            // add nes
            if (selected_nes == -1) Fatal(1, error, "selected nes -1");
            // add the selected nes to work
            ci_list* cil = pctx->ns[selected_nes];
            for(int k=0; k < cil->count; k++) {
                // find group to add
                int group = cil->data[k];
                // if already selected, continue
                if (s[idx].emit_status[group] & ES_SELECTED) continue;
                // otherwise mark as selected
                s[idx].emit_status[group] |= ES_SELECTED;
                select_group (model, pctx, group);
                // and add to work array
                if (pctx->group_status[group] & GS_DISABLED) {
                    s[idx].work[s[idx].work_disabled--] = group;
                } else {
                    s[idx].work[s[idx].work_enabled++] = group;
                }
            }
        }

        // if the current search context has enabled transitions, handle all of them
        while (s[idx].work_enabled > 0) {
            // one less enabled transition (work_enabled = 0 -> no enabled transitions)
            s[idx].work_enabled--;
            int w = s[idx].work_enabled;
            int current_group = s[idx].work[w];

            // select and mark as ready
            s[idx].emit_status[current_group] |= ES_SELECTED | ES_READY;

            // this is a bit strange. the first group added by setup must be selected too
            // the condition here enforces that only the first enabled transition is selected.
            // after this, the score is > emit_score-1. but this might change when the
            // emit_score is changed in the initialization. Should select_group be called at setup?
            // other idea is to init the search context only here, because we might need to
            // do a lot more work if we do it all at once and then just need one
            if (s[idx].score == pctx->emit_score-1) select_group (model, pctx, current_group); /* init search context here? */

            // update the search score
            s[idx].score += 1 + ( pctx->group_status[current_group] & GS_VISIBLE ? n : 0);

            // push all dependent unselected groups
            por_context *ctx = (por_context*)GBgetContext(model);
            for (int j=0; j < *ctx->is_dependent_tg_tg[current_group]; j++) {
                int dependent_group = ctx->is_dependent_tg_tg[current_group][j+1];
                // already selected?
                if (s[idx].emit_status[dependent_group] & ES_SELECTED) continue;
                // mark as selected
                s[idx].emit_status[dependent_group] |= ES_SELECTED;
                // and select
                select_group (model, pctx, dependent_group);
                // add to work, enabled in front, disabled at the end
                if (pctx->group_status[dependent_group] & GS_DISABLED) {
                    s[idx].work[s[idx].work_disabled--] = dependent_group;
                } else {
                    s[idx].work[s[idx].work_enabled++] = dependent_group;
                }
            }
        }
        // move search context down based on score
        int score = s[idx].score;

        // if it can't move, we found the best score
        if (pctx->beam_used > 1 && score > s[pctx->search_order[1]].score) {
            // if score is one, and we have no more work, we're ready too
            if (score == pctx->emit_score && s[idx].work_disabled == n)
                break;

            // bubble current context down the search, continue with other context
            // this is known by the above conditions
            pctx->search_order[0] = pctx->search_order[1];
            // continue with 2
            int bubble = 2;
            while(bubble < pctx->beam_used && score >= s[pctx->search_order[bubble]].score) {
                pctx->search_order[bubble-1] = pctx->search_order[bubble];
                bubble++;
            }
            bubble--;

            if (bubble < pctx->beam_used)
                pctx->search_order[bubble] = idx;

            // find new best search idx
            idx = pctx->search_order[0];
        } else {
            // break out if not moving, and no more work can be done
            if (s[idx].work_disabled == n) break;
        }
     }
     return 0;
    (void) src;
}


/**
 * This function emits the persistent set for deadlocks
 * For deadlocks, no extra communication with the algorithm is required
 * and the persistent set just needs to be emitted. The assumption
 * is that the persistent set contains at least one transition by the assumption
 * that if all guards of a transition group are true, there is a successor state.
 * If this is not the case this function must be adapted to check that there is
 * at least one successor state generated
 */
static inline int
bs_emit_dlk(model_t model, por_context* pctx, int* src, TransitionCB cb, void* ctx)
{
    // if no transition is in the beam search, there are no
    // enabled transitions, return 0 directly
    if (pctx->beam_used == 0) return 0;

    // the score is setup such that score > emit_limit implies that
    // all transition groups are selected or a visible transition is selected
    // in this case, return all enabled transitions
    if (pctx->search[pctx->search_order[0]].score >= pctx->emit_limit) {
        return GBgetTransitionsAll(pctx->parent, src, cb, ctx);
    // if none of the above, return the persistent set
    } else {
        int n = dm_nrows(GBgetDMInfo(model));

        // emit
        int res = 0;
        for(int i=0; i < n; i++) {
            // enabled && selected
            if (!pctx->group_status[i]&GS_DISABLED &&
                 pctx->search[pctx->search_order[0]].emit_status[i]&ES_SELECTED) {
                 res+=GBgetTransitionsLong(pctx->parent,i,src,cb,ctx);
            }
        }
        return res;
    }
}

/**
 * These functions emits the persistent set with the ltl constraints
 * For ltl, extra communication with the algorithm is required.
 * The algorithm annotates each transition with the por_proviso flag.
 * For ltl, all selected transition groups in the persistent set must
 * have this por_proviso flag set to true, otherwise enabled(s) will be returned.
 */

typedef struct ltl_hook_context
{
    TransitionCB    cb;
    void*           user_context;
    int por_proviso_true_cnt;
    int por_proviso_false_cnt;
} ltl_hook_context_t;

void ltl_hook_cb (void*context,transition_info_t *ti,int*dst) {
    ltl_hook_context_t* infoctx = (ltl_hook_context_t*)context;
    infoctx->cb(infoctx->user_context, ti, dst);
    // catch transition info status
    if (ti->por_proviso) {
        infoctx->por_proviso_true_cnt++;
    } else {
        infoctx->por_proviso_false_cnt++;
    }
}

static inline int
bs_emit_ltl(model_t model, por_context* pctx, int* src, TransitionCB cb, void* ctx)
{
    // if no enabled transitions, return directly
    if (pctx->beam_used == 0) return 0;
    // if the score is larger then the number of enabled transitions, emit all
    if (pctx->emitted == 0 && pctx->search[pctx->search_order[0]].score >= pctx->emit_limit) {
        // return all enabled
        return GBgetTransitionsAll(pctx->parent, src, cb, ctx);
    // otherwise: try the persistent set, but enforce that all por_proviso flags are true
    } else {
        // setup context
        int n = dm_nrows(GBgetDMInfo(model));
        ltl_hook_context_t ltlctx = {cb, ctx, 0, 0};

        // emit select as long as por_proviso_false_cnt is 0
        int res = 0;
        pctx->emitted = 0;
        for(int i=0; i < n && ltlctx.por_proviso_false_cnt == 0; i++) {
            // enabled && selected
            if ( !pctx->group_status[i]&GS_DISABLED &&
                 (pctx->search[pctx->search_order[0]].emit_status[i]&ES_SELECTED) ) {
                pctx->search[pctx->search_order[0]].emit_status[i]|=ES_EMITTED;
                res+=GBgetTransitionsLong(pctx->parent,i,src,ltl_hook_cb,&ltlctx);
            }
        }

        // emit all if there is a transition group with por_proviso = false
        if (ltlctx.por_proviso_false_cnt != 0) {
            // reemmit, emit all unemmitted
            for(int i=0; i < n; i++) {
                // enabled && selected
                if ( !pctx->group_status[i]&GS_DISABLED &&
                    !(pctx->search[pctx->search_order[0]].emit_status[i]&ES_EMITTED) ) {
                    // these should also be marked as emmitted, for consistency
                    // except that this data is not used anymore
                    // pctx->search[pctx->search_order[0]].emit_status[i]|=ES_EMITTED;
                    res+=GBgetTransitionsLong(pctx->parent,i,src,cb,ctx);
                }
            }
        }
        return res;
    }
}

/**
 * Generic persistent set beam search *
 */
static int
por_beam_search_dlk_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    int res = 0;

    por_context* pctx = ((por_context*)GBgetContext(self));
    do {
        bs_setup(self, pctx, src);
        bs_analyze(self, pctx, src);
    } while ( (res = bs_emit_dlk(self, pctx, src, cb, user_context)) < 0 );

    return res;
    (void)src;
}

/**
 * Same but for LTL
 */
static int
por_beam_search_ltl_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    int res = 0;

    por_context* pctx = ((por_context*)GBgetContext(self));
    do {
        bs_setup(self, pctx, src);
        bs_analyze(self, pctx, src);
    } while ( (res = bs_emit_ltl(self, pctx, src, cb, user_context)) < 0 );

    return res;
    (void)src;
}


/**
 * Mark the visibility
 * NOTE: this is a hack that tightly couples the LTL and POR wrappers toghether.
 * A proper solution should be made using the PINS interface
 * Assumptions: this function is only called by the LTL layer if there is a POR layer present
 */
void
por_visibility(model_t model, int group, int visibility)
{
    por_context *ctx = (por_context *)GBgetContext (model);
    ctx->group_visibility[group] = visibility;
}

/**
 * Setup the partial order reduction layer
 */
model_t
GBaddPOR (model_t model, int por_check_ltl )
{
    Warning(info,"Initializing partial order reduction layer..");

    // check support for guards, fail without
    if (!GBhasGuardsInfo(model)) {
        Warning(info, "Frontend doesn't have guards. Ignoring --por.");
        return model;
    }

    // do the setup
    model_t             pormodel = GBcreateBase ();

    por_context *ctx = RTmalloc (sizeof *ctx);
    ctx->parent = model;

    // initializing dependency lookup table ( (t, t') \in D relation)
    Warning(info, "Initializing dependency lookup table.");

    matrix_t           *p_dm = GBgetDMInfo (model);
    matrix_t           *p_dm_w = p_dm;
    p_dm_w = GBgetDMInfoWrite (model);

    int groups = dm_nrows( p_dm );
    int len = dm_ncols( p_dm );
    dm_create(&ctx->is_dependent, groups, groups);
    for(int i=0; i < groups; i++) {
        for(int j=0; j < groups; j++) {
            if (i == j) {
                dm_set(&ctx->is_dependent, i, j);
            } else {
                // is dependent?
                for (int k=0; k < len; k++)
                {
                    if ((dm_is_set( p_dm_w, i, k) && dm_is_set( p_dm, j, k)) ||
                        (dm_is_set( p_dm, i, k) && dm_is_set( p_dm_w, j, k)) ) {
                        dm_set( &ctx->is_dependent, i, j );
                        break;
                    }
                }
            }
        }
    }

    // guard to group dependencies
    sl_group_t* sl_guards = GBgetStateLabelGroupInfo(model, GB_SL_GUARDS);
    int guards = sl_guards->count;
    matrix_t        *p_sl = GBgetStateLabelInfo(model);
    // len is unchanged
    dm_create(&ctx->guard_is_dependent, guards, groups);
    for(int i=0; i < guards; i++) {
        for(int j=0; j < groups; j++) {
            for(int k=0; k < len; k++) {
                if (dm_is_set( p_sl, sl_guards->sl_idx[i], k ) && dm_is_set( p_dm_w, j, k )) {
                    dm_set( &ctx->guard_is_dependent, sl_guards->sl_idx[i], j );
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
        if (g != NULL) {
            for(int j=0; j < g->count; j++) {
                dm_set(&gg_matrix, i, g->guard[j]);
            }
        } else {
            Fatal(1,error,"GUARD RETURNED NULL %d", i);
        }
    }

    // extract guard not co-enabled and guard-nes information
    // from guard may-be-co-enabled with guard relation:
    // for a guard g, find all guards g' that may-not-be co-enabled with it
    // then, for each g', mark all groups in gnce_matrix
    matrix_t *p_gce_matrix = GBgetGuardCoEnabledInfo(model);
    matrix_t gnce_matrix;
    dm_create(&gnce_matrix, guards, groups);
    for(int g=0; g < guards; g++) {
        // iterate over all guards
        for (int gg=0; gg < guards; gg++) {
            // find all guards that may not be co-enabled
            if (!dm_is_set(p_gce_matrix, g, gg)) {
                // gg may not be co-enabled with g, find all
                // transition groups in which it is used
                for(int tg=0; tg < groups; tg++) {
                    // if it is used, mark in gnce_matrix
                    if (dm_is_set(&gg_matrix, tg, gg))
                        dm_set(&gnce_matrix, g, tg);
                }
            }
        }
    }

    // mark minimal necessary enabling set
    matrix_t *p_gnes_matrix = GBgetGuardNESInfo(model);
    // copy p_gnes_matrix to gnes_matrix, then optimize it
    matrix_t gnes_matrix;
    dm_copy(p_gnes_matrix, &gnes_matrix);

    // optimize nes
    // remove all transition groups that do not write to this guard
    for(int i=0; i < guards; i++) {
        for(int j=0; j < groups; j++) {
            // if guard i has group j in the nes, make sure
            // the group writes to the same part of the state
            // vector the guard reads from, otherwise
            // this value can be removed
            if (dm_is_set(&gnes_matrix, i, j)) {
                if (!dm_is_set(&ctx->guard_is_dependent, i, j))
                    dm_unset(&gnes_matrix, i, j);
            }
        }
    }

    // same for nds
    matrix_t *p_gnds_matrix = GBgetGuardNDSInfo(model);
    matrix_t gnds_matrix;
    dm_copy(p_gnds_matrix, &gnds_matrix);

    // optimize nds matrix
    // remove all transition groups that do not write to this guard
    for(int i=0; i < guards; i++) {
        for(int j=0; j < groups; j++) {
            // if guard i has group j in the nes, make sure
            // the group writes to the same part of the state
            // vector the guard reads from, otherwise
            // this value can be removed
            if (dm_is_set(&gnds_matrix, i, j)) {
                if (!dm_is_set(&ctx->guard_is_dependent, i, j))
                    dm_unset(&gnds_matrix, i, j);
            }
        }
    }

    // set lookup tables
    ctx->is_dependent_tg_tg    = dm_rows_to_idx_table(&ctx->is_dependent);
    ctx->is_dependent_guard_tg = dm_rows_to_idx_table(&ctx->guard_is_dependent);
    ctx->guard_tg              = dm_cols_to_idx_table(&gg_matrix);
    ctx->guard_nce             = dm_rows_to_idx_table(&gnce_matrix);
    ctx->guard_nes             = dm_rows_to_idx_table(&gnes_matrix);
    ctx->guard_nds = dm_rows_to_idx_table(&gnds_matrix);


    // free temporary matrices
    dm_free(&gg_matrix);
    dm_free(&gnce_matrix);
    dm_free(&gnes_matrix);
    dm_free(&gnds_matrix);

    // init por model
    Warning(info, "Initializing dependency lookup table done.");
    GBsetContext (pormodel, ctx);

    GBsetNextStateLong  (pormodel, por_long);
    GBsetNextStateShort (pormodel, por_short);

    // what proviso do we need? none (deadlock) or ltl?
    if (por_check_ltl) {
        // reserve memory for group visibility, will be provided/set by ltl layer
        ctx->group_visibility = RTmallocZero( groups * sizeof(int) );
        // setup ltl search
        GBsetNextStateAll   (pormodel, por_beam_search_ltl_all);
    } else {
        // setup deadlock search
        GBsetNextStateAll   (pormodel, por_beam_search_dlk_all);
    }

    GBinitModelDefaults (&pormodel, model);

    int                 s0[len];
    GBgetInitialState (model, s0);
    GBsetInitialState (pormodel, s0);

    // after complete initialization
    bs_init_context (pormodel);

    return pormodel;
}
