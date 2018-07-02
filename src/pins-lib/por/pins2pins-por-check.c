#include <hre/config.h>

#include <limits.h>
#include <stdlib.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/por/pins2pins-por-check.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/por/por-internal.h>
#include <util-lib/fast_set.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/treedbs.h>
#include <util-lib/util.h>

/**
 * Verifies the results of the POR layer by checking the definition of the
 * dynamic stubborn set on the generated stubborn sets (on all paths over
 * non-stubborn transitions, the stubborn transitions remain enabled/disabled,
 * moreover the non stubborn transitions commute with the stubborn ones).
 *
 * Concepts (should be self explanatory):
 * - stubborn transition
 * - stubborn group
 * - non-stubborn (NS) transition
 * - non-stubborn group
 * - DFS over non-stubborn transitions
 *
 *
 */

/**
 * stacks contain: state, src_group, src_idx_in_group
 */
typedef struct dlk_hook_context {
    por_context    *pctx;
    void*           user_context;
    int             len;
    int             groups;
    char           *stubborn;
    ci_list        *ss_list;
    ci_list        *ss_en_list;
    ci_list        *por_emitted_list;
    ci_list        *en_list;
    ci_list        *seen_list;
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
    TransitionCB    user_cb;
} dlk_check_context_t;

static char *
str_list (ci_list *list)
{
    if (list->count == 0) {
        return "{}";
    }
    char *set = RTmalloc (4096);
    char *ptr = set;
    char *end = set + 4096;
    for (int i = 0; i < list->count; i++)
        ptr += snprintf (ptr, end - ptr, "%d, ", list->data[i]);
    ptr[-2] = '\0';
    return set;
}

static char *
str_group (dlk_check_context_t *ctx, int group)
{
    model_t model = ctx->pctx->parent;
    lts_type_t ltstype = GBgetLTStype (model);
    int label = lts_type_find_edge_label (ltstype, LTSMIN_EDGE_TYPE_STATEMENT);
    if (label) return "NULL";
    int type = lts_type_get_edge_label_typeno (ltstype, label);
    int count = pins_chunk_count  (model, type);
    if (count < ctx->groups) return "NULL";
    chunk c = pins_chunk_get  (model, type, group);
    return c.data;
}

static bool
find_list (ci_list *list, int num)
{
    for (int i = 0; i < list->count; i++) {
        int entry = list->data[i];
        if (entry == num) return true;
    }
    return false;
}

/**
 * tgt_out_stack: state, src_group, src_idx_in_group, src_src_group, src_src_idx_in_group
 * to remember from what transition we mimiced the NS transition!
 */
static inline dlk_check_context_t *
create_check_ctx (por_context *ctx)
{
    matrix_t* m = GBgetDMInfo(ctx->parent);
    dlk_check_context_t *check_ctx = RTalign (CACHE_LINE_SIZE, sizeof (dlk_check_context_t));
    check_ctx->groups = dm_nrows(m);
    check_ctx->len = dm_ncols(m);
    check_ctx->stubborn = RTalignZero (CACHE_LINE_SIZE, sizeof (char[check_ctx->groups]));
    check_ctx->ss_list = ci_create(check_ctx->groups);
    check_ctx->ss_en_list = ci_create(check_ctx->groups);
    check_ctx->por_emitted_list = ci_create(check_ctx->groups);
    check_ctx->en_list = ci_create(check_ctx->groups);
    check_ctx->seen_list = ci_create(check_ctx->groups);
    check_ctx->stack = dfs_stack_create (check_ctx->len + 2);
    check_ctx->tgt_in_stack = dfs_stack_create (check_ctx->len + 2);
    check_ctx->tgt_out_stack = dfs_stack_create (check_ctx->len + 4);
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

static int *
push_trans (dlk_check_context_t* ctx, dfs_stack_t stack, int* dst)
{
    int *space = dfs_stack_push (stack, NULL );
    memcpy (space, dst, sizeof(int[ctx->len]));
    space[ctx->len] = ctx->current; // from update group info
    space[ctx->len + 1] = ctx->current_idx; // from update group info
    return space;
}

static void
explore_state (dlk_check_context_t *ctx, int *state, TransitionCB cb) {
    ctx->p_count = ctx->np_count = ctx->pgroup_count = 0;
    ctx->current = -1;
    ctx->seen_list->count = 0;
    ctx->src = state;
    GBgetTransitionsAll(ctx->pctx->parent, state, cb, ctx);
}

static void
print_diff (dlk_check_context_t *ctx, int *s1, int *s2)
{
    model_t model = ctx->pctx->parent;
    lts_type_t ltstype = GBgetLTStype (model);
    for (int i = 0; i < ctx->len; i++) {
        if (s1[i] != s2[i]) {
            Printf (info, "%30s: %d <--> %d",
                    lts_type_get_state_name(ltstype, i), s1[i], s2[i]);
            Printf (info, "\n");
        }
    }
}

static void
print_group_name (dlk_check_context_t *ctx, int group, int idx)
{
    model_t model = ctx->pctx->parent;
    lts_type_t      ltstype = GBgetLTStype (model);
    size_t K = pins_get_group_count (model);

    int             statement_label = lts_type_find_edge_label (
                                     ltstype, LTSMIN_EDGE_TYPE_STATEMENT);
    if (statement_label != -1) {
        int             statement_type = lts_type_get_edge_label_typeno (
                                                 ltstype, statement_label);
        size_t          count = pins_chunk_count  (model, statement_type);
        HREassert (count >= K, "Missing group names in LTSMIN_EDGE_TYPE_STATEMENT edge labels");
        chunk c = pins_chunk_get  (model, statement_type, group);
        Printf (lerror, "Group %d (%d) name: %s\n", group, idx, c.data);
    } else {
        Printf (lerror, "Group %d (%d)\n", group, idx);
    }
}

static void
print_search_path (dlk_check_context_t *ctx)
{
    Printf (lerror, "NS search path: \n");
    size_t start = dfs_stack_frame_size(ctx->stack) == 0 ? 1 : 0;
    for (size_t i = start; i < dfs_stack_nframes(ctx->stack); i++) {
        int *path = dfs_stack_peek_top (ctx->stack, i);
        print_group_name (ctx, path[ctx->len], path[ctx->len+1]);
    }
    Printf (lerror, "\n");
}

static void
print_noncommuting_trans (dlk_check_context_t *ctx, int *src, int *dst, int *s1,
                                                               int *tgt, int *s2)
{
    print_search_path (ctx);

    Printf (info, "\n");
    Printf (info, "src  ----ns---->  dst\n");
    Printf (info, " |                 |\n");
    Printf (info, "stub              stub\n");
    Printf (info, " |                 |\n");
    Printf (info, " v                 v\n");
    Printf (info, "tgt --ns--> s2 =?= s1\n");


    Printf (info, "\n");
    Printf (info, "src --> dst (group %d)\n", dst[ctx->len]);
    print_group_name (ctx, dst[ctx->len], dst[ctx->len+1]);
    print_diff (ctx, src, dst);

    Printf (info, "\n");
    Printf (info, "dst --> s1 (group %d)\n", s1[ctx->len]);
    print_group_name (ctx, s1[ctx->len], s1[ctx->len+1]);
    print_diff (ctx, dst, s1);

    Printf (info, "\n");
    Printf (info, "src --> tgt (group %d)\n", tgt[ctx->len]);
    print_group_name (ctx, tgt[ctx->len], tgt[ctx->len+1]);
    print_diff (ctx, src, tgt);

    Printf (info, "\n");
    Printf (info, "tgt --> s2 (group %d)\n", s2[ctx->len]);
    print_group_name (ctx, s2[ctx->len], s2[ctx->len+1]);
    print_diff (ctx, tgt, s2);

    Printf (info, "\n");
    Printf (info, "s1 =?= s2\n");
    print_diff (ctx, s1, s2);
}

static void
print_disabled_trans (dlk_check_context_t *ctx, int *src, int *dst, int *tgt)
{
    print_search_path (ctx);

    Printf (info, "\n");
    Printf (info, "src  ----ns---->  dst\n");
    Printf (info, " |                 |\n");
    Printf (info, "stub              stub\n");
    Printf (info, " |                 |\n");
    Printf (info, " v                 v\n");
    Printf (info, "tgt --ns--> ERR   s1\n");

    Printf (info, "\n");
    Printf (info, "src --> dst (group %d)\n", dst[ctx->len]);
    print_diff (ctx, src, dst);

    Printf (info, "\n");
    Printf (info, "src --> tgt (group %d)\n", tgt[ctx->len]);
    print_diff (ctx, src, tgt);
}

static void
print_enabled_trans (dlk_check_context_t *ctx, int *src, int *dst, int *s1)
{
    print_search_path (ctx);

    Printf (info, "\n");
    Printf (info, "src  ----ns---->  dst\n");
    Printf (info, "                   |\n");
    Printf (info, "                 stub\n");
    Printf (info, "                   |\n");
    Printf (info, "                   v\n");
    Printf (info, "                  s1\n");

    Printf (info, "\n");
    Printf (info, "src --> dst (group %d)\n", dst[ctx->len]);
    print_diff (ctx, src, dst);

    Printf (info, "\n");
    Printf (info, "dst --> s1 (group %d)\n", s1[ctx->len]);
    print_diff (ctx, dst, s1);
}


/**
 * Check for whether a ns transition from src to dst commutes with the
 * same path from all tgt s.t. src --stubborn--> tgt:
 *
 * src  --ns-->  dst --ns--> next
 *  |             |
 * stub*         stub*
 *  |             |
 *  v             v s1 in tgt_in_stack
 * tgt* --ns-->
 *           s2 in tgt_out_stack
 *
 * Also checks key transitions for weak POR (at least one should remain enabled
 * after a NS transition).
 */
static void
check_commute (dlk_check_context_t *ctx, int *dst)
{
    int nsgroup = dst[ctx->len];
    int nsgroup_idx = dst[ctx->len + 1];
    if (nsgroup == -1) return;

    int scount = dfs_stack_frame_size(ctx->tgt_in_stack);
    int nscount = dfs_stack_frame_size(ctx->tgt_out_stack);
    if ((!POR_WEAK && scount != nscount) || scount > nscount)
        Warning (lerror, "Stubborn trans %s disappeared after NS %d/%d (|src| = %d, |dst| = %d):\n %s <--> %s",
                 str_list(ctx->ss_en_list), nsgroup, nsgroup_idx, scount, nscount,
                 str_group(ctx,ctx->ss_en_list->data[0]), str_group(ctx, nsgroup));

    // check commutes ( tgt_out is superset of tgt_in for weak POR )
    int s_i = scount - 1;
    for (int ns_i = nscount - 1; ns_i >= 0 && s_i >= 0; ns_i--) {
        int *s2 = dfs_stack_peek (ctx->tgt_out_stack, ns_i);
        HREassert (s2[ctx->len] == nsgroup, "Minic group failed? %d != %d", s2[ctx->len], nsgroup);
        HREassert (s2[ctx->len + 1] == nsgroup_idx, "Minic transition in group failed? %d != %d", s2[ctx->len+1], nsgroup_idx);

        int *s1 = dfs_stack_peek (ctx->tgt_in_stack, s_i);
        if (POR_WEAK) {
            if (s1[ctx->len] != s2[ctx->len+2] || s1[ctx->len + 1] != s2[ctx->len + 3]) {
                continue; // non-key transition may have been disabled
            }
        }
        s_i--;

        int diff = memcmp (s1, s2, sizeof(int[ctx->len]));
        if (diff != 0) {
            int *src = dfs_stack_peek_top (ctx->stack, 2);
            int *tgt = dfs_stack_peek_top2 (ctx->tgt_in_stack, 1, ns_i);

            print_noncommuting_trans (ctx, src, dst, s1, tgt, s2);

            HREassert (diff == 0, "Stubborn trans %d/%d does not commute with NS trans: %d/%d "
                       "(count: %d, idx: %d, stubborn groups enabled: %d)"
                       ":\n\n%s\ndoes not commute with\n%s", s1[ctx->len],
                       s1[ctx->len+1], s2[ctx->len], s1[ctx->len + 1],
                       scount, ns_i, ctx->ss_en_list->count,
                       str_group(ctx, s1[ctx->len]), str_group(ctx, s2[ctx->len]));
        }

        //Debug ("Stubborn trans %d/%d commutes with %d/%d",
        //      s1[ctx->len], s1[ctx->len+1], s2[ctx->len], s1[ctx->len + 1]);
    }

    if (scount == 0) { HREassert (POR_WEAK);
        // first we print commute info on all missing transitions
        for (int ns_i = nscount - 1; ns_i >= 0 && s_i >= 0; ns_i--) {
            int *s2 = dfs_stack_peek (ctx->tgt_out_stack, ns_i);
            int space[ctx->len + 2]; // create the disabled stubborn transition
            for (int i = 0; i < ctx->len; i++) space[i] = -1;
            space[ctx->len] = nsgroup;
            space[ctx->len + 1] = nsgroup_idx;
            int *src = dfs_stack_peek_top (ctx->stack, 2);
            int *tgt = dfs_stack_peek_top2 (ctx->tgt_in_stack, 1, ns_i);
            print_noncommuting_trans (ctx, src, dst, space, tgt, s2);
        }
        HREassert (false, "No key transition in weak set after NS %d/%d. "
                 "pers set: %s\n\n", nsgroup, nsgroup_idx, str_list(ctx->ss_en_list));
    }

    // additional check whether all stubborn transitions have been considered
    if (POR_WEAK && s_i != -1) {
        for (int i = nscount - 1; i >= 0; i--) {
            int *s1 = dfs_stack_peek (ctx->tgt_in_stack, s_i);
            HREassert (false, "Newly introduced stubborn transition %d/%d "
                        "after NS transition %d/%d",
                       s1[ctx->len], s1[ctx->len+1], nsgroup, nsgroup_idx);
        }
    }

    // clean
    for (int i = nscount - 1; i >= 0; i--) {
        dfs_stack_pop (ctx->tgt_out_stack);
    }
    Debug ("Stubborn transitions (%d x) commute with NS transition %d/%d (%d x)."
           "%d key transitions found (%d - %d)",
          scount, nsgroup, nsgroup_idx, nscount, nscount - scount, nscount, scount);
}

static void
follow_dfs_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    (void) cpy;
    dlk_check_context_t *ctx = (dlk_check_context_t*)context;
    update_group_info (ctx, ti);

    if (ti->group != ctx->follow_group) return;
    if (ctx->current_idx != ctx->follow_group_idx) return;

    int *space = push_trans (ctx, ctx->tgt_out_stack, dst);
    // copy the group info from the transition that we mimiced!
    space[ctx->len + 2] = ctx->src[ctx->len];
    space[ctx->len + 3] = ctx->src[ctx->len + 1];
    ctx->p_count++;
}

/**
 * Explore same transition from stubborn state and put results on tgt_out stack.
 */
static void
mimic (dlk_check_context_t *ctx, int *state)
{
    ctx->follow_group = state[ctx->len];
    ctx->follow_group_idx = state[ctx->len + 1];

    if (ctx->follow_group == -1) return;

    // iterate over stubborn transitions
    for (int i = dfs_stack_frame_size(ctx->tgt_in_stack) - 1; i >= 0; i--) {
        int *t = dfs_stack_peek (ctx->tgt_in_stack, i);
        explore_state (ctx, t, follow_dfs_cb);

        if (!POR_WEAK && ctx->p_count != 1) {
            int *src = dfs_stack_peek_top (ctx->stack, 1);
            print_disabled_trans (ctx, src, state, t);

            HREassert (ctx->p_count == 1, "NS trans %d/%d disabled from stubborn trans "
                     "%d/%d (successor count: %d), pers set: %s"
                     ":\n\n%s\nwas disabled by:\n%s",
                     ctx->follow_group, ctx->follow_group_idx, t[ctx->len],
                     t[ctx->len + 1], ctx->p_count, str_list(ctx->ss_en_list),
                     str_group(ctx, state[ctx->len]),
                     str_group(ctx, t[ctx->len]));
        } // for weak sets, we check whether tgt_out is a nonempty subset of tgt_in
    }
}

static void
get_nonstubborn_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    (void) cpy;
    dlk_check_context_t *ctx = (dlk_check_context_t*)context;

    update_group_info (ctx, ti);
    ctx->seen_list->data[ctx->seen_list->count++] = ti->group;

    if (ctx->stubborn[ti->group]) {
        // push (initially) enabled stubborn transitions
        dst = push_trans (ctx, ctx->tgt_in_stack, dst);

        if (!find_list(ctx->ss_en_list, ti->group)) { // bail out
            int *src = dfs_stack_peek_top (ctx->stack, 2);
            print_enabled_trans (ctx, src, ctx->src, dst);
            HREassert (false, "Disabled stubborn transition %d was enabled by %d/%d, "
                           "(ss: %s, enss: %s):\n\n%s\nwas enabled by:\n%s",
                           ti->group, ctx->src[ctx->len], ctx->src[ctx->len+1],
                           str_list(ctx->ss_list), str_list(ctx->ss_en_list),
                           str_group(ctx, ti->group),
                           str_group(ctx, ctx->src[ctx->len]));
        }
    } else {
        push_trans (ctx, ctx->stack, dst);
    }
}

/**
 * Invariants:
 * - Both stack and tgt_in_stack have an equal nr of frame levels (enter / leave)
 * - stack only contains the src state and states reachable of NS transitions
 * - forall level in stack : stubborn (stack.frame(level).top) = tgt.frame(level)
 *   (tgt_in contains the stubborn successors of the currently expanded state
 *   on the stack of the NS search)
 */
static void
do_dfs_over_ns (dlk_check_context_t *ctx)
{
    // for symmetry with stack push an unused state on tgt_in_stack
    dfs_stack_push (ctx->tgt_in_stack, dfs_stack_top (ctx->stack));

    while (true) {
        int *state = dfs_stack_top (ctx->stack);
        if (NULL != state) {

            // For every explored NS transition, we check commutativity with
            // the stubborn transitions enabled at the initial state.
            mimic (ctx, state);         // fill tgt_out
            dfs_stack_enter (ctx->tgt_in_stack);
            dfs_stack_enter (ctx->stack);
            explore_state (ctx, state, get_nonstubborn_cb);
            check_commute (ctx, state); // empty tgt_out

            int seen = fset_find (ctx->set, NULL, state, NULL, true);
            HREassert (seen != FSET_FULL);
            if (seen) {
                dfs_stack_leave (ctx->tgt_in_stack);
                dfs_stack_leave (ctx->stack); // throw away NS successors (next)
                dfs_stack_pop (ctx->stack); // throw away state
            }
        } else if (dfs_stack_nframes(ctx->stack) == 0) {
            break;
        } else {
            dfs_stack_leave (ctx->tgt_in_stack); // removes multiple states
            dfs_stack_leave (ctx->stack);
            dfs_stack_pop (ctx->stack);
        }
    }

    dfs_stack_pop (ctx->tgt_in_stack); // remove symmetry state
}

// checks the transitive closure of all transitions outside the persistent set
// all transitions in these transition groups should be independent with all
// transitions in the selected persistent set
static void
check_semistubborn (dlk_check_context_t *ctx, int *src)
{
    HREassert (ctx->pctx->enabled_list->count != ctx->ss_en_list->count, "Pers == En?");
    int *space = dfs_stack_push (ctx->stack, NULL);
    memcpy (space, src, sizeof(int[ctx->len]));
    space[ctx->len] = -1;
    do_dfs_over_ns (ctx);
    Debug ("Transitive por check visited %zu states", fset_count(ctx->set));
    fset_clear (ctx->set); // clean seen states
}

static inline void
bs_emit_dlk_check (model_t model, por_context *pctx, int *src, TransitionCB cb,
                   void *org, int successors)
{
    if (successors == 0) { // check real deadlock
        int successors = GBgetTransitionsAll (pctx->parent, src, cb, org);
        HREassert (successors == 0, "Deadlock state introduced by POR! Enabled: %s",
                   str_list(pctx->enabled_list));
    }

    if (successors >= pctx->enabled_list->count) // stubborn
        return;

    dlk_check_context_t *ctx = GBgetContext(model);

    ctx->en_list->count = 0;
    ctx->ss_list->count = 0;
    ctx->ss_en_list->count = 0;
    for (int i = 0; i < ctx->groups; i++) {
        bool enabled  = !(pctx->group_status[i] & GS_DISABLED);
        bool stubborn = por_is_stubborn(pctx, i);

        ctx->en_list->data[ctx->en_list->count] = i;
        ctx->ss_list->data[ctx->ss_list->count] = i;
        ctx->ss_en_list->data[ctx->ss_en_list->count] = i;

        ctx->ss_list->count += stubborn;
        ctx->en_list->count += enabled;
        ctx->ss_en_list->count += enabled & stubborn;

        ctx->stubborn[i] = stubborn;
    }

    bool same = ctx->ss_en_list->count == ctx->por_emitted_list->count;
    ci_sort (ctx->ss_en_list);
    ci_sort (ctx->por_emitted_list);
    for (int i = 0; i < ctx->ss_en_list->count && same; i++) {
        same &= (ctx->ss_en_list->data[i] == ctx->por_emitted_list->data[i]);
    }
    if (!same) {
        Abort ("POR layer emitted (%s) conflicts with stubborn set (%s)",
               str_list(ctx->por_emitted_list), str_list(ctx->ss_en_list));
    }

    check_semistubborn (ctx, src);
}

static void
por_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    dlk_check_context_t *ctx = (dlk_check_context_t*)context;

    if (ctx->por_emitted_list->count == 0 ||
            ctx->por_emitted_list->data[ctx->por_emitted_list->count-1] != ti->group)
        ctx->por_emitted_list->data[ctx->por_emitted_list->count++] = ti->group;

    ctx->user_cb (ctx->user_context, ti, dst, cpy);
}

static int
check_por_all (model_t check_model, int *src, TransitionCB cb, void *user_context)
{
    model_t por_model = GBgetParent (check_model);
    dlk_check_context_t *check_ctx = GBgetContext(check_model);
    check_ctx->user_cb = cb;
    check_ctx->user_context = user_context;
    check_ctx->por_emitted_list->count = 0;
    int successors = GBgetTransitionsAll (por_model, src, por_cb, check_ctx);

    por_context *por_ctx = GBgetContext (por_model);
    bs_emit_dlk_check (check_model, por_ctx, src, cb, user_context, successors);
    return successors;
}

model_t
GBaddPORCheck (model_t model)
{
    HREassert (PINS_LTL == PINS_LTL_AUTO, "Use --por-check without LTL");

    Print1 (info, "POR checking layer activated.");

    // init POR
    model_t                 por_model = PORwrapper (model);

    // create extra check layer
    model_t                 check_model = GBcreateBase ();
    por_context            *por_ctx = GBgetContext (por_model);
    dlk_check_context_t    *check_ctx = create_check_ctx (por_ctx);
    GBsetContext (check_model, check_ctx);

    GBinitModelDefaults (&check_model, por_model);

    GBsetNextStateAll (check_model, check_por_all);

    return check_model;
}
