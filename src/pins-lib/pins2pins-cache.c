#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <pins-lib/pins.h>
#include <util-lib/dynamic-array.h>
#include <hre/stringindex.h>

static const int EL_OFFSET = 1;

struct group_cache {
    int                 len;
    string_index_t      idx;
    int                 explored;
    int                 visited;
    array_manager_t     begin_man;
    int                *begin;
    array_manager_t     dest_man;
    int                 Nedge_labels;
    int                *dest;
    /* Data layout:
     * dest[begin[i]]...dest[begin[i+1]-1]: successor info for state i
     * k := Nedge_labels
     * transitions i --l_x1,...,l_xk--> j_x
     * dest[begin[i]+x*(EL_OFFSET+k)]   := j_x
     * dest[begin[i]+x*(EL_OFFSET+k)+1] := l_x1
     * ...
     * dest[begin[i]+x*(EL_OFFSET+k)+k] := l_xk
     */

    /* int len; */
    /* TransitionCB cb; */
    /* void*user_context; */
};

struct cache_context {
    struct group_cache *cache;
};

static inline int
edge_info_sz (struct group_cache *cache)
{
    return EL_OFFSET + cache->Nedge_labels;
}

static void
add_cache_entry (void *context, transition_info_t *ti, int *dst)
{
    struct group_cache *ctx = (struct group_cache *)context;
    int                 dst_index =
        SIputC (ctx->idx, (char *)dst, ctx->len);
    if (dst_index >= ctx->visited)
        ctx->visited = dst_index + 1;
    ensure_access (ctx->dest_man, ctx->begin[ctx->explored]+edge_info_sz(ctx));

    int *pe_info = &ctx->dest[ctx->begin[ctx->explored]];
    *pe_info = dst_index;
    if (ti->labels != NULL)
        memcpy(pe_info + EL_OFFSET, ti->labels, ctx->Nedge_labels * sizeof *pe_info);

    ctx->begin[ctx->explored] += edge_info_sz(ctx);
}

static int
cached_short (model_t self, int group, int *src, TransitionCB cb,
              void *user_context)
{
    struct cache_context *ctx =
        (struct cache_context *)GBgetContext (self);
    struct group_cache *cache = &(ctx->cache[group]);
    int len = dm_ones_in_row(GBgetDMInfo(self), group);

    int                 tmp[len];
    int                 src_idx =
        SIputC (cache->idx, (char *)src, cache->len);

    if (src_idx == cache->visited) {
        cache->visited++;
        while (cache->explored < cache->visited) {
            // MW: remove if edge label becomes "const int *"?
            memcpy (tmp, SIgetC (cache->idx, cache->explored, NULL),
                    cache->len);
            cache->explored++;
            ensure_access (cache->begin_man, cache->explored);
            cache->begin[cache->explored] =
                cache->begin[cache->explored - 1];
            GBgetTransitionsShort (GBgetParent(self), group, tmp,
                                   add_cache_entry, cache);
        }
    }
    for (int i = cache->begin[src_idx]; i < cache->begin[src_idx + 1];
         i += edge_info_sz (cache)) {
        // MW: remove if edge label becomes "const int *"?
        memcpy (tmp, SIgetC (cache->idx, cache->dest[i], NULL),
                cache->len);
        int *labels = cache->Nedge_labels == 0 ? NULL : &(cache->dest[i+EL_OFFSET]);
        transition_info_t cbti = GB_TI(labels, group);
        cb (user_context, &cbti, tmp);
    }
    return (cache->begin[src_idx + 1] - cache->begin[src_idx]) /
        edge_info_sz (cache);
}

static int
cached_transition_in_group (model_t self, int* labels, int group)
{
  return GBtransitionInGroup(GBgetParent(self), labels, group);
}

model_t
GBaddCache (model_t model)
{
    HREassert (model != NULL, "No model");
    matrix_t           *p_dm = GBgetDMInfo (model);
    matrix_t           *p_dm_read = GBgetDMInfoRead (model);
    matrix_t           *p_dm_write = GBgetDMInfoWrite (model);
    int                 N = dm_nrows (p_dm);
    struct group_cache *cache = RTmalloc (N * sizeof (struct group_cache));
    for (int i = 0; i < N; i++) {
        int                 len = dm_ones_in_row (p_dm, i);
        cache[i].len = len * sizeof (int);
        cache[i].idx = SIcreate ();
        cache[i].explored = 0;
        cache[i].visited = 0;
        cache[i].begin_man = create_manager (256);
        cache[i].begin = NULL;
        ADD_ARRAY (cache[i].begin_man, cache[i].begin, int);
        cache[i].begin[0] = 0;
        cache[i].dest_man = create_manager (256);
        cache[i].Nedge_labels = lts_type_get_edge_label_count(GBgetLTStype(model));
        cache[i].dest = NULL;
        ADD_ARRAY (cache[i].dest_man, cache[i].dest, int);
    }
    struct cache_context *ctx = RTmalloc (sizeof *ctx);
    model_t             cached = GBcreateBase ();
    ctx->cache = cache;

    GBsetDMInfo (cached, p_dm);
    GBsetDMInfoRead (cached, p_dm_read);
    GBsetDMInfoWrite (cached, p_dm_write);
    GBsetContext (cached, ctx);

    GBsetNextStateShort (cached, cached_short);
    GBsetTransitionInGroup (cached, cached_transition_in_group);

    GBinitModelDefaults (&cached, model);

    int                 len =
        lts_type_get_state_length (GBgetLTStype (model));
    int                 s0[len];
    GBgetInitialState (model, s0);
    GBsetInitialState (cached, s0);

    return cached;
}
