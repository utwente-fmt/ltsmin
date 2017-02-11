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
    bms_t                 **lists;
    por_context            *por;
    dfs_stack_t             inout[2];
    dfs_stack_t             stack_states;
    void                   *uctx;
    TransitionCB            ucb;
    size_t                  states;
    size_t                  levels;
    size_t                  seens;
};

static void
proviso_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    leap_t             *leap = (leap_t *) context;
    // set group to the special group we added in the add_leap_group function
    transition_info_t ti_new = { ti->labels, leap->groups, ti->por_proviso };
    leap->ucb (leap->uctx, &ti_new, dst, cpy);
    // avoid full exploration (proviso is enforced later in backtrack)
    ti->por_proviso = 1;
}

static void
forward_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    leap_t             *leap = (leap_t *) context;

    // set group to the special group we added in the add_leap_group function
    transition_info_t ti_new = { ti->labels, leap->groups, ti->por_proviso };
    leap->ucb (leap->uctx, &ti_new, dst, cpy);
    leap->proviso.por_proviso_false_cnt += ti_new.por_proviso == 0;
    leap->proviso.por_proviso_true_cnt += ti_new.por_proviso != 0;
    if ((PINS_LTL || SAFETY) && ti_new.por_proviso == 0) {
        dfs_stack_push (leap->stack_states, dst);
    }
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
leap_do_emit (leap_t *leap, int *src, bms_t *groups, TransitionCB cb,
              void *uctx)
{
    por_context        *ctx = leap->por;

    size_t              emitted = 0;
    for (int g = 0; g < bms_count (groups, 0); g++) {
        int group = bms_get (groups, 0, g);
        if (CHECK_SEEN && bms_has(ctx->include, 0, group)) { // state seen:
            emitted += GBgetTransitionsLong (ctx->parent, group, src, forward_cb, uctx);
        } else {
            emitted += GBgetTransitionsLong (ctx->parent, group, src, cb, uctx);
        }
    }
    return emitted;
}

static inline size_t
leap_level (leap_t *leap, size_t round)
{
    bms_t              *groups = leap->lists[round];

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
    bms_debug (groups);
    Printf (debug, " (%zu < %zu)\n", round, leap->round);

    // process stack in order
    size_t              emitted = 0;
    for (size_t s = 0; s < dfs_stack_size(leap->inout[0]); s++) {
        int                *next = dfs_stack_index (leap->inout[0], s);
        if (CHECK_SEEN && round != 0) { // re check seen groups:
            por_seen_groups (leap->por, next, true);
        }
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
    if ((PINS_LTL || SAFETY))
        dfs_stack_clear (leap->stack_states);
    dfs_stack_push (leap->inout[0], src);

    emitted = leap_level (leap, 0);

    dfs_stack_clear (leap->inout[1]);

    return emitted;
}

static void
leap_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    leap_t             *leap = (leap_t *) context;
    bms_t              *groups = leap->lists[leap->round];

    size_t              c = bms_count (groups, 0);
    bms_push_if (groups, 0, ti->group, c == 0 || bms_top(groups, 0) != ti->group);

    (void) cpy; (void) dst;
}

static bool
conjoin_lists (leap_t *leap, bms_t *exclude, bms_t *groups, bool *second_visible)
{
    *second_visible = false;
    bool                visible = false;
    int                 group;
    bool                disjoint = true;
    int                 c = bms_count (groups, 0);
    for (int g = 0; g < c; g++) {
        group = bms_get (groups, 0, g);
        disjoint &= bms_push_new (exclude, 0, group);
        visible |= bms_has (leap->por->visible, VISIBLE, group);
    }

    if ((SAFETY || PINS_LTL) && visible && leap->visible) {
        *second_visible = true;
        bms_clear_all (groups);
    }
    if (debug) {
        Printf (debug, "Pot. leaping stubborn set: ");
        bms_debug_1 (groups, 0);
        Printf (debug, " (%d%s) %s\n", c, (visible ? " visible" : ""),
                        *second_visible ? "[SKIPPING: second visible]" :
                        (disjoint ? "[USING]" : "[SKIPPING: overlapping]"));
    }

    leap->visible |= visible;
    return disjoint;
}

static inline bool
in_leap_set (leap_t* leap, int e)
{
    for (size_t l = 0; l < leap->round; l++) {
        if (bms_has (leap->lists[l], 0, e)) return false;
    }
    return true;
}

static size_t
handle_proviso (model_t self, size_t total)
{
    por_context        *ctx = ((por_context *)GBgetContext(self));
    leap_t             *leap = ctx->leap;
    // 1 round is taken care of by POR layer
    if ((PINS_LTL && leap->proviso.por_proviso_false_cnt != 0) ||
        (SAFETY && leap->proviso.por_proviso_true_cnt != 0)) {
        // also emit all single steps (probably results in horrid reductions)
        ci_list            *en = ctx->enabled_list;
        while (dfs_stack_size(leap->stack_states)) {
            int                *next = dfs_stack_pop (leap->stack_states);
            for (int *e = ci_begin (en); e != ci_end (en); e++) {
                if (!in_leap_set (leap, *e)) {
                    total += GBgetTransitionsLong (ctx->parent, *e, next,
                                                   proviso_cb, leap->uctx);
                }
            }
            if (SAFETY) break;
        }
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
    bool                second_visible;

    bms_clear_all (ctx->exclude);
    leap->visible = false;
    leap->proviso.por_proviso_false_cnt = 0;
    leap->proviso.por_proviso_true_cnt = 0;
    leap->round = 0;
    while (true) {
        bms_t              *groups = leap->lists[leap->round];
        bms_clear_all (groups);

        stubborn = leap->next_por (self, src, leap_cb, leap);

        HREassert (stubborn != 0 || leap->round == 0);
        if (stubborn == 0) return 0;

        disjoint = conjoin_lists (leap, ctx->exclude, groups, &second_visible);
        if (!disjoint) break;
        if (second_visible) continue;
        cross *= stubborn;
        leap->round++;
    }

    leap->uctx = uctx;
    leap->ucb = cb;
    total = leap_emit (leap, src);
    if (!SAFETY && !PINS_LTL) {
        HREassert (CHECK_SEEN ? total <= cross : total == cross,
                   "Wrong number of sets %zu, expected %zu (rounds: %zu)",
                   total, cross, leap->round);
    } else if (leap->round > 1) { // 1 round is taken care of by POR layer
        total = handle_proviso (self, total);
    }
    leap->seens += cross - total;
    leap->states++;
    leap->levels += leap->round;
    Printf (debug, "---------- (avg. levels: %f%% / avg. seen: %f%%) \n",
            (float) (leap->levels / leap->states),
            (float) (leap->seens / leap->states));
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

void
leap_add_leap_group (model_t por_model, model_t pre_por)
{
    matrix_t *dm = enlarge_matrix (GBgetDMInfo(pre_por), 1, 0);
    set_row (dm, dm_nrows (dm) - 1);
    GBsetDMInfo (por_model, dm);

    dm = enlarge_matrix (GBgetDMInfoMayWrite(pre_por), 1, 0);
    set_row (dm, dm_nrows (dm) - 1);
    GBsetDMInfoMayWrite (por_model, dm);

    dm = enlarge_matrix (GBgetDMInfoMustWrite(pre_por), 1, 0);
    set_row (dm, dm_nrows (dm) - 1);
    GBsetDMInfoMustWrite (por_model, dm);

    dm = enlarge_matrix (GBgetDMInfoRead(pre_por), 1, 0);
    set_row (dm, dm_nrows (dm) - 1);
    GBsetDMInfoRead (por_model, dm);

    // TODO: Leaping POR does not modify guard-dependencies and commutativity info yet.
    // assuming higher layers are not using that
}

leap_t *
leap_create_context (model_t por_model, model_t pre_por,
                     next_method_black_t next_all)
{
    por_context *ctx = GBgetContext (por_model);

    leap_t             *leap = RTmalloc (sizeof(leap_t));
    leap->next_por = next_all;
    leap->slots = pins_get_state_variable_count (ctx->parent);
    leap->inout[0] = dfs_stack_create (leap->slots);
    leap->inout[1] = dfs_stack_create (leap->slots);
    if ((PINS_LTL || SAFETY))
        leap->stack_states = dfs_stack_create (leap->slots);
    leap->groups = pins_get_group_count (ctx->parent);
    leap->lists = RTmalloc (sizeof(bms_t **[leap->groups]));
    for (size_t i = 0; i < leap->groups; i++)
        leap->lists[i] = bms_create (leap->groups, 1);
    leap->por = ctx;
    leap->states = 0;
    leap->levels = 0;
    leap->seens = 0;

    leap_add_leap_group (por_model, pre_por);
    return leap;
}

bool
leap_is_stubborn (por_context *ctx, int group)
{
    HREassert (false, "unimplemented for leaping POR");
    return false;
    (void) ctx; (void) group;
}
