#include <hre/config.h>

#include <stdlib.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/por/por-ample.h>
#include <pins-lib/por/por-deletion.h>
#include <pins-lib/por/por-lipton.h>
#include <pins-lib/por/por-internal.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/pins-util.h>
#include <util-lib/bitmultiset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>


/**
 * LIPTON reduction
 */

typedef enum {
    PRE__COMMIT = true,
    POST_COMMIT = false
} phase_e;

#define PROC_BITS 10
#define GROUP_BITS 21

#define GROUP_NONE ((1LL<<GROUP_BITS) - 1)

typedef struct  __attribute__((packed)) stack_data_s {
    uint32_t            group : GROUP_BITS;
    uint32_t            proc  : PROC_BITS;
    phase_e             phase : 1;
    int                 state[];
} stack_data_t;

struct lipton_ctx_s {
    por_context        *por;
    int                 lipton_group;
    process_t          *procs;
    int                *g2p;
    size_t              num_procs;
    del_ctx_t          *del;        // deletion context
    ci_list           **g_next;     // group --> group [proc internal] (enabling, inv. of NES)
    ci_list           **p_dis;      // group --> process [proc remote] (disabled, NDS)
    ci_list           **p_left_dep;
    ci_list           **p_rght_dep;
    ci_list           **not_left_accords;   // to swap out in POR layer, controlling deletion algorithm
    ci_list           **not_left_accordsn;  // to swap out in POR layer, controlling deletion algorithm

    dfs_stack_t         queue[2];
    dfs_stack_t         commit;
    TransitionCB        ucb;
    void               *uctx;
    stack_data_t       *src;
    size_t              emitted;
    size_t              depth;
    bool                commutes_left;
    bool                commutes_right;
};

static inline int
lipton_commutes (lipton_ctx_t *lipton, int i, ci_list **dir)
{
    if (USE_DEL) {
        return dir == lipton->p_rght_dep ? lipton->commutes_right : lipton->commutes_left;
    } else {
        return dir[i]->count == 0;
    }
    (void) lipton;
}

static inline stack_data_t *
lipton_stack (lipton_ctx_t *lipton, dfs_stack_t q, int *dst, int proc,
              phase_e phase, int group)
{
    HREassert (group == GROUP_NONE || group < lipton->por->ngroups);
    stack_data_t       *data = (stack_data_t*) dfs_stack_push (q, NULL);
    memcpy (&data->state, dst, sizeof(int[lipton->por->nslots]));
    data->group = group;
    data->proc = proc;
    data->phase = phase;
    return data;
}

static inline void
lipton_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    // TODO: ti->group, cpy, labels (also in leaping)
    lipton_ctx_t       *lipton = (lipton_ctx_t*) context;
    phase_e             phase = lipton->src->phase;
    int                 proc = lipton->src->proc;
    if (phase == PRE__COMMIT && !lipton_commutes(lipton, ti->group, lipton->p_rght_dep))
        phase = POST_COMMIT;
    lipton_stack (lipton, lipton->queue[1], dst, proc, phase, ti->group);
    (void) cpy;
}

void
lipton_emit_one (lipton_ctx_t *lipton, int *dst, int group)
{
    group = lipton->depth == 1 ? group : lipton->lipton_group;
    transition_info_t       ti = GB_TI (NULL, group);
    ti.por_proviso = 1; // force proviso true
    lipton->ucb (lipton->uctx, &ti, dst, NULL);
    lipton->emitted += 1;
}

static void
lipton_init_proc_enabled (lipton_ctx_t *lipton, int *src, process_t *proc,
                          int g_prev)
{
    ci_clear (proc->en);
    model_t             model = lipton->por->parent;
    ci_list            *cands = g_prev == GROUP_NONE ? proc->groups : lipton->g_next[g_prev];
    for (int *n = ci_begin(cands); n != ci_end(cands); n++) {
        guard_t            *gs = GBgetGuard (lipton->por->parent, *n);
        HREassert(gs != NULL, "GUARD RETURNED NULL %d", *n);
        bool                enabled = true;
        for (int j = 0; enabled && j < gs->count; j++)
            enabled &= GBgetStateLabelLong (model, gs->guard[j], src) != 0;
        ci_add_if (proc->en, *n, enabled);
    }
    if (debug && g_prev != GROUP_NONE) {
        int                 count = 0;
        for (int *n = ci_begin(proc->groups); n != ci_end(proc->groups); n++) {
            guard_t            *gs = GBgetGuard (lipton->por->parent, *n);
            HREassert(gs != NULL, "GUARD RETURNED NULL %d", *n);
            int                enabled = 1;
            for (int j = 0; enabled && j < gs->count; j++)
                enabled &= GBgetStateLabelLong (model, gs->guard[j], src) != 0;
            if (enabled) HREassert (*n == proc->en->data[count++]);
        }
        HREassert (count == proc->en->count);
    }
}

static bool
lipton_calc_commutes (lipton_ctx_t *lipton, process_t *proc, ci_list **deps)
{
    por_context        *por = lipton->por;

    size_t c = 0;
    for (int *g = ci_begin(proc->en); c == 0 && g != ci_end(proc->en); g++)
        c += deps[*g]->count;
    if (c == 0) return true;

    swap (por->alg, lipton->del);  // NO DELETION CALLS BEFORE

    por->not_left_accordsn = deps == lipton->p_left_dep ?
                        lipton->not_left_accordsn : lipton->not_left_accords;
    del_por (por);
    int                 count1 = 0;
    for (int *g = ci_begin(por->enabled_list); !count1 && g != ci_end(por->enabled_list); g++) {
        count1 += lipton->g2p[*g] != proc->id && del_is_stubborn(por, *g);
    }

    int                 count2 = 0;
//    if (left) {
//        for (int *g = ci_begin(proc->en); g != ci_end(proc->en); g++) {
//            count2 += !del_is_stubborn_key(por, *g);
//        }
//    }

    swap (por->alg, lipton->del); // NO DELETION CALLS AFTER

    if (count1 > 0 || count2 > 0) {
        if (count2 > 0 && count2 != proc->en->count)
            Debug ("Lost precision in key transition handling. Implement it on a per-transition basis.");
        return false;
    }
    return true;
}

static bool //FIX: return values (N, seen, new)
lipton_gen_succs (lipton_ctx_t *lipton, stack_data_t *state)
{
    por_context        *por = lipton->por;
    int                 group = state->group; // Keep variables off of the recursive stack
    int                 phase = state->phase;
    int                *src = state->state;
    process_t          *proc = &lipton->procs[state->proc];

    if (CHECK_SEEN && lipton->depth != 0 && por_seen(src, group, true)) {
        lipton_emit_one (lipton, src, group);
        return false;
    }

    int                 seen = fset_find (proc->fset, NULL, src, NULL, true);
    HREassert (seen != FSET_FULL, "Table full");
    if (seen) return false;
    lipton_init_proc_enabled (lipton, src, proc, group);

    if (proc->en->count == 0) {  // avoid re-emition of external start state:
        if (lipton->depth != 0) lipton_emit_one (lipton, src, group);
        return false;
    }

    if (USE_DEL) {
        for (int *g = ci_begin(proc->en); g != ci_end(proc->en); g++) {
            bms_push (por->include, 0, *g);
        }
        por_init_transitions (por->parent, por, src);

        // left commutativity
        lipton->commutes_left = lipton_calc_commutes (lipton, proc, lipton->p_left_dep);
        // right commutativity
        lipton->commutes_right = lipton_calc_commutes (lipton, proc, lipton->p_rght_dep);

        bms_clear_all (por->include);
    }

    if (phase == POST_COMMIT) {
        for (int *g = ci_begin (proc->en); g != ci_end (proc->en); g++) {
            if (!lipton_commutes(lipton, *g, lipton->p_left_dep)) {
                lipton_emit_one (lipton, src, group);
                return false;
            }
        }
    }

    lipton->src = state;
    for (int *g = ci_begin (proc->en); g != ci_end (proc->en); g++) {
        GBgetTransitionsLong (por->parent, *g, src, lipton_cb, lipton);
    }

    return true;
}

static void
lipton_bfs (lipton_ctx_t *lipton) // RECURSIVE
{
    void               *state;
    while (dfs_stack_size(lipton->queue[1])) {
        swap (lipton->queue[0], lipton->queue[1]);
        lipton->depth++;
        while ((state = dfs_stack_pop (lipton->queue[0]))) {
            lipton_gen_succs (lipton, (stack_data_t *) state);
        }
    }
}

static inline int
lipton_lipton (por_context *por, int *src)
{
    lipton_ctx_t           *lipton = (lipton_ctx_t *) por->alg;
    HREassert (dfs_stack_size(lipton->queue[0]) == 0 &&
               dfs_stack_size(lipton->queue[1]) == 0 &&
               dfs_stack_size(lipton->commit  ) == 0);

    stack_data_t           *state = lipton_stack (lipton, lipton->queue[0], src,
                                                  0, PRE__COMMIT, GROUP_NONE);
    lipton->depth = 0;
    for (size_t i = 0; i < lipton->num_procs; i++) {
        fset_clear (lipton->procs[i].fset);
        state->proc = i;
        lipton_gen_succs (lipton, state);
    }
    dfs_stack_pop (lipton->queue[0]);

    lipton->emitted = 0;
    lipton_bfs (lipton);
    return lipton->emitted;
}

static void
lipton_setup (model_t model, por_context *por, TransitionCB ucb, void *uctx)
{
    HREassert (bms_count(por->exclude, 0) == 0, "Not implemented for Lipton reduction.");
    HREassert (bms_count(por->include, 0) == 0, "Not implemented for Lipton reduction.");

    lipton_ctx_t       *lipton = (lipton_ctx_t *) por->alg;

    lipton->ucb = ucb;
    lipton->uctx = uctx;
    (void) model;
}

int
lipton_por_all (model_t model, int *src, TransitionCB ucb, void *uctx)
{
    por_context *por = ((por_context*)GBgetContext(model));
    lipton_setup (model, por, ucb, uctx);
    return lipton_lipton (por, src);
}

lipton_ctx_t *
lipton_create (por_context *por, model_t model)
{
    HRE_ASSERT (GROUP_BITS + PROC_BITS + 1 == 32);
    HREassert (por->ngroups < (1LL << GROUP_BITS) - 1, // minus GROUP_NONE
               "Lipton reduction does not support more than 2^%d-1 groups", GROUP_BITS);

    lipton_ctx_t *lipton = RTmalloc (sizeof(lipton_ctx_t));

    // find processes:
    lipton->g2p = RTmallocZero (sizeof(int[por->ngroups]));
    lipton->procs = identify_procs (por, &lipton->num_procs, lipton->g2p);
    lipton->queue[0] = dfs_stack_create (por->nslots + INT_SIZE(sizeof(stack_data_t)));
    lipton->queue[1] = dfs_stack_create (por->nslots + INT_SIZE(sizeof(stack_data_t)));
    lipton->commit   = dfs_stack_create (por->nslots + INT_SIZE(sizeof(stack_data_t)));
    lipton->por = por;
    lipton->not_left_accords = por->not_left_accords;
    lipton->not_left_accordsn = por->not_left_accordsn;
    for (size_t i = 0; i < lipton->num_procs; i++) {
        lipton->procs[i].transaction = bms_create (por->ngroups, 1);
        lipton->procs[i].fset = fset_create (sizeof(int[por->nslots]), 0, 4, 10);
    }
    if (USE_DEL) {
        lipton->del = del_create (por);
    }

    leap_add_leap_group (model, por->parent);
    lipton->lipton_group = por->ngroups;

    HREassert (lipton->num_procs < (1ULL << PROC_BITS));

    matrix_t            nes;
    dm_create (&nes, por->ngroups, por->ngroups);
    for (int g = 0; g < por->ngroups; g++) {
        int             i = lipton->g2p[g];
        process_t      *p = &lipton->procs[i];
        for (int *h = ci_begin(p->groups); h != ci_end(p->groups); h++) {
            if (guard_of(por, *h, &por->label_nes_matrix, g)) {
                dm_set (&nes, g, *h);
            }
        }
        for (int *h = ci_begin(p->groups); h != ci_end(p->groups); h++) {
            if (!dm_is_set(&por->nce, g, *h)) {
                dm_set (&nes, g, *h); // self-enablement isn't captured in NES
            }
        }
    }
    lipton->g_next = (ci_list **) dm_rows_to_idx_table (&nes);
    dm_free (&nes);

    matrix_t            nds;
    dm_create (&nds, por->ngroups, por->ngroups);
    for (size_t g = 0; g < (size_t) por->ngroups; g++) {
        size_t              i = lipton->g2p[g];
        for (size_t j = 0; j < lipton->num_procs; j++) {
            if (i == j) continue;
            process_t          *o = &lipton->procs[j];
            for (int *h = ci_begin(o->groups); h != ci_end(o->groups); h++) {
                if (guard_of(por, g, &por->label_nds_matrix, *h)) {
                    dm_set (&nds, g, j);
                    break;
                }
            }
        }
    }
    lipton->p_dis = (ci_list **) dm_rows_to_idx_table (&nds);
    dm_free (&nds);

    matrix_t p_left_dep;
    matrix_t p_rght_dep;
    dm_create (&p_left_dep, por->ngroups, por->ngroups);
    dm_create (&p_rght_dep, por->ngroups, por->ngroups);
    for (size_t g = 0; g < (size_t) por->ngroups; g++) {
        size_t              i = lipton->g2p[g];
        for (size_t j = 0; j < lipton->num_procs; j++) {
            if (i == j) continue;
            process_t          *o = &lipton->procs[j];
            int                 d = 0;
            for (int *h = ci_begin(o->groups); d != 3 && h != ci_end(o->groups); h++) {
                if (dm_is_set(por->nla, g, *h)) {
                    dm_set (&p_left_dep, g, j); d |= 1;
                }
                if (dm_is_set(por->nla, *h, g)) {
                    dm_set (&p_rght_dep, g, j); d |= 2;
                }
            }
        }
    }
    lipton->p_left_dep = (ci_list**) dm_rows_to_idx_table (&p_left_dep);
    lipton->p_rght_dep = (ci_list**) dm_rows_to_idx_table (&p_rght_dep);

    Printf1(infoLong, "Process --> Left Conflict groups:\n");
    for (size_t i = 0; i < lipton->num_procs; i++) {
        Printf1(infoLong, "%3zu: ", i);
        for (int g = 0; g < por->ngroups; g++) {
            for (int *o = ci_begin(lipton->p_left_dep[g]); o != ci_end(lipton->p_left_dep[g]); o++) {
                if (*o == (int) i) {
                    Printf1(infoLong, "%3d,", g);
                }
            }
        }
        Printf1(infoLong, "\n");
    }
    dm_free (&p_left_dep);
    dm_free (&p_rght_dep);

    return lipton;
}

bool
lipton_is_stubborn (por_context *ctx, int group)
{
    HREassert(false, "Unimplemented for Lipton reduction");
    (void) ctx; (void) group;
}
