#include <stdio.h>
#include "greybox.h"
#include "aterm2.h"
#include "runtime.h"

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
} *group_context_t;


static void
group_cb (void *context, int *labels, int *olddst)
{
    group_context_t     ctx = (group_context_t) (context);
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
    group_context_t     ctx = (group_context_t) GBgetContext (self);
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

model_t
GBregroup (model_t model, char *group_spec)
{
    FILE               *fp = fopen (group_spec, "r");
    if (!fp)
        Fatal (-1, error, "Group specification file not found: %s",
               group_spec);

    model_t             group = GBcreateBase ();
    GBcopyChunkMaps (group, model);
    GBsetLTStype (group, GBgetLTStype (model));
        
    struct group_context *ctx = RTmalloc (sizeof *ctx);
    ctx->parent = model;
    GBsetContext (group, ctx);
    
    GBsetNextStateLong (group, group_long);

    // not supported yet (should permute states)
    GBsetStateLabelShort (group, NULL);
    GBsetStateLabelLong (group, NULL);
    GBsetStateLabelsAll (group, NULL);

    // fill statemapping: assumption this is a bijection
    {
        ATermList           statemapping = (ATermList) ATreadFromFile (fp);
        int                 Nparts = ATgetLength (statemapping);
        if (Nparts != lts_type_get_state_length (GBgetLTStype (model)))
            Fatal (-1, error,
                   "state mapping in file doesn't match the specification");
        ctx->len = Nparts;
        ctx->statemap = RTmalloc (Nparts * sizeof (int));
        for (int i = 0; i < Nparts; i++) {
            ATerm               first = ATgetFirst (statemapping);
            int                 s =
                ATgetInt ((ATermInt)
                          ATgetFirst ((ATermList)
                                      ATgetArgument (first, 1)));
            ctx->statemap[i] = s;
            statemapping = ATgetNext (statemapping);
        }
    }

    // fill transition mapping: assumption: this is a surjection
    {
        ATermList           transmapping = (ATermList) ATreadFromFile (fp);
        int                 oldNgroups = GBgetEdgeInfo (model)->groups;
        int                 newNgroups = ATgetLength (transmapping);
        Warning (info, "Regrouping: %d->%d groups", oldNgroups,
                 newNgroups);
        ctx->transbegin = RTmalloc ((1 + newNgroups) * sizeof (int));
        ctx->transmap = RTmalloc (oldNgroups * sizeof (int));
        int                 p = 0;
        for (int i = 0; i < newNgroups; i++) {
            ATerm               first = ATgetFirst (transmapping);
            ATermList           tail =
                (ATermList) ATgetArgument (first, 1);
            int                 n = ATgetLength (tail);
            ctx->transbegin[i] = p;
            for (int j = 0; j < n; j++) {
                ctx->transmap[p + j] =
                    ATgetInt ((ATermInt) ATgetFirst (tail));
                tail = ATgetNext (tail);
            }
            p = p + n;
            transmapping = ATgetNext (transmapping);
        }
        ctx->transbegin[newNgroups] = p;
    }

    // fill edge_info
    {
        ATermList           dependencies = (ATermList) ATreadFromFile (fp);
        int                 newNgroups = ATgetLength (dependencies);
        edge_info_t         e_info =
            (edge_info_t) RTmalloc (sizeof (struct edge_info));
        e_info->groups = newNgroups;
        e_info->length = RTmalloc (newNgroups * sizeof (int));
        e_info->indices = RTmalloc (newNgroups * sizeof (int *));

        for (int i = 0; i < newNgroups; i++) {
            ATermList           deps =
                (ATermList) ATgetArgument (ATgetFirst (dependencies), 1);
            int                 n = ATgetLength (deps);
            e_info->length[i] = n;
            e_info->indices[i] = RTmalloc (n * sizeof (int));
            for (int j = 0; j < n; j++) {
                int                 d =
                    ATgetInt ((ATermInt) ATgetFirst (deps));
                e_info->indices[i][j] = d;
                deps = ATgetNext (deps);
            }
            dependencies = ATgetNext (dependencies);
        }
        GBsetEdgeInfo (group, e_info);
    }

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

    return group;
}
