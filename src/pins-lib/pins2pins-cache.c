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

typedef struct label_data {
    int is_cached;
    int val;
} label_data_t;

static void
init_label_cache (void *arg, void *old_array, int old_size, void *new_array,
                  int new_size) {
    label_data_t        *array = (label_data_t *) new_array;
    while (old_size < new_size) {
        array[old_size].is_cached = 0;
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

typedef struct label_cache {
    size_t              len;  // length of the short vector
    size_t              size; // length of the short vector in bytes
    string_index_t      idx;  // mapping of short state -> index

    array_manager_t     label_val_man;
    label_data_t       *label_cache;
} label_cache_t;

typedef struct cache_context {
    group_cache_t *cache;
    label_cache_t *cache_labels;
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
    (void) cpy;
}

static int
cached_label_short (model_t self, int label, int *src) {
    cache_context_t    *ctx = (cache_context_t *)GBgetContext (self);
    label_cache_t      *cache = &(ctx->cache_labels[label]);

    int                 idx = SIputC (cache->idx, (char *)src, cache->size);
    ensure_access (cache->label_val_man, idx);

    if (cache->label_cache[idx].is_cached == 0) {
        cache->label_cache[idx].val = GBgetStateLabelShort (GBgetParent(self), label, src);
        cache->label_cache[idx].is_cached = 1;
    }

    return cache->label_cache[idx].val;
}

static void
get_label_group_cached(model_t model, sl_group_enum_t group, int *src, int *label) {

    sl_group_t         *label_group = GBgetStateLabelGroupInfo(model, group);
    int                 N_labels = label_group->count;
    matrix_t           *label_info = GBgetStateLabelInfo(model);

    for (int i = 0; i < N_labels; i++) {
        // convert to short state
        int             nth_label = label_group->sl_idx[i];
        int             short_len = dm_ones_in_row(label_info, nth_label);
        int             short_state[short_len];
        dm_project_vector(label_info, nth_label, src, short_state);

        label[i] = cached_label_short(model, nth_label, short_state);
    }
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

    matrix_t           *state_label_info = GBgetStateLabelInfo (model);
    int                 N_labels = dm_nrows (state_label_info);


    label_cache_t      *label_cache = RTmalloc (sizeof (label_cache_t[N_labels]));
    for (int i = 0; i < N_labels; i++) {
        int len = dm_ones_in_row(state_label_info, i);
        label_cache[i].len = len;
        label_cache[i].size = sizeof(int[len]);
        label_cache[i].idx = SIcreate ();
        label_cache[i].label_val_man = create_manager (256);
        label_cache[i].label_cache = NULL;
        ADD_ARRAY_CB(label_cache[i].label_val_man, label_cache[i].label_cache, label_data_t, init_label_cache, NULL);
    }



    cache_context_t    *ctx = RTmalloc (sizeof(cache_context_t));
    model_t             cached = GBcreateBase ();
    ctx->cache = cache;
    ctx->cache_labels = label_cache;
    
    //GBsetDMInfo (cached, p_dm); // this shold be set by GBinitModelDefaults (?)
    GBsetContext (cached, ctx);
    
    GBsetNextStateShort (cached, cached_short);
    GBsetGroupsOfEdge (cached, cached_groups_of_edge);

    GBsetStateLabelShort (cached, cached_label_short);
    GBsetStateLabelsGroup (cached, get_label_group_cached);


    for (int i = 0; i < GBgetMatrixCount(model); i++) {
        const char *name = GBgetMatrixName(model, i);
        matrix_t *matrix = GBgetMatrix(model, i);
        pins_strictness_t strictness = GBgetMatrixStrictness(model, i);
        index_class_t row_info = GBgetMatrixRowInfo(model, i);
        index_class_t col_info = GBgetMatrixColumnInfo(model, i);
        GBsetMatrix(cached, name, matrix, strictness, row_info, col_info);
    }


    GBinitModelDefaults (&cached, model);

    int                 len = lts_type_get_state_length (GBgetLTStype (model));
    int                 s0[len];
    GBgetInitialState (model, s0);
    GBsetInitialState (cached, s0);

    return cached;
}

