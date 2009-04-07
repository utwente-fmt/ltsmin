#include <stdlib.h>

#include "greybox.h"
#include "runtime.h"
#include "dynamic-array.h"
#include "stringindex.h"

struct group_cache {
    int                 len;
    string_index_t      idx;
    int                 explored;
    int                 visited;
    array_manager_t     begin_man;
    int                *begin;
    array_manager_t     dest_man;
    int                *label;
    int                *dest;
    /* int len; */
    /* TransitionCB cb; */
    /* void*user_context; */
};

struct cache_context {
    model_t             parent;
    struct group_cache *cache;
};

static void
add_cache_entry (void *context, int *label, int *dst)
{
    struct group_cache *ctx = (struct group_cache *)context;
    int                 dst_index = SIputC (ctx->idx, (char *)dst, ctx->len);
    if (dst_index >= ctx->visited)
        ctx->visited = dst_index + 1;
    ensure_access (ctx->dest_man, ctx->begin[ctx->explored]);
    ctx->label[ctx->begin[ctx->explored]] = label[0];
    ctx->dest[ctx->begin[ctx->explored]] = dst_index;
    ctx->begin[ctx->explored]++;
}

#if 0
static void
check_cache (void *context, int *label, int *dst)
{
    struct
    group_cache        *ctx = (struct group_cache *)context;
    int                 dst_index = TreeFold (ctx->dbs, dst);
    Warning (info, "==%d=>%d", label[0], dst_index);
    for (int i = 0; i < ctx->len; i++)
        printf ("%3d", dst[i]);
    printf ("\n");
    ctx->cb (ctx->user_context, label, dst);
}
#endif

static int
cached_short (model_t self, int group, int *src, TransitionCB cb,
              void *user_context)
{
    struct cache_context *ctx =
        (struct cache_context *)GBgetContext (self);
    struct group_cache *cache = &(ctx->cache[group]);
    int                 len = GBgetEdgeInfo (self)->length[group];
    int                 tmp[len];
    int                 src_idx =
        SIputC (cache->idx, (char *)src, cache->len);

    if (src_idx == cache->visited) {
        cache->visited++;
        while (cache->explored < cache->visited) {
            memcpy (tmp, SIgetC (cache->idx, cache->explored, NULL),
                    cache->len);
            cache->explored++;
            ensure_access (cache->begin_man, cache->explored);
            cache->begin[cache->explored] =
                cache->begin[cache->explored - 1];
            GBgetTransitionsShort (ctx->parent, group, tmp,
                                   add_cache_entry, cache);
        }
    }
    for (int i = cache->begin[src_idx]; i < cache->begin[src_idx + 1]; i++) {
        memcpy (tmp, SIgetC (cache->idx, cache->dest[i], NULL),
                cache->len);
        cb (user_context, &(cache->label[i]), tmp);
    }
    return (cache->begin[src_idx + 1] - cache->begin[src_idx]);
}

model_t
GBaddCache (model_t model)
{
    model_t             cached = GBcreateBase ();
    struct cache_context *ctx = RTmalloc (sizeof *ctx);
    edge_info_t         e_info = GBgetEdgeInfo (model);
    int                 N = e_info->groups;
    struct group_cache *cache = RTmalloc (N * sizeof *(ctx->cache));
    for (int i = 0; i < N; i++) {
        int                 len = e_info->length[i];
        cache[i].len = len * sizeof (int);
        cache[i].idx = SIcreate ();
        cache[i].explored = 0;
        cache[i].visited = 0;
        cache[i].begin_man = create_manager (256);
        cache[i].begin = NULL;
        ADD_ARRAY (cache[i].begin_man, cache[i].begin, int);
        cache[i].begin[0] = 0;
        cache[i].dest_man = create_manager (256);
        cache[i].label = NULL;
        ADD_ARRAY (cache[i].dest_man, cache[i].label, int);
        cache[i].dest = NULL;
        ADD_ARRAY (cache[i].dest_man, cache[i].dest, int);
    }
    ctx->cache = cache;
    ctx->parent = model;

    GBcopyChunkMaps (cached, model);
    GBsetLTStype (cached, GBgetLTStype (model));
    GBsetEdgeInfo (cached, e_info);

    int                 len =
        lts_type_get_state_length (GBgetLTStype (model));
    int                 s0[len];
    GBgetInitialState (model, s0);
    GBsetInitialState (cached, s0);

    GBsetContext (cached, ctx);
    GBsetNextStateShort (cached, cached_short);

    return cached;
}
