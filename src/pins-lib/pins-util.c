#include <hre/config.h>


#include <limits.h>
#include <stdlib.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <hre/stringindex.h>
#include <ltsmin-lib/ltsmin-parse-env.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <pins-lib/pins-util.h>


size_t
pins_get_state_label_count (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_get_state_label_count (ltstype);
}

size_t
pins_get_edge_label_count (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_get_edge_label_count (ltstype);
}

size_t
pins_get_state_variable_count (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_get_state_length (ltstype);
}

size_t
pins_get_group_count (model_t model)
{
    matrix_t           *write_info = GBgetDMInfo (model);
    return dm_nrows (write_info);
}

void
pins_add_group_visible (model_t model, int group)
{
    int *visibles = GBgetPorGroupVisibility (model);
    HREassert (visibles != NULL, "pins_add_edge_label_visible: No (lower) PINS layer uses POR visibility.");
    
    visibles[group] = 1;
}

void
pins_add_edge_label_visible (model_t model, int edge, int label)
{
    int *visibles = GBgetPorGroupVisibility (model);
    HREassert (visibles != NULL, "pins_add_edge_label_visible: No (lower) PINS layer uses POR visibility.");

    int* groups_of_edge = NULL;
    const int n = GBgroupsOfEdge(model, edge, label, &groups_of_edge);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            visibles[groups_of_edge[i]] = 1;
        }
        RTfree(groups_of_edge);
    } else {
        chunk c = pins_chunk_get(model, lts_type_get_edge_label_typeno(GBgetLTStype(model), edge), label);
        char s[c.len * 2 + 6];
        chunk2string(c, sizeof(s), s);
        Abort("There is no group that can produce edge \"%s\"", s);

    }
}

void
pins_add_state_variable_visible (model_t model, int index)
{
    int                *visible = GBgetPorGroupVisibility(model);
    HREassert (visible != NULL, "pins_add_state_variable_visible: No (lower) PINS layer uses POR visibility.");
    matrix_t           *wr_info = GBgetDMInfoMayWrite (model);
    int                 ngroups = dm_nrows (wr_info);
    for (int i = 0; i < ngroups; i++) {
        if (dm_is_set(wr_info, i, index)) {
            visible[i] = 1;
        }
    }
}

void
pins_add_state_label_visible (model_t model, int index)
{
    int                *visible = GBgetPorStateLabelVisibility(model);
    HREassert (visible != NULL, "pins_add_state_label_visible: No (lower) PINS layer uses POR visibility.");
    visible[index] = 1;
}

int
pins_get_accepting_set_edge_label_index (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_find_edge_label (ltstype, LTSMIN_EDGE_LABEL_ACCEPTING_SET);
}

int
pins_get_accepting_state_label_index (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_find_state_label (ltstype, LTSMIN_STATE_LABEL_ACCEPTING);
}

int
pins_get_weak_ltl_progress_state_label_index (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_find_state_label (ltstype, LTSMIN_STATE_LABEL_WEAK_LTL_PROGRESS);
}

int
pins_get_progress_state_label_index (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_find_state_label (ltstype, LTSMIN_STATE_LABEL_PROGRESS);
}

int
pins_get_valid_end_state_label_index (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_find_state_label (ltstype, LTSMIN_STATE_LABEL_VALID_END);
}

static inline int
evalBoolGuard (int label, model_t model, int *src)
{
    if (label == -1) return 0;
    int val = GBgetStateLabelLong (model, label, src);
    HREassert (val == 0 || val == 1, "Boolean state label expected, found: %d", val);
    return val;
}

int
pins_state_is_accepting (model_t model, int *src)
{
    int label = pins_get_accepting_state_label_index (model);
    return evalBoolGuard (label, model, src);
}

int
pins_state_is_progress (model_t model, int *src)
{
    int label = pins_get_progress_state_label_index (model);
    return evalBoolGuard (label, model, src);
}

int
pins_state_is_weak_ltl_progress (model_t model, int *src)
{
    int label = pins_get_weak_ltl_progress_state_label_index (model);
    return evalBoolGuard (label, model, src);
}

int
pins_state_is_valid_end (model_t model, int *src)
{
    int label = pins_get_valid_end_state_label_index (model);
    return evalBoolGuard (label, model, src);
}

void
pins_chunk_put_at (model_t model,int type_no,const chunk c,int pos)
{
    value_table_t map = GBgetChunkMap (model, type_no);
    VTputAtChunk (map, c, pos);
}

int
pins_chunk_put (model_t model,int type_no,const chunk c)
{
    value_table_t map = GBgetChunkMap (model, type_no);
    return VTputChunk (map, c);
}

chunk
pins_chunk_get (model_t model,int type_no,int chunk_no)
{
    value_table_t map = GBgetChunkMap (model, type_no);
    return VTgetChunk (map, chunk_no);
}

int
pins_chunk_count (model_t model,int type_no)
{
    value_table_t map = GBgetChunkMap (model, type_no);
    return VTgetCount (map);
}

table_iterator_t
pins_chunk_iterator (model_t model,int type_no)
{
    value_table_t map = GBgetChunkMap (model, type_no);
    return VTiterator (map);
}
