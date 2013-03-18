#ifndef PINS_UTIL
#define PINS_UTIL

#include <stdlib.h>

#include <pins-lib/pins.h>


extern size_t       pins_get_state_label_count (model_t model);
extern size_t       pins_get_state_variable_count (model_t model);
extern size_t       pins_get_group_count (model_t model);

extern void         mark_edge_label_visible (model_t model, int act_label,
                                             int act_index);

#endif // PINS_UTIL
