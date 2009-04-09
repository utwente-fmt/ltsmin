#include <stdio.h>
#include "greybox.h"
#include "runtime.h"
#include "dm/dm.h"

// this part should be encapsulated in greybox.h
#include "dynamic-array.h"
#include "lts-type.h"

typedef struct group_context {
    model_t             parent;
    int                 len;
    int                *transbegin;
    int                *transmap;
    int                *statemap;
    TransitionCB        cb;
    void               *user_context;
}                  *group_context_t;


static void
group_cb (void *context, int *labels, int *olddst)
{
    group_context_t     ctx = (group_context_t)(context);
    int                 len = ctx->len;
    int                 newdst[len];
    for (int i = 0; i < len; i++)
        newdst[i] = olddst[ctx->statemap[i]];
    ctx->cb (ctx->user_context, labels, newdst);
}

static int
group_long (model_t self, int group, int *newsrc, TransitionCB cb,
            void *user_context)
{
    group_context_t     ctx = (group_context_t)GBgetContext (self);
    model_t             parent = ctx->parent;
    int                 len = ctx->len;
    int                 oldsrc[len];
    int                 Ntrans = 0;
    int                 begin = ctx->transbegin[group];
    int                 end = ctx->transbegin[group + 1];
    ctx->cb = cb;
    ctx->user_context = user_context;

    for (int i = 0; i < len; i++)
        oldsrc[ctx->statemap[i]] = newsrc[i];

    for (int j = begin; j < end; j++) {
        int                 g = ctx->transmap[j];
        Ntrans += GBgetTransitionsLong (parent, g, oldsrc, group_cb, ctx);
    }

    return Ntrans;
}

int
max_row_first (matrix_t *m, int rowa, int rowb)
{
    int                 i,
                        ra,
                        rb;

    for (i = 0; i < dm_ncols (m); i++) {
        ra = dm_is_set (m, rowa, i);
        rb = dm_is_set (m, rowb, i);

        if ((ra && rb) || (!ra && !rb))
            continue;
        return (rb - ra);
    }

    return 0;
}

model_t
GBregroup (model_t model, char *group_spec)
{
    // note: context information is available via matrix, doesn't need to
    // be stored again
    (void)group_spec;                  // ignore warning
    matrix_t           *m = RTmalloc (sizeof (matrix_t));

    dm_copy (GBgetDMInfo (model), m);

    dm_sort_rows (m, &max_row_first);
    dm_nub_rows (m);
    dm_optimize (m);
    dm_sort_rows (m, &max_row_first);

    // create new model
    model_t             group = GBcreateBase ();
    GBcopyChunkMaps (group, model);
    GBsetLTStype (group, GBgetLTStype (model));
        
    struct group_context *ctx = RTmalloc (sizeof *ctx);

    ctx->parent = model;
    GBsetContext (group, ctx);
    
    GBsetNextStateLong (group, group_long);

    // fill statemapping: assumption this is a bijection
    {
        int                 Nparts = dm_ncols (m);
        if (Nparts != lts_type_get_state_length (GBgetLTStype (model)))
            Fatal (-1, error,
                   "state mapping in file doesn't match the specification");
        ctx->len = Nparts;
        ctx->statemap = RTmalloc (Nparts * sizeof (int));
        for (int i = 0; i < Nparts; i++) {
            int                 s = m->col_perm.data[i].becomes;
            ctx->statemap[i] = s;
        }
    }

    // fill transition mapping: assumption: this is a surjection
    {
        int                 oldNgroups = dm_nrows (GBgetDMInfo (model));
        int                 newNgroups = dm_nrows (m);
        Warning (info, "Regrouping: %d->%d groups", oldNgroups,
                 newNgroups);
        ctx->transbegin = RTmalloc ((1 + newNgroups) * sizeof (int));
        ctx->transmap = RTmalloc (oldNgroups * sizeof (int));
        int                 p = 0;
        for (int i = 0; i < newNgroups; i++) {
            int                 first = m->row_perm.data[i].becomes;
            int                 all_in_group = first;
            // count
            int                 n = 0;
            do {
                n++;
                all_in_group = m->row_perm.data[all_in_group].group;
            }
            while (all_in_group != first);

            ctx->transbegin[i] = p;
            int                 j = 0;
            do {
                ctx->transmap[p + j] = all_in_group;
                j++;
                all_in_group = m->row_perm.data[all_in_group].group;
            } while (all_in_group != first);
            p = p + n;
        }
        ctx->transbegin[newNgroups] = p;
    }

    // fill edge_info
    GBsetDMInfo (group, m);

    GBinitModelDefaults (&group, model);

    // permute initial state
    {
        int                 len = ctx->len;
        int                 s0[len],
                            news0[len];
        GBgetInitialState (model, s0);
        for (int i = 0; i < len; i++)
            news0[i] = s0[ctx->statemap[i]];
        GBsetInitialState (group, news0);
    }

    // not supported yet (should permute states)
    GBsetStateLabelShort (group, NULL);
    GBsetStateLabelLong (group, NULL);
    GBsetStateLabelsAll (group, NULL);

    // who is responsible for freeing matrix_t dm_info in group?
    // probably needed until program termination
    return group;
}
