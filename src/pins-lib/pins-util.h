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

/**
 * Gets the index for the LTSMIN_STATE_LABEL_ACCEPTING (see ltsmin-standard.h)
 */
extern int pins_get_accepting_state_label_index (model_t model);

/**
 * Gets the index for the LTSMIN_STATE_LABEL_PROGRESS (see ltsmin-standard.h)
 */
extern int pins_get_progress_state_label_index (model_t model);

/**
 * Gets the index for the LTSMIN_STATE_LABEL_VALID_END (see ltsmin-standard.h)
 */
extern int pins_get_valid_end_state_label_index (model_t model);

/**
\brief Return accepting/not-accepting for a given state, false if undefined
*/
extern int pins_state_is_accepting (model_t model, int *src);

/**
\brief Return progress/non-progress for a given state, false if undefined
*/
extern int pins_state_is_progress (model_t model, int *src);

/**
\brief Return valid end/invalid end for a given state, false if undefined
*/
extern int pins_state_is_valid_end (model_t model, int *src);


#endif // PINS_UTIL
