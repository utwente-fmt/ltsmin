#ifndef PINS_UTIL
#define PINS_UTIL

#include <stdlib.h>

#include <pins-lib/pins.h>


extern size_t       pins_get_state_label_count (model_t model);
extern size_t       pins_get_state_edge_count (model_t model);
extern size_t       pins_get_state_variable_count (model_t model);
extern size_t       pins_get_group_count (model_t model);

extern void         pins_add_edge_label_visible (model_t model, int act_label,
                                                  int act_index);

/**
\brief Adds visibility info for a state variable to the PorGroupVisibility array.
*/
extern void         pins_add_state_variable_visible (model_t model, int index);

/**
\brief Adds visibility info for a state variable to the PorStateLabelVisibility array.
*/
extern void         pins_add_state_label_visible (model_t model, int index);

#endif // PINS_UTIL
