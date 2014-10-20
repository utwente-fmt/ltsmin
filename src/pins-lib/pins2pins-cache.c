#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <pins-lib/pins.h>
#include <util-lib/dynamic-array.h>
#include <hre/stringindex.h>

static const int EL_OFFSET = 1;

struct state_info {
    int first;
    int trans;
    int edges;
};

static void init_state_info(void*arg,void*old_array,int old_size,void*new_array,int new_size){
    (void)arg;(void)old_array;
    struct state_info*array=(struct state_info*)new_array;
    while(old_size<new_size){
        array[old_size].first=-1;
        old_size++;
    }
}

struct group_cache {
    int                 len;
    int                 r_len;
    int                 w_len;
    string_index_t      idx;
//    int                 explored;
    int                 source;
    int                 edges;
    array_manager_t     begin_man;
    struct state_info*  begin;
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
add_cache_entry (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    struct group_cache *ctx = (struct group_cache *)context;
    int                 dst_index =
        SIputC (ctx->idx, (char *)dst, ctx->len);
    
    int offset=ctx->begin[ctx->source].first+ctx->begin[ctx->source].edges*edge_info_sz(ctx);
    ensure_access (ctx->dest_man,offset+edge_info_sz(ctx));

    int *pe_info = &ctx->dest[offset];
    *pe_info = dst_index;
    if (ti->labels != NULL)
        memcpy(pe_info + EL_OFFSET, ti->labels, ctx->Nedge_labels * sizeof *pe_info);

    ctx->edges++;
    ctx->begin[ctx->source].edges++;

    return;
    (void)cpy;
}

static int
cached_short (model_t self, int group, int *src, TransitionCB cb,
              void *user_context, int (*short_proc)(model_t,int,int*,TransitionCB,void*))
{
    struct cache_context *ctx =
        (struct cache_context *)GBgetContext (self);
    struct group_cache *cache = &(ctx->cache[group]);
    int len = dm_ones_in_row(GBgetDMInfo(self), group);

    int                 dst[len];
    int                 src_idx =
        SIputC (cache->idx, (char *)src, cache->len);

    ensure_access(cache->begin_man,src_idx);
    if (cache->begin[src_idx].first==-1) {
            cache->source=src_idx;
            cache->begin[src_idx].first = cache->edges * edge_info_sz (cache);
            cache->begin[cache->source].edges=0;
            cache->begin[src_idx].trans = short_proc (GBgetParent(self), group, src, add_cache_entry, cache);
    }
    int N=cache->begin[src_idx].edges;
    for (int i = cache->begin[src_idx].first ; N>0 ; N--,i += edge_info_sz (cache)) {
        // MW: remove if edge label becomes "const int *"?
        memcpy (dst, SIgetC (cache->idx, cache->dest[i], NULL),
                cache->len);
        int *labels = cache->Nedge_labels == 0 ? NULL : &(cache->dest[i+EL_OFFSET]);
        transition_info_t cbti = GB_TI(labels, group);
        cb (user_context, &cbti, dst, NULL);
    }
    return cache->begin[src_idx].trans;
}

static int
cached_next_short (model_t self, int group, int *src, TransitionCB cb,
                   void *user_context) {
    return cached_short(self, group, src, cb, user_context, &GBgetTransitionsShort);
}

static int
cached_actions_short (model_t self, int group, int *src, TransitionCB cb,
                   void *user_context) {
    return cached_short(self, group, src, cb, user_context, &GBgetActionsShort);
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
    matrix_t           *p_dm_read = GBgetExpandMatrix (model);
    matrix_t           *p_dm_may_write = GBgetProjectMatrix (model);
    int                 N = dm_nrows (p_dm);
    struct group_cache *cache = RTmalloc (N * sizeof (struct group_cache));
    for (int i = 0; i < N; i++) {
        int len = dm_ones_in_row (p_dm, i);
        cache[i].len = len * sizeof (int);
        int r_len = dm_ones_in_row (p_dm_read, i);
        cache[i].r_len = r_len * sizeof (int);
        int w_len = dm_ones_in_row (p_dm_may_write, i);
        cache[i].w_len = w_len * sizeof (int);
        cache[i].idx = SIcreate ();
        cache[i].edges = 0;
        cache[i].begin_man = create_manager (256);
        cache[i].begin = NULL;
        add_array(cache[i].begin_man,(void*)&(cache[i].begin),sizeof(struct state_info),init_state_info,NULL);
        cache[i].dest_man = create_manager (256);
        cache[i].Nedge_labels = lts_type_get_edge_label_count(GBgetLTStype(model));
        cache[i].dest = NULL;
        ADD_ARRAY (cache[i].dest_man, cache[i].dest, int);
    }
    struct cache_context *ctx = RTmalloc (sizeof *ctx);
    model_t             cached = GBcreateBase ();
    ctx->cache = cache;
    
    GBsetContext (cached, ctx);

    GBsetNextStateShort (cached, cached_next_short);
    GBsetActionsShort (cached, cached_actions_short);
    GBsetTransitionInGroup (cached, cached_transition_in_group);

    GBinitModelDefaults (&cached, model);

    int                 len =
        lts_type_get_state_length (GBgetLTStype (model));
    int                 s0[len];
    GBgetInitialState (model, s0);
    GBsetInitialState (cached, s0);

    GBsetDefaultFilter(cached,GBgetDefaultFilter(model));

    return cached;
}

