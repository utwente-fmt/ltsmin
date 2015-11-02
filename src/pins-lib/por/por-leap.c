#include <hre/config.h>

#include <stdbool.h>

#include <pins-lib/pins-util.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/por/por-leap.h>
#include <pins-lib/por/por-internal.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>


struct leap_s {
    next_method_black_t     next_por;
    size_t                  groups;
    size_t                  slots;
    size_t                  round;
    prov_t                  proviso;
    bool                    visible;
    ci_list               **lists;
    por_context            *por;
    dfs_stack_t             inout[2];
    void                   *uctx;
    TransitionCB            ucb;
};

static void
forward_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    leap_t             *leap = (leap_t *) context;

    // set group to the special group we added in the add_leap_group function
    transition_info_t ti_new = { ti->labels, leap->groups, ti->por_proviso };
    leap->ucb (leap->uctx, &ti_new, dst, cpy);
    leap->proviso.por_proviso_false_cnt += ti_new.por_proviso == 0;
    leap->proviso.por_proviso_true_cnt += ti_new.por_proviso != 0;
    // avoid full exploration (proviso is enforced later in backtrack)
    ti->por_proviso = 1;
}

static void
chain_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    dfs_stack_t         stack = (dfs_stack_t) context;
    dfs_stack_push (stack, dst);
    (void) ti; (void) cpy;
}

static inline size_t
leap_do_emit (leap_t *leap, int *src, ci_list *groups, TransitionCB cb,
              void *uctx)
{
    por_context        *ctx = leap->por;

    size_t emitted = 0;
    for (int g = 0; g < groups->count; g++) {
        int group = ci_get (groups, g);
        emitted += GBgetTransitionsLong (ctx->parent, group, src, cb, uctx);
    }
    return emitted;
}

static inline size_t
leap_level (leap_t *leap, size_t round)
{
    ci_list            *groups = leap->lists[round];

    TransitionCB        cb;
    void               *uctx;
    if (leap->round == 1) { // POR layer takes care of LTL cycle proviso
        cb = leap->ucb;
        uctx = leap->uctx;
        Printf (debug, "Emitting: ");
    } else if (round + 1 == leap->round) {
        cb = forward_cb;
        uctx = leap;
        Printf (debug, "Forwarding: ");
    } else {
        cb = chain_cb;
        uctx = leap->inout[1];
        HREassert (dfs_stack_size(leap->inout[1]) == 0);
        Printf (debug, "Following: ");
    }
    ci_debug (groups);
    Printf (debug, " (%zu < %zu)\n", round, leap->round);

    // process stack in order
    size_t              emitted = 0;
    for (size_t s = 0; s < dfs_stack_size(leap->inout[0]); s++) {
        int                *next = dfs_stack_index (leap->inout[0], s);
        emitted += leap_do_emit (leap, next, groups, cb, uctx);
    }
    dfs_stack_clear (leap->inout[0]); // processed in stack

    if (round + 1 == leap->round) {
        return emitted;
    } else {
        swap (leap->inout[0], leap->inout[1]);
        return leap_level (leap, round + 1);
    }
}

static inline size_t
leap_emit (leap_t *leap, int *src)
{
    size_t              emitted;
    HREassert (dfs_stack_size(leap->inout[0]) == 0);
    dfs_stack_push (leap->inout[0], src);

    emitted = leap_level (leap, 0);

    dfs_stack_clear (leap->inout[1]);

    return emitted;
}

static void
leap_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    leap_t             *leap = (leap_t *) context;
    ci_list            *groups = leap->lists[leap->round];

    size_t              c = ci_count(groups);
    ci_add_if (groups, ti->group, c == 0 || ci_top(groups) != ti->group);

    (void) cpy; (void) dst;
}

static bool
conjoin_lists (leap_t *leap, ci_list *groups)
{
    ci_list            *all = leap->lists[leap->groups];
    bool                visible = false;
    ci_sort (all);
    for (int g = 0; g < groups->count; g++) {
        int                 group = ci_get(groups, g);
        visible |= bms_has (leap->por->visible, VISIBLE, group);
        if (ci_binary_search (all, group) != -1) {
            Printf (debug, "Found overlapping stubborn set: ");
            ci_debug (groups);
            Printf (debug, " (%d)\n", ci_count(groups));
            return false;
        }
    }
    if ((SAFETY || PINS_LTL) && visible && leap->visible) {
        Printf (debug, "Found second visible stubborn set: ");
        ci_debug (groups);
        Printf (debug, " (%d)\n", ci_count(groups));
        return false;
    }
    leap->visible |= visible;

    for (int g = 0; g < groups->count; g++) {
        int                 group = ci_get(groups, g);
        ci_add (all, group);
    }
    Printf (debug, "Found leaping stubborn set: ");
    ci_debug (groups);
    Printf (debug, " (%d%s)\n", ci_count(groups), (visible ? " visible" : ""));
    return true;
}

static size_t
handle_proviso (model_t self, int *src, size_t total)
{
    por_context        *ctx = ((por_context *)GBgetContext(self));
    leap_t             *leap = ctx->leap;
    // 1 round is taken care of by POR layer
    if ((PINS_LTL && leap->proviso.por_proviso_false_cnt != 0) ||
        (SAFETY && leap->proviso.por_proviso_true_cnt != 0)) {
        // also emit all single steps (probably results in horrid reductions)
        total += GBgetTransitionsAll (GBgetParent(self), src, leap->ucb,
                                                              leap->uctx);
    }
    return total;
}

int
leap_search_all (model_t self, int *src, TransitionCB cb, void *uctx)
{
    por_context        *ctx = ((por_context *)GBgetContext(self));
    leap_t             *leap = ctx->leap;
    size_t              total;
    size_t              stubborn, cross = 1;
    bool                disjoint;

    ci_list            *all = leap->lists[leap->groups];
    all->count = 0;
    leap->visible = false;
    leap->proviso.por_proviso_false_cnt = 0;
    leap->proviso.por_proviso_true_cnt = 0;
    leap->round = 0;
    while (true) {
        ci_list            *groups = leap->lists[leap->round];
        ci_clear (groups);

        por_exclude (ctx, all);
        stubborn = leap->next_por (self, src, leap_cb, leap);
        por_exclude (ctx, NULL);

        HREassert (!stubborn == 0 || leap->round == 0);
        if (stubborn == 0) return 0;

        disjoint = conjoin_lists (leap, groups);
        if (!disjoint) break;
        cross *= stubborn;
        leap->round++;
    }

    leap->uctx = uctx;
    leap->ucb = cb;
    total = leap_emit (leap, src);
    if (!SAFETY && !PINS_LTL) {
        HREassert (total == cross, "Wrong number of sets %zu, expected %zu (rounds: %zu)",
                   total, cross, leap->round);
    } else if (leap->round > 1) { // 1 round is taken care of by POR layer
        total = handle_proviso (self, src, total);
    }
    return total;
}

static void
set_row (matrix_t *dm, int row)
{
    for (int i = 0; i < dm_ncols (dm); i++) {
        dm_set (dm, row, i);
    }
}

static matrix_t *
enlarge_matrix (matrix_t *dm, int add_rows, int add_cols)
{
    matrix_t *new = RTmalloc (sizeof(matrix_t));
    dm_create (new, dm_nrows(dm) + add_rows, dm_ncols(dm) + add_cols);
    for (int i = 0; i < dm_nrows (dm); i++) {
        for (int j = 0; j < dm_ncols(dm); j++) {
            if (dm_is_set(dm, i, j)) {
                dm_set (new, i, j);
            }
        }
    }
    return new;
}

static void
add_leap_group (model_t *por_model, model_t pre_por)
{
    matrix_t *dm = enlarge_matrix (GBgetDMInfo(pre_por), 1, 0);
    set_row (dm, dm_nrows (dm) - 1);
    GBsetDMInfo (*por_model, dm);

    dm = enlarge_matrix (GBgetDMInfoMayWrite(pre_por), 1, 0);
    set_row (dm, dm_nrows (dm) - 1);
    GBsetDMInfoMayWrite (*por_model, dm);

    dm = enlarge_matrix (GBgetDMInfoMustWrite(pre_por), 1, 0);
    set_row (dm, dm_nrows (dm) - 1);
    GBsetDMInfoMustWrite (*por_model, dm);

    dm = enlarge_matrix (GBgetDMInfoRead(pre_por), 1, 0);
    set_row (dm, dm_nrows (dm) - 1);
    GBsetDMInfoRead (*por_model, dm);

    // TODO: Leaping POR does not modify guard-dependencies and commutativity info yet.
    // assuming higher layers are not using that
}

leap_t *
leap_create_context (model_t *por_model, model_t pre_por,
                     next_method_black_t next_all)
{
    por_context *ctx = GBgetContext (*por_model);

    leap_t             *leap = RTmalloc (sizeof(leap_t));
    leap->next_por = next_all;
    leap->slots = pins_get_state_variable_count (ctx->parent);
    leap->inout[0] = dfs_stack_create (leap->slots);
    leap->inout[1] = dfs_stack_create (leap->slots);
    leap->groups = pins_get_group_count (ctx->parent);
    leap->lists = RTmalloc (sizeof(ci_list **[leap->groups + 1]));
    for (size_t i = 0; i < leap->groups + 1; i++)
        leap->lists[i] = ci_create (leap->groups);
    leap->por = ctx;

    add_leap_group (por_model, pre_por);
    return leap;
}

bool
leap_is_stubborn (por_context *ctx, int group)
{
    HREassert (false, "unimplemented for leaping POR");
    return false;
    (void) ctx; (void) group;
}
