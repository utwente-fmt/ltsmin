#include <hre/config.h>

#include <stdlib.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/por/por-ample.h>
#include <pins-lib/por/por-lipton.h>
#include <pins-lib/por/por-internal.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/pins-util.h>
#include <util-lib/bitmultiset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>


/**
 * DELETION algorithm.
 */


struct lipton_ctx_s {
    int                 transaction_group;
    process_t          *procs;
    int                *g2p;
    size_t              num_procs;
    ci_list           **pnext;       // group --> group (enabling)
    ci_list           **both;
    ci_list           **left;
    ci_list           **rght;
    ci_list           **pboth;
    ci_list           **pleft;
    ci_list           **prght;

    dfs_stack_t         level[2];
    TransitionCB        ucb;
    void               *uctx;
};

typedef enum {
    PRE__COMMIT,
    POST_COMMIT
} commit_e;

void
hook_cb (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    lipton_ctx_t       *lipton = (lipton_ctx_t *) context;
    dfs_stack_push (lipton->level, dst);
}

static inline int
lipton_commutes (lipton_ctx_t *lipton, int i, ci_list **dir)
{
    return dir[i]->count == 0;
}

void
lipton_emit_one (lipton_ctx_t *lipton, int *dst)
{
    // TODO: ti->group, cpy, labels (also in leaping)
    transition_info_t ti = GB_TI (NULL, lipton->transaction_group);
    ti.por_proviso = 1; // for proviso true
    lipton->ucb (lipton->uctx, &ti, dst, NULL);
}

static void
lipton_init_proc_enabled (por_context* por, process_t *proc, int g, int* src)
{
    lipton_ctx_t       *lipton = (lipton_ctx_t *) por->alg;
    ci_clear (proc->en);
    ci_list* candidates = g == -1 ? proc->groups : lipton->pnext[proc->id];
    for (int* g = ci_begin (candidates); g != ci_end (candidates); g++) {
        guard_t            *guards = GBgetGuard (por->parent, *g);
        HREassert(guards != NULL, "GUARD RETURNED NULL %d", *g);
        bool                enabled = true;
        for (int j = 0; enabled && j < guards->count; j++)
            enabled &= GBgetStateLabelLong (por->parent, j, src) != 0;
        ci_add_if (proc->en, *g, enabled);
    }
}

static inline int
lipton_next (por_context *por, int *src, process_t *proc, commit_e c, int g)
{
    lipton_ctx_t       *lipton = (lipton_ctx_t *) por->alg;

    lipton_init_proc_enabled (por, proc, g, src);

    switch (c == POST_COMMIT) { // All must left-commute:
        for (int *g = ci_begin (proc->en); g != ci_end (proc->en); g++) {
            // TODO: deletion
            if (!lipton_commutes (lipton, *g, lipton->pleft)) {
                lipton_emit_one (lipton, src);
                return 1;
            }
        }
    }

    size_t              emitted = 0;
    bool                commutes;
    for (int *g = ci_begin (proc->en); g != ci_end (proc->en); g++) {

        GBgetTransitionsLong (por->parent, *g, src, hook_cb, lipton);

        commit_e            next_c = c;
        if (c == PRE__COMMIT && !lipton_commutes(lipton, *g, lipton->prght))
            next_c = POST_COMMIT;
        while (dfs_stack_size(lipton->level)) {
            int                *next = dfs_stack_pop (lipton->level);
            emitted += lipton_next (por, next, proc, next_c);
        }
    }

    return emitted;
}

static inline int
lipton_lipton (por_context *por, int *src)
{
    lipton_ctx_t           *lipton = (lipton_ctx_t *) por->alg;
    size_t                  emitted = 0;

    for (int i = 0; i < lipton->num_procs; i++) {
        emitted += lipton_next (por, src, &lipton->procs[i], PRE__COMMIT, -1);
    }

    return emitted;
}

static void
lipton_setup (model_t model, por_context *por, int *src)
{
    lipton_ctx_t       *lipton = (lipton_ctx_t *) por->alg;

    HREassert (bms_count(por->exclude, 0) == 0, "Not implemented for Lipton reduction.");
    HREassert (bms_count(por->include, 0) == 0, "Not implemented for Lipton reduction.");
}

int
lipton_por_all (model_t model, int *src, TransitionCB cb, void *user_context)
{
    por_context *por = ((por_context*)GBgetContext(model));
    lipton_setup (model, por, src);
    return lipton_lipton (por, src);
}

lipton_ctx_t *
lipton_create (por_context *por, model_t model)
{
    lipton_ctx_t *lipton = RTmalloc (sizeof(lipton_ctx_t));

    // find processes:
    lipton->g2p = RTmallocZero (sizeof(int[por->ngroups]));
    lipton->procs = identify_procs (por, &lipton->num_procs, lipton->g2p);
    lipton->level[0] = dfs_stack_create (por->nslots);
    lipton->level[1] = dfs_stack_create (por->nslots);

    leap_add_leap_group (model, por->parent);
    lipton->transaction_group = por->ngroups;

    matrix_t nes;
    dm_create (&nes, por->ngroups, por->ngroups);
    for (int g = 0; g < por->ngroups; g++) {
        int i = lipton->g2p[g];
        for (int h = 0; h < por->ngroups; h++) {
            int j = lipton->g2p[h];
            if (h != j) continue;
            if (guard_of (por, g, &por->label_nes_matrix, h)) {
                dm_set (&nes, g, h);
            }
        }
    }
    lipton->pnext = (ci_list **) dm_cols_to_idx_table (&nes);
    dm_free (&nes);

    matrix_t both;
    matrix_t left;
    dm_create (&both, por->ngroups, por->ngroups);
    for (int g = 0; g < por->ngroups; g++) {
        int i = lipton->g2p[g];
        for (int h = 0; h < por->ngroups; h++) {
            int j = lipton->g2p[h];
            if (i == j) continue;
            if (dm_is_set (&por->not_accords_with, g, h)) {
                dm_set (&both, g, h);
            }
            if (dm_is_set (por->nla, g, h)) {
                dm_set (&left, g, h);
            }
        }
    }
    lipton->both = (ci_list**) dm_rows_to_idx_table (&both);
    lipton->left = (ci_list**) dm_rows_to_idx_table (&left);
    lipton->rght = (ci_list**) dm_cols_to_idx_table (&left);


    matrix_t pboth;
    matrix_t pleft;
    matrix_t prght;
    dm_create (&pboth, por->ngroups, lipton->num_procs);
    dm_create (&pleft, por->ngroups, lipton->num_procs);
    dm_create (&prght, por->ngroups, lipton->num_procs);
    for (int g = 0; g < por->ngroups; g++) {
        for (int *h = ci_begin(lipton->both[g]); h != ci_end(lipton->both[g]); h++) {
            if (dm_is_set (&por->not_accords_with, g, *h)) {
                dm_set (&pboth, g, lipton->g2p[*h]);
                break;
            }
        }
        for (int *h = ci_begin(lipton->left[g]); h != ci_end(lipton->left[g]); h++) {
            if (dm_is_set (&por->not_accords_with, g, *h)) {
                dm_set (&pleft, g, lipton->g2p[*h]);
                break;
            }
        }
        for (int *h = ci_begin(lipton->rght[g]); h != ci_end(lipton->rght[g]); h++) {
            if (dm_is_set (&por->not_accords_with, g, *h)) {
                dm_set (&prght, g, lipton->g2p[*h]);
                break;
            }
        }
    }
    lipton->pboth = (ci_list**) dm_rows_to_idx_table (&pboth);
    lipton->pleft = (ci_list**) dm_rows_to_idx_table (&pleft);
    lipton->prght = (ci_list**) dm_rows_to_idx_table (&prght);


    Printf1(infoLong, "Process --> Conflict groups:\n");
    for (int g = 0; g < por->ngroups; g++) {
        Printf1(infoLong, "%3d: ", g);
        for (int *h = ci_begin(lipton->both[g]); h != ci_end(lipton->both[g]); h++) {
            Printf1(infoLong, "%3d%s,", *h, dm_is_set(&both, *h, g) ? "" : "!");
        }
        Printf1(infoLong, "\n");
    }
    dm_free (&both);
    dm_free (&left);

    return lipton;
}

bool
lipton_is_stubborn (por_context *ctx, int group)
{
    HREassert(false, "Unimplemented for Lipton reduction");
}
