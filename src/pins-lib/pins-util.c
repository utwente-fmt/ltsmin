#include <hre/config.h>


#include <stdlib.h>

#include <dm/dm.h>
#include <pins-lib/pins-util.h>


size_t
pins_get_state_label_count (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    return lts_type_get_state_label_count (ltstype);
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
    matrix_t           *write_info = GBgetDMInfoWrite (model);
    return dm_nrows (write_info);
}

void
mark_edge_label_visible (model_t model, int act_label, int act_index)
{
    int *visibles = GBgetPorGroupVisibility (model);
    int label_count = pins_get_state_label_count (model);
    int groups = pins_get_group_count (model);
    int labels[label_count];
    for (size_t i = 0; i < label_count; i++)
        labels[i] = act_label == i ? act_index : -1;
    for (size_t i = 0; i < groups; i++)
        visibles[i] = GBtransitionInGroup (model, labels, i);
}
