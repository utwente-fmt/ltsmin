#include <hre/config.h>

#include <stdlib.h>

#include <hre/stringindex.h>
#include <hre/user.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins2pins-cache.h>
#include <util-lib/dynamic-array.h>

static int              cache = 0;

struct poptOption cache_options[]={
    { "cache" , 'c' , POPT_ARG_VAL , &cache , 1 , "enable caching (memoization) of PINS calls" , NULL },
    POPT_TABLEEND
};

static const int EL_OFFSET = 1;

typedef struct state_data {
    int                 first;
    int                 edges;
} state_data_t;

static void
init_state_info (void *arg, void *old_array, int old_size, void *new_array,
                 int new_size)
{
    state_data_t       *array = (state_data_t *) new_array;
    while (old_size < new_size) {
        array[old_size].first = -1;
        old_size++;
    }
    (void)arg;(void)old_array;
}

/**
 *
 * Data layout:
 * dest[begin[i]]...dest[begin[i+1]-1]: successor info for state i
 * k := Nedge_labels
 * transitions i --l_x1,...,l_xk--> j_x
 * dest[begin[i]+x*(EL_OFFSET+k)]   := j_x
 * dest[begin[i]+x*(EL_OFFSET+k)+1] := l_x1
 * ...
 * dest[begin[i]+x*(EL_OFFSET+k)+k] := l_xk
 */
typedef struct group_cache {
    size_t              len;
    size_t              size;
    string_index_t      idx;
    int                 Nedge_labels;

    size_t              total_edges;
    array_manager_t     begin_man;
    state_data_t       *begin;
    array_manager_t     dest_man;
    int                *dest;
} group_cache_t;

typedef struct cache_context {
    group_cache_t *cache;
} cache_context_t;

static inline int
edge_info_sz (group_cache_t *cache)
{
    return EL_OFFSET + cache->Nedge_labels;
}

typedef struct cache_cb_s {
    group_cache_t      *cache;
    state_data_t       *src;
} cache_cb_t;

static void
add_cache_entry (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    cache_cb_t         *cbctx = (cache_cb_t *)context;
    group_cache_t      *ctx = cbctx->cache;
    state_data_t       *src = cbctx->src;
    int                 dst_index = SIputC (ctx->idx, (char *)dst, ctx->size);
    
    int                 offset = src->first + src->edges * edge_info_sz(ctx);
    ensure_access (ctx->dest_man, offset + edge_info_sz(ctx));

    int *pe_info = &ctx->dest[offset];
    *pe_info = dst_index;
    if (ti->labels != NULL)
        memcpy(pe_info + EL_OFFSET, ti->labels, ctx->Nedge_labels * sizeof *pe_info);

    src->edges++;
}

static int
cached_short (model_t self, int group, int *src, TransitionCB cb,
              void *user_context)
{
    cache_context_t    *ctx = (cache_context_t *)GBgetContext (self);
    group_cache_t      *cache = &(ctx->cache[group]);

    int                 dst[cache->len];
    int                 src_idx = SIputC (cache->idx, (char *)src, cache->size);

    ensure_access (cache->begin_man, src_idx);
    state_data_t       *state = &cache->begin[src_idx];
    if (state->first == -1) {
        int                 trans;
        cache_cb_t          cbctx = { cache, state };
        state->first = cache->total_edges * edge_info_sz (cache);
        state->edges = 0;
        trans = GBgetTransitionsShort (GBgetParent(self), group, src, add_cache_entry, &cbctx);
        HREassert (trans == state->edges, "Front-end returns wrong "
                "number of edges: %d in stead of %d", trans, state->edges);
        cache->total_edges += state->edges;
    }

    int                *labels;
    int                 N = state->edges;
    for (int i = state->first; N > 0; N--, i += edge_info_sz (cache)) {
        // MW: remove if edge label becomes "const int *"?
        memcpy (dst, SIgetC (cache->idx, cache->dest[i], NULL), cache->size);
        labels = cache->Nedge_labels == 0 ? NULL : &(cache->dest[i+EL_OFFSET]);
        transition_info_t       cbti = GB_TI(labels, group);
        cb (user_context, &cbti, dst, NULL);
    }
    return state->edges;
}

static int
cached_groups_of_edge (model_t self, int edgeno, int index, int** groups)
{
    return GBgroupsOfEdge(GBgetParent(self), edgeno, index, groups);
}

model_t
GBaddCache (model_t model)
{
    if (cache == 0) return model;

    HREassert (model != NULL, "No model");
    matrix_t           *p_dm = GBgetDMInfo (model);
    int                 N = dm_nrows (p_dm);
    group_cache_t      *cache = RTmalloc (sizeof (group_cache_t[N]));
    for (int i = 0; i < N; i++) {
        int len = dm_ones_in_row (p_dm, i);
        cache[i].len = len;
        cache[i].size = sizeof(int[len]);
        cache[i].idx = SIcreate ();
        cache[i].total_edges = 0;
        cache[i].begin_man = create_manager (256);
        cache[i].begin = NULL;
        add_array (cache[i].begin_man, (void*)&(cache[i].begin),
                   sizeof(state_data_t), init_state_info, NULL);
        cache[i].dest_man = create_manager (256);
        cache[i].Nedge_labels = lts_type_get_edge_label_count (GBgetLTStype(model));
        cache[i].dest = NULL;
        ADD_ARRAY (cache[i].dest_man, cache[i].dest, int);
    }
    cache_context_t    *ctx = RTmalloc (sizeof(cache_context_t));
    model_t             cached = GBcreateBase ();
    ctx->cache = cache;
    
    GBsetDMInfo (cached, p_dm);
    GBsetContext (cached, ctx);
    
    GBsetNextStateShort (cached, cached_short);
    GBsetGroupsOfEdge (cached, cached_groups_of_edge);

    GBinitModelDefaults (&cached, model);

    int                 len = lts_type_get_state_length (GBgetLTStype (model));
    int                 s0[len];
    GBgetInitialState (model, s0);
    GBsetInitialState (cached, s0);

    return cached;
}

