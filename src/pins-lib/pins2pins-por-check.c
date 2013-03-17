#include <hre/config.h>

#include <limits.h>
#include <stdlib.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins2pins-por.h>
#include <util-lib/fast_set.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/treedbs.h>
#include <util-lib/util.h>

typedef struct dlk_hook_context {
    por_context    *pctx;
    void*           user_context;
    int             len;
    int             groups;
    int            *persistent;
    ci_list        *pers_list;
    ci_list        *en_list;
    int             pgroup_count; // persistent groups encountered
    int             np_count;     // non persistent trans count
    int             p_count;      // pers (trans) count
    int             current;
    int             current_idx;
    int             follow_group;
    int             follow_group_idx;
    int            *src;
    dfs_stack_t     stack;
    dfs_stack_t     tgt_in_stack;
    dfs_stack_t     tgt_out_stack;
    treedbs_t       tree;
    fset_t         *set;
} dlk_check_context_t;

static char *
str_list (ci_list *list)
{
    HREassert (list->count > 0);
    char *set = RTmalloc (4096);
    char *ptr = set;
    char *end = set + 4096;
    for (int i = 0; i < list->count; i++)
        ptr += snprintf (ptr, end - ptr, "%d, ", list->data[i]);
    ptr[-2] = '\0';
    return set;
}

static inline dlk_check_context_t *
create_check_ctx (por_context *ctx)
{
    matrix_t* m = GBgetDMInfo(ctx->parent);
    dlk_check_context_t *check_ctx = RTalign (CACHE_LINE_SIZE, sizeof (dlk_check_context_t));
    check_ctx->groups = dm_nrows(m);
    check_ctx->len = dm_ncols(m);
    check_ctx->persistent = RTalignZero (CACHE_LINE_SIZE, sizeof (int[check_ctx->groups]));
    check_ctx->pers_list = RTalignZero (CACHE_LINE_SIZE, sizeof (int[check_ctx->groups+1]));
    check_ctx->en_list = RTalignZero (CACHE_LINE_SIZE, sizeof (int[check_ctx->groups+1]));
    check_ctx->stack = dfs_stack_create (check_ctx->len + 2);
    check_ctx->tgt_in_stack = dfs_stack_create (check_ctx->len + 2);
    check_ctx->tgt_out_stack = dfs_stack_create (check_ctx->len + 2);
    //loc->tree = TreeDBScreate (loc->len);
    check_ctx->set = fset_create (check_ctx->len, 0, 4, 26);
    check_ctx->pctx = ctx;
    return check_ctx;
}

static inline void
update_group_info (dlk_check_context_t *ctx, transition_info_t *ti)
{
    if (ctx->current != ti->group) {
        // first time we see this one
        ctx->current = ti->group;
        ctx->current_idx = 0;
    } else {
        ctx->current_idx++;
    }
}

static void
push_state (dlk_check_context_t* ctx, dfs_stack_t stack, int* dst)
{
    int *space = dfs_stack_push (stack, NULL );
    memcpy (space, dst, sizeof(int[ctx->len]));
    space[ctx->len] = ctx->current; // from update group info
    space[ctx->len + 1] = ctx->current_idx; // from update group info
}

static void
explore_state (dlk_check_context_t *ctx, int *state, TransitionCB cb) {
    ctx->p_count = ctx->np_count = ctx->pgroup_count = 0;
    ctx->current = -1;
    GBgetTransitionsAll(ctx->pctx->parent, state, cb, ctx);
}

static void
follow_dfs_cb (void *context, transition_info_t *ti, int *dst)
{
    dlk_check_context_t *ctx = (dlk_check_context_t*)context;
    update_group_info (ctx, ti);

    if (ti->group != ctx->follow_group) return;
    if (ctx->current_idx != ctx->follow_group_idx) return;

    push_state (ctx, ctx->tgt_out_stack, dst);
    ctx->p_count++;
}

static void
check_persistent_cb (void *context, transition_info_t *ti, int *dst)
{
    dlk_check_context_t *ctx = (dlk_check_context_t*)context;
    if (ctx->persistent[ti->group] == 0) return; // non persistent (not selected)

    update_group_info (ctx, ti);
    push_state (ctx, ctx->tgt_out_stack, dst);
    ctx->pgroup_count += ctx->current_idx == 0;
    ctx->p_count++;
}

/**
 * Check for whether path of np transitions from src to dst commutes with the
 * same path from all tgt s.t. src --persistent--> tgt:
 *
 * src  --np--> s_1 --np--> .... --np-->  dst
 *  |                                      |
 * pers*                                  pers*
 *  |                                      |
 *  v                                      v
 * tgt* --np--> s_1 --np--> .... --np--> tgtdst*
 */
static void
check_commute (dlk_check_context_t *ctx, int *dst)
{
    int bottom = dfs_stack_nframes (ctx->stack);
    HREassert (bottom > 1, "Pers == En?");

    int *src = dfs_stack_peek_top (ctx->stack, bottom);
    HREassert (src[ctx->len] == -1, "Source not on bottom of stack");

    // get all persistent trans from src
    explore_state (ctx, src, check_persistent_cb);
    HREassert (ctx->pgroup_count == ctx->pers_list->count, "Persistent groups disappeared?");
    int pcount = ctx->p_count;
    HREassert (dfs_stack_size(ctx->tgt_out_stack) == (size_t)pcount);

    swap (ctx->tgt_in_stack, ctx->tgt_out_stack);

    ci_list        *path_list = ctx->en_list; path_list->count = 0; // reused!
    // Follow DFS trace by updating states inline in src_stack
    for (int i = bottom - 1; i > 0; i--) {
        int *path = dfs_stack_peek_top (ctx->stack, i);
        ctx->follow_group  = path[ctx->len];
        ctx->follow_group_idx = path[ctx->len + 1];
        path_list->data[path_list->count++] = ctx->follow_group;

        for (int j = pcount - 1; j >= 0; j--) {
            int *state = dfs_stack_peek (ctx->tgt_in_stack, j);
            explore_state (ctx, state, follow_dfs_cb);
            HREassert (ctx->p_count == 1, "NP path (%s) disabled from pers trans "
                     "%d/%d (not group!) at group %d,%d (successor count: %d), pers set: %s",
                     str_list(path_list), j, pcount, ctx->follow_group,
                     ctx->follow_group_idx, ctx->p_count, str_list(ctx->pers_list));
        }

        swap (ctx->tgt_in_stack, ctx->tgt_out_stack); // tgtdst == tgt_in_stack
        for (int j = pcount - 1; j >= 0; j--) // empty out
            HREassert (dfs_stack_pop(ctx->tgt_out_stack));
        HREassert (dfs_stack_size(ctx->tgt_out_stack) == 0);
    }

    explore_state (ctx, dst, check_persistent_cb);
    HREassert (pcount == ctx->p_count, "Persistent trans disappeared (|src| = %d, |dst| = %d)", pcount, ctx->p_count);

    for (int j = pcount - 1; j >= 0; j--) {
        int *state1 = dfs_stack_pop (ctx->tgt_in_stack);
        int *state2 = dfs_stack_pop (ctx->tgt_out_stack);
        int diff = memcmp (state1, state2, sizeof(int[ctx->len]));
        HREassert (diff == 0, "Path %s not commute with persistent set: %s (trans: %d)", str_list(path_list), str_list(ctx->pers_list), pcount);
    }
    Debug ("Path %s commutes with persistent set: %s (trans: %d)", str_list(path_list), str_list(ctx->pers_list), pcount);
}

static void
check_np_cb (void *context, transition_info_t *ti, int *dst)
{
    dlk_check_context_t *ctx = (dlk_check_context_t*)context;

    update_group_info (ctx, ti);

    if (ctx->persistent[ti->group]) {
        ctx->pgroup_count += ctx->current_idx == 0;
        return; // we've taken a transition from the persistent set, stop checking
    }

    ctx->np_count++;
    int seen = fset_find (ctx->set, NULL, dst, NULL, false);
    if (!seen) push_state (ctx, ctx->stack, dst);
}

static void
check_result (dlk_check_context_t *ctx, int *state, int group)
{
    if (group != -1)
    for (int i = 0; i < ctx->pers_list->count; i++) {
        int p = ctx->pers_list->data[i];
        HREassert (p != group, "i == state_group (both %d)? something must be wrong", p);
        int dependent = dm_is_set(&ctx->pctx->is_dep_and_ce, p, group);
        HREassert (!dependent, "Error: dependency between %d and %d", p, group);
    }

    HREassert (ctx->pgroup_count == ctx->pers_list->count,
        "Persistent set (%s) was disabled by %d (seen: %d)", str_list(ctx->pers_list), group, ctx->pgroup_count);

    if (ctx->np_count == 0) { // no more nonpersistent trans
        // check commutative
        check_commute (ctx, state);
    }
}

static void
do_dfs_over_np (dlk_check_context_t *ctx)
{
    while (true) {
        int *state = dfs_stack_top (ctx->stack);
        if (NULL != state) {
            int seen = fset_find (ctx->set, NULL, state, NULL, true);
            if (!seen) {
                dfs_stack_enter (ctx->stack);
                int group = state[ctx->len];
                explore_state (ctx, state, check_np_cb);
                check_result (ctx, state, group);
            } else {
                dfs_stack_pop (ctx->stack);
            }
        } else if (0 == dfs_stack_size(ctx->stack)) {
            break;
        } else {
            dfs_stack_leave (ctx->stack);
            dfs_stack_pop (ctx->stack);
        }
    }
}

// checks the transitive closure of all transitions outside the persistent set
// all transitions in these transition groups should be independent with all
// transitions in the selected persistent set
static void
check_persistence (dlk_check_context_t *ctx, int *src)
{
    if (ctx->pers_list->count == 0) return; // no pers set
    HREassert (ctx->pctx->emit_limit != ctx->pers_list->count, "Pers == En?");
    int *space = dfs_stack_push (ctx->stack, NULL);
    memcpy (space, src, sizeof(int[ctx->len]));
    space[ctx->len] = -1;
    do_dfs_over_np (ctx);
    //Warning (info, "Transitive por check visited %zu states", fset_count(dlkctx->set));
    fset_clear (ctx->set); // clean seen states
}

static inline void
bs_emit_dlk_check (model_t model, por_context *pctx, int *src, TransitionCB cb,
                   void *ctx)
{
    if (pctx->beam_used == 0) { // check real deadlock
        int successors = GBgetTransitionsAll (pctx->parent, src, cb, ctx);
        HREassert (successors == 0, "Deadlock state introduced by POR!");
    }

    if (pctx->search[pctx->search_order[0]].score >= pctx->emit_limit)
        return;

    dlk_check_context_t *dctx = GBgetContext(model);
    dctx->pers_list->count = 0;
    dctx->en_list->count = 0;
    for (int i = 0; i < dctx->groups; i++) {
        dctx->persistent[i] = 0;
        if (pctx->group_status[i] & GS_DISABLED) continue; // disabled

        // check MCE relation
        for (int j = 0; j < dctx->en_list->count; j++) {
            int group = dctx->en_list->data[j];
            int nce = dm_is_set(&pctx->gnce_tg_tg, i, group);
            HREassert (!nce, "Error: groups enabled, but not coenabled: %d and %d", i, group);
        }
        dctx->en_list->data[dctx->en_list->count++] = i;

        if (pctx->search[pctx->search_order[0]].emit_status[i]&ES_SELECTED) { // selected
             dctx->persistent[i] = 1;
             dctx->pers_list->data[dctx->pers_list->count++] = i;
        }
    }
    check_persistence (dctx, src);
}

static int
check_por_all (model_t check_model, int *src, TransitionCB cb, void *user_context)
{
    model_t por_model = GBgetParent (check_model);
    int successors = GBgetTransitionsAll (por_model, src, cb, user_context);
    por_context *por_ctx = GBgetContext (por_model);
    bs_emit_dlk_check (check_model, por_ctx, src, cb, user_context);
    return successors;
}

model_t
GBaddPORCheck (model_t model, int por_check_ltl)
{
    HREassert (!por_check_ltl, "Use --por-check without LTL");

    // init POR
    model_t por_model = GBaddPOR (model, por_check_ltl);

    // create extra check layer
    model_t             check_model = GBcreateBase ();

    por_context *por_ctx = GBgetContext (por_model);
    dlk_check_context_t *check_ctx = create_check_ctx (por_ctx);
    GBsetContext (check_model, check_ctx);

    GBinitModelDefaults (&check_model, por_model);

    GBsetNextStateAll (check_model, check_por_all);

    return check_model;
}
