#include <hre/config.h>

#include <stdlib.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/por/por-deletion.h>
#include <pins-lib/por/por-internal.h>
#include <pins-lib/por/por-tr.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/pins-util.h>
#include <util-lib/bitmultiset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/fast_set.h>
#include <util-lib/util.h>

/**
 * LIPTON reduction
 */

typedef enum {
    PRE__COMMIT = true,
    POST_COMMIT = false
} phase_e;

typedef enum {
    SCOPE__IN = 0,
    SCOPE_OUT = 1
} scope_e;

typedef enum {
    COMMUTE_RGHT = 0,
    COMMUTE_LEFT = 1,
    COMMUTE_COUNT = 2,
} comm_e;

#define PROC_BITS 10
#define GROUP_BITS 21

#define GROUP_NONE ((1LL<<GROUP_BITS) - 1)

typedef struct  __attribute__((packed)) stack_data_s {
    uint32_t            group : GROUP_BITS;
    uint32_t            proc  : PROC_BITS;
    phase_e             phase : 1;
    int                 state[];
} stack_data_t;

typedef struct dyn_process_s {
    int                 id;
    ci_list            *groups;

    ci_list            *en;
    //fset_t             *fset;           // for detecting non-progress
} dyn_process_t;

struct tr_ctx_s {
    por_context        *por;
    int                 lipton_group;
    int                *g2p;
    dyn_process_t      *procs;
    size_t              num_procs;
    del_ctx_t          *del;        // deletion context
    matrix_t            must_dis; // group X group
    matrix_t            must_ena; // group X group
    matrix_t            equiv_one; // group X group   never codisabled on one guard's account
    ci_list           **nla[COMMUTE_COUNT];
    bms_t              *visible;
    int                 visible_initialized;

    dfs_stack_t         queue[3];
    dfs_stack_t         commit;
    TransitionCB        ucb;
    void               *uctx;
    stack_data_t       *src;
    size_t              emitted;
    size_t              depth;
    comm_e              commutes;
    ci_list            *tmp;
    size_t              max_stack;
    size_t              max_depth;
    size_t              avg_stack;
    size_t              avg_depth;
    size_t              states;
    ci_list            *en;
};

static inline stack_data_t *
tr_stack (tr_ctx_t *tr, dfs_stack_t q, int *dst, int proc, phase_e phase, int group)
{
    HREassert (group == GROUP_NONE || group < tr->por->ngroups);
    stack_data_t       *data = (stack_data_t*) dfs_stack_push (q, NULL);
    memcpy (&data->state, dst, sizeof(int[tr->por->nslots]));
    data->group = group;
    data->proc = proc;
    data->phase = phase;
    return data;
}

static inline void
tr_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    // TODO: ti->group, cpy, labels (also in leaping)
    tr_ctx_t           *tr = (tr_ctx_t*) context;
    phase_e             phase = tr->src->phase;
    int                 proc = tr->src->proc;
    tr_stack (tr, tr->queue[1], dst, proc, phase, ti->group);
    (void) cpy;
}

void
tr_emit_one (tr_ctx_t *tr, int *dst, int group)
{
    group = tr->depth == 1 ? group : tr->lipton_group;
    transition_info_t       ti = GB_TI (NULL, group);
    ti.por_proviso = 1; // force proviso true
    tr->ucb (tr->uctx, &ti, dst, NULL);
    tr->emitted += 1;
    Debugf ("EMITTED\n");
}


static inline bool
tr_same_proc_criteria (tr_ctx_t *tr, ci_list *list, int g)
{
    if (tr->g2p[g] != -1) return false; // already claimed by another proc
    for (int *h = ci_begin (list); h != ci_end (list); h++) {
        if (*h == g) continue;
        if (TR_MODE == 0) {
            if (!dm_is_set (&tr->must_dis, *h, g) ||
                !dm_is_set (&tr->must_dis, g, *h)) {
                return false;
            }
        } else if (TR_MODE == 1) {
            if (!dm_is_set (&tr->equiv_one, g, *h)) {
                return false;
            }
        } else {
            Abort ("TR: unimplemented mode: %d", TR_MODE);
        }
    }
    return true;
}

static bool
tr_does_commute (tr_ctx_t *tr, dyn_process_t *proc)
{
    por_context        *por = tr->por;
    ci_clear (tr->tmp);
    for (int *g = ci_begin(por->enabled_list); g != ci_end(por->enabled_list); g++) {
        if (tr->g2p[*g] == proc->id || !del_is_stubborn(tr->del, *g)) continue;
        // See if we can include the enabled transition:
        if (!tr_same_proc_criteria(tr, proc->en, *g)) { tr->tmp->data[0] = *g; return false; }
        ci_add (tr->tmp, *g);
    }
    for (int *g = ci_begin(tr->tmp); g != ci_end(tr->tmp); g++) {
        if (!tr_same_proc_criteria(tr, tr->tmp, *g)) { tr->tmp->data[0] = *g; return false; }
    }
    return true;
}


static bool
tr_calc_del (tr_ctx_t *tr, dyn_process_t *proc, comm_e comm)
{
    por_context        *por = tr->por;

    por->not_left_accordsn = tr->nla[comm];
    del_por (tr->del, false);
    if (debug) {
        Debugf ("TR-%d DEL in-group %d (%s)", proc->id, tr->src->group == GROUP_NONE ? - 1 : tr->src->group, comm==COMMUTE_LEFT?"left, outgoing":"right, incoming");
        Debugf (" enabled { ");
        for (int *g = ci_begin(proc->en); g != ci_end(proc->en); g++)
            Debugf ("%3d,", *g);
        Debugf ("} ");
    }

    bool                commutes = tr_does_commute (tr, proc);
    if (commutes) { // add all stubborn transitions (enabled and diabled)
        Debugf ("REDUCED with %d { ", proc->id);
        for (int *g = ci_begin(por->enabled_list); g != ci_end(por->enabled_list); g++) {
            if (!del_is_stubborn(tr->del, *g) || tr->g2p[*g] == proc->id) continue;
            HREassert (tr->g2p[*g] == -1);
            tr->g2p[*g] = proc->id;
            ci_add (proc->groups, *g);
            ci_add (proc->en, *g);
            Debugf ("%3d,", *g);
        }
        Debugf ("}\n");
    } else {
        Debugf ("CONFICT with %d\n", tr->tmp->data[0]);
    }

    return commutes;
}

/**
 * Calls the deletion algorithm to figure out whether a dependent action is
 * reachable. Dependent is left-dependent in the post-phase and right-dependent
 * in the pre-phase. However, because we check the pre-phase only after the
 * fact (after the execution of the action), we cannot use the algorithm in
 * the normal way. Instead, for the right-depenency check, we fix the
 * dependents directly in the deletion algorithm (instead of the action itself).
 * For left-commutativity, we need to check that all enabled actions commute in
 * non-stubborn futures. Therefore, we fix all enabled actions of the process in
 * that case.
 * The inclusion takes care that the algorithm does not attempt to delete
 * the process' enabled actions.
 */
static bool
tr_comm_rght (tr_ctx_t *tr, dyn_process_t *proc, int group, int *src)
{
    (void)src;
    por_context        *por = tr->por;
    if (SAFETY && bms_has(tr->visible, COMMUTE_RGHT, group)) return false;
    bms_clear_all (por->fix);
    bms_clear_all (por->include);
    for (int *g = ci_begin(proc->en); g != ci_end(proc->en); g++)
        bms_push (por->include, 0, *g);
    for (int *g = ci_begin(tr->por->not_left_accordsn[group]); g != ci_end(tr->por->not_left_accordsn[group]); g++)
        bms_push (por->fix, 0, *g);
    return tr_calc_del (tr, proc, COMMUTE_RGHT);
}

static bool
tr_comm_left (tr_ctx_t *tr, dyn_process_t *proc, int *src)
{
    (void)src;
    por_context        *por = tr->por;
    for (int *g = ci_begin (proc->en); SAFETY && g != ci_end (proc->en); g++)
        if (bms_has(tr->visible, COMMUTE_LEFT, *g)) return false;
    bms_clear_all (por->fix);
    bms_clear_all (por->include);
    for (int *g = ci_begin(proc->en); g != ci_end(proc->en); g++)
        bms_push (por->include, 0, *g);
    for (int *g = ci_begin(proc->en); g != ci_end(proc->en); g++)
        bms_push (por->fix, 0, *g);
    return tr_calc_del (tr, proc, COMMUTE_LEFT);
}

static void
tr_init_proc_enabled (tr_ctx_t *tr, int *src, dyn_process_t *proc, int g_prev)
{
    ci_clear (proc->en);
    por_context            *por = tr->por;
    por_init_transitions (por->parent, por, src);
    Debugf ("en { ");
    for (int *g = ci_begin(por->enabled_list); g != ci_end(por->enabled_list); g++) {
        ci_add_if (proc->en, *g, tr->g2p[*g] == proc->id);
        if (tr->g2p[*g] == proc->id) Debugf ("%3d,", *g);
    }
    Debugf ("}\n");
    (void) g_prev;
}

static bool //FIX: return values (N, seen, new)
tr_gen_succs (tr_ctx_t *tr, stack_data_t *state)
{
    por_context        *por = tr->por;
    int                 group = state->group; // Keep variables off of the recursive stack
    phase_e             phase = state->phase;
    int                *src = state->state;
    dyn_process_t      *proc = &tr->procs[state->proc];
    HREassert (group != GROUP_NONE);

    Debugf ("TR-%d in-group %3d ", proc->id, group == GROUP_NONE ? -1 : group);
    if (CHECK_SEEN && por_seen(src, group, true)) {
        Debugf ("SEEN ");
        tr_emit_one (tr, src, group);
        return false;
    }
    tr_init_proc_enabled (tr, src, proc, group);

    if (phase == PRE__COMMIT && !tr_comm_rght(tr, proc, group, src)) {
        phase = state->phase = POST_COMMIT;
    }
    if (proc->en->count == 0) {
        for (int *g = ci_begin(por->enabled_list); g != ci_end(por->enabled_list); g++) {
            if (!dm_is_set(&tr->must_ena, group, *g)) continue;
            tr->g2p[*g] = proc->id;
            ci_add (proc->groups, *g);
            ci_add (proc->en, *g);
            break;
        }
    }
    if (phase == POST_COMMIT && !tr_comm_left(tr, proc, src)) {
        Debugf ("TR-%d POST CONFLICT ", proc->id);
        tr_emit_one (tr, src, group);
        return false;
    }
    if (proc->en->count == 0) {
        if (phase == POST_COMMIT) {
            Debugf ("TR-%d END ", proc->id);
            tr_emit_one (tr, src, group);
        }
        return false;
    }

    Debugf ("TR-%d stepping { ", proc->id);
    tr->src = state;
    for (int *g = ci_begin(proc->en); g != ci_end(proc->en); g++) {
        GBgetTransitionsLong (por->parent, *g, src, tr_cb, tr);
        Debugf ("%3d,", *g);
    }
    Debugf ("}\n\n");
    return true;
}

static void
tr_bfs (tr_ctx_t *tr) // RECURSIVE
{
    void                   *state;
    while (dfs_stack_size(tr->queue[1])) {
        swap (tr->queue[0], tr->queue[1]);
        tr->depth++;
        tr->max_stack = max (tr->max_stack, dfs_stack_size(tr->queue[0]));
        tr->max_depth = max (tr->max_depth, tr->depth);
        while ((state = dfs_stack_pop (tr->queue[0]))) {
            tr_gen_succs (tr, (stack_data_t *) state);
        }
    }
}

static inline void
tr_init_visibles (tr_ctx_t *tr, int *src)
{
    if (tr->visible_initialized) return;
    tr->visible_initialized = 1;

    por_context        *por = tr->por;
    por_init_transitions (por->parent, por, src);
    ci_list            *vis = bms_list (por->visible_labels, 0);
    for (int* l = ci_begin (vis); l != ci_end (vis); l++) {
        for (int* g = ci_begin (por->label_nds[*l]);
                        g != ci_end (por->label_nds[*l]); g++) {
            bms_push_new (tr->visible, COMMUTE_RGHT, *g);
        }
    }
    for (int* l = ci_begin (vis); l != ci_end (vis); l++) {
        for (int* g = ci_begin (por->label_nes[*l]);
                        g != ci_end (por->label_nes[*l]); g++) {
            bms_push_new (tr->visible, COMMUTE_LEFT, *g);
        }
    }
    Print1 (infoLong, "TR visible groups: %d (right), %d (left) / %d",
            bms_count(tr->visible, COMMUTE_RGHT), bms_count(tr->visible, COMMUTE_LEFT), por->nlabels);
    bms_debug_1 (tr->visible, COMMUTE_RGHT);
    bms_debug_1 (tr->visible, COMMUTE_LEFT);
    bms_clear_all (por->visible);
    bms_clear_all (por->visible_labels);
}

static inline int
tr_lipton (por_context *por, int *src)
{
    tr_ctx_t               *tr = (tr_ctx_t *) por->alg;
    HREassert (dfs_stack_size(tr->queue[0]) == 0 &&
               dfs_stack_size(tr->queue[1]) == 0 &&
               dfs_stack_size(tr->commit  ) == 0);

    tr_init_visibles (tr, src);

    for (int i = 0; i < tr->por->ngroups; i++) tr->g2p[i] = -1;
    por_init_transitions (por->parent, por, src);
    ci_clear (tr->en);
    for (int *g = ci_begin(por->enabled_list); g != ci_end(por->enabled_list); g++) {
        ci_add (tr->en, *g);
    }

    void                   *dst;
    stack_data_t           *state = tr_stack (tr, tr->queue[2], src,
                                              0, PRE__COMMIT, GROUP_NONE);
    tr->depth = 1;
    tr->num_procs = 0;
    for (int *g = ci_begin(tr->en); g != ci_end(tr->en); g++) {
        if (tr->g2p[tr->num_procs] != -1) continue;

        dyn_process_t           *proc = &tr->procs[tr->num_procs];
        Debugf ("TR-%d initializing with group %3d\n", proc->id, *g);
        ci_clear (proc->groups);
        // fset_clear (proc->fset);
        tr->g2p[*g] = tr->num_procs;
        ci_add (proc->groups, *g);
        state->proc = tr->num_procs;

        tr->src = state;
        for (int *g = ci_begin(proc->groups); g != ci_end(proc->groups); g++) {
            GBgetTransitionsLong (por->parent, *g, src, tr_cb, tr);
        }

        swap (tr->queue[0], tr->queue[1]);
        while ((dst = dfs_stack_pop (tr->queue[0])))
            tr_gen_succs (tr, (stack_data_t *) dst);
        swap (tr->queue[0], tr->queue[1]);

        tr->num_procs++;
    }
    dfs_stack_pop (tr->queue[2]);

    swap (tr->queue[0], tr->queue[1]);
    tr->emitted = 0;
    size_t max_stack = tr->max_stack;
    size_t max_depth = tr->max_depth;
    tr->max_stack = 0;
    tr->max_depth = 0;
    tr_bfs (tr);
    tr->avg_stack += tr->max_stack;
    tr->avg_depth += tr->max_depth;
    tr->max_stack = max (tr->max_stack, max_stack);
    tr->max_depth = max (tr->max_depth, max_depth);
    return tr->emitted;
}

static void
tr_setup (model_t model, por_context *por, TransitionCB ucb, void *uctx)
{
    HREassert (bms_count(por->exclude, 0) == 0, "Not implemented for Lipton reduction.");
//    HREassert (bms_count(por->include, 0) == 0, "Not implemented for Lipton reduction.");

    tr_ctx_t               *tr = (tr_ctx_t *) por->alg;

    tr->states++;
    tr->ucb = ucb;
    tr->uctx = uctx;
    (void) model;
}

int
tr_por_all (model_t model, int *src, TransitionCB ucb, void *uctx)
{
    por_context            *por = ((por_context*)GBgetContext(model));
    tr_setup (model, por, ucb, uctx);
    return tr_lipton (por, src);
}

tr_ctx_t *
tr_create (por_context *por, model_t pormodel)
{
    HRE_ASSERT (GROUP_BITS + PROC_BITS + 1 == 32);
    HREassert (por->ngroups < (1LL << GROUP_BITS) - 1, // minus GROUP_NONE
               "Lipton reduction does not support more than 2^%d-1 groups", GROUP_BITS);
    HREassert (PINS_LTL == PINS_LTL_AUTO, "LTL currently not supported in Lipton reduction.");

    tr_ctx_t               *tr = RTmalloc (sizeof(tr_ctx_t));
    tr->queue[0] = dfs_stack_create (por->nslots + INT_SIZE(sizeof(stack_data_t)));
    tr->queue[1] = dfs_stack_create (por->nslots + INT_SIZE(sizeof(stack_data_t)));
    tr->queue[2] = dfs_stack_create (por->nslots + INT_SIZE(sizeof(stack_data_t)));
    tr->commit   = dfs_stack_create (por->nslots + INT_SIZE(sizeof(stack_data_t)));
    tr->por = por;
    tr->g2p = RTmallocZero (sizeof(int[por->ngroups]));
    tr->procs = RTmallocZero (sizeof(dyn_process_t[por->ngroups]));
    tr->tmp = ci_create (por->ngroups);
    for (int i = 0; i < por->ngroups; i++) {
//       // tr->procs[i].fset = fset_create (sizeof(int[por->nslots]), 0, 4, 10);
        tr->procs[i].groups = ci_create (por->ngroups);
        tr->procs[i].en = ci_create (por->ngroups);
        tr->procs[i].id = i;
    }
    USE_DEL = 1;
    tr->en = ci_create (por->ngroups);
    tr->del = del_create (por);
    tr->max_stack = 0;
    tr->max_depth = 0;
    tr->avg_stack = 0;
    tr->avg_depth = 0;
    tr->states = 0;

    leap_add_leap_group (pormodel, por->parent);
    tr->lipton_group = por->ngroups;

    tr->nla[COMMUTE_RGHT] = por->not_left_accords;   // TODO: Overwritten with better version to fix after-the-fact POR
    tr->nla[COMMUTE_LEFT] = por->not_left_accordsn;

    tr->visible_initialized = 0;
    tr->visible = bms_create (por->ngroups, COMMUTE_COUNT);

    int                     id = GBgetMatrixID (por->parent, LTSMIN_MUST_DISABLE_MATRIX);
    if (id == SI_INDEX_FAILED) Abort ("TR requires a must-disable matrix.");
    matrix_t               *must_disable = GBgetMatrix (por->parent, id);
    id = GBgetMatrixID (por->parent, LTSMIN_MUST_ENABLE_MATRIX);
    if (id == SI_INDEX_FAILED) Abort ("TR requires a must-enable matrix.");
    matrix_t               *must_enable = GBgetMatrix (por->parent, id);
    dm_create (&tr->must_dis, por->ngroups, por->ngroups);
    dm_create (&tr->must_ena, por->ngroups, por->ngroups);
    for (int g = 0; g < por->ngroups; g++) {
        for (int h = 0; h < por->ngroups; h++) {
            if (guard_of(por, h, must_disable, g)) {
                dm_set (&tr->must_dis, g, h);
            }
            if (guard_of(por, h, must_enable, g)) {
                dm_set (&tr->must_ena, g, h);
            }
        }
    }

    id = GBgetMatrixID (por->parent, LTSMIN_MAYBE_INV_COENANBLED);
    if (id == SI_INDEX_FAILED) Abort ("TR requires a maybe co-disabled matrix.");
    matrix_t               *ice = GBgetMatrix (por->parent, id);
    // !dm_is_set(ice, g1m g2)  <===>  forall s : g2(s) ==> g1(s)
    dm_create (&tr->equiv_one, por->ngroups, por->ngroups);
    for (int g = 0; g < por->ngroups; g++) {
        for (int h = 1; h < g; h++) {
            // look for one guard of each s.t. g1 <===> g2
            bool            equiv = false;
            for (int *l = ci_begin(por->group2guard[g]); !equiv && l != ci_end(por->group2guard[g]); l++) {
                for (int *k = ci_begin(por->group2guard[h]); !equiv && k != ci_end(por->group2guard[h]); k++) {
                    equiv |= *l == *k || (!dm_is_set(ice, *k, *l) && !dm_is_set(ice, *l, *k));
                }
            }
            if (equiv) {
                dm_set (&tr->equiv_one, g, h);
                dm_set (&tr->equiv_one, h, g);
            }
        }
        dm_set (&tr->equiv_one, g, g);
    }

//    Printf1(infoLong, "Group --> group equiv_one:\n");
//    for (int g = 0; g < por->ngroups; g++) {
//        Printf1(infoLong, "%3d: ", g);
//        for (int h = 0; h < por->ngroups; h++) {
//            if (dm_is_set(&tr->equiv_one, g, h)) Printf1(infoLong, "%3d,", h);
//        }
//        Printf1(infoLong, "\n");
//    }

    return tr;
}

bool
tr_is_stubborn (por_context *ctx, int group)
{
    HREassert(false, "Unimplemented for Lipton reduction");
    (void) ctx; (void) group;
    return false;
}

void
tr_stats (model_t model)
{
    por_context        *por = ((por_context*)GBgetContext(model));
    tr_ctx_t           *tr = (tr_ctx_t *) por->alg;
    Warning (info, "TR step size %zu max / %.3f avg, queues %zu max / %.3f avg",
             tr->max_depth, (float)(tr->avg_depth / tr->states), tr->max_stack, (float)(tr->avg_stack / tr->states))
}
