#ifndef PINS_UTIL
#define PINS_UTIL

#include <stdlib.h>

#include <pins-lib/pins.h>


extern size_t       pins_get_state_label_count (model_t model);
extern size_t       pins_get_edge_label_count (model_t model);
extern size_t       pins_get_state_variable_count (model_t model);
extern size_t       pins_get_group_count (model_t model);

/**
\brief Adds visibility info for groups that can produce and edge with a specific label.
*/
extern void         pins_add_edge_label_visible (model_t model, int edge, int label);

/**
\brief Adds visibility info for group to the PorGroupVisibility array.
*/
extern void         pins_add_group_visible (model_t model, int group);

/**
\brief Adds visibility info for a state variable to the PorGroupVisibility array.
*/
extern void         pins_add_state_variable_visible (model_t model, int index);

/**
\brief Adds visibility info for a state variable to the PorStateLabelVisibility array.
*/
extern void         pins_add_state_label_visible (model_t model, int index);

/**
 * Gets the index for the LTSMIN_EDGE_LABEL_ACCEPTING_SET (see ltsmin-standard.h)
 */
extern int pins_get_accepting_set_edge_label_index (model_t model);

/**
 * Gets the index for the LTSMIN_STATE_LABEL_ACCEPTING (see ltsmin-standard.h)
 */
extern int pins_get_accepting_state_label_index (model_t model);

/**
 * Gets the index for the LTSMIN_STATE_LABEL_PROGRESS (see ltsmin-standard.h)
 */
extern int pins_get_progress_state_label_index (model_t model);

/**
 * Gets the index for the LTSMIN_STATE_LABEL_WEAK_LTL_PROGRESS (see ltsmin-standard.h)
 */
extern int pins_get_weak_ltl_progress_state_label_index (model_t model);

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
\brief Return progress/non-progress for a given state, false if undefined
*/
extern int pins_state_is_weak_ltl_progress (model_t model, int *src);

/**
\brief Return valid end/invalid end for a given state, false if undefined
*/
extern int pins_state_is_valid_end (model_t model, int *src);

/**
 * \brief Get table iterator.
 *
 * @param model The model in question.
 * @param type_no The type the chunk maps to/from.
 */
extern table_iterator_t pins_chunk_iterator (model_t model, int type_no);

/**
 * \brief Get the number of different chunks of type type_no.
 * Please note that this function is potentially expensive in a distributed setting
 * because it requires a non-authoritative database to query the authoritative one.
 * Moreover, the result can only be guaranteed to be correct if there are no Put-calls
 * in progress anywhere in the system during the time this call is made.
 * This is of interest to: language front-ends
 * @param model The model in question.
 * @param type_no The type the chunk maps to/from.
 * @return The number of different chunks of type type_no.
 */
extern int pins_chunk_count (model_t model, int type_no);

/**
 * \brief Put a chunk into a table at a specific index.
 * WARNING: only to be used at initialization time! Otherwise this operation
 * will fail with an error.
 * This is of interest to: language front-ends
 * @param model The model in question.
 * @param type_no The type the chunk maps to/from
 * @param c The chunk to be put at the specified index
 * @param index The index the chunk will be put at
*/
extern void pins_chunk_put_at (model_t model, int type_no, const chunk c, int index);

/**
 * Put the specified chunk at any index and return the index it was put at.
 * The returned index is in the range [0,GBchunkCount(model,type_no)).
 * This is of interest to: language front-ends
 * @param model The model in question.
 * @param type_no The type the chunk maps to/from.
 * @param c The chunk to be put at any index.
 * @return The index at which the chunk was put.
*/
extern int pins_chunk_put (model_t model, int type_no, const chunk c);

/**
 * Get the chunk of the specified type at the specified index.
 * This is of interest to: language front-ends
 * @param model The model in question.
 * @param type_no The type the chunk maps to/from.
 * @param index The index at which the chunk will be returned
 * @return The requested chunk. The user can assume that the data area of the chunk
 * will keep its contents forever. The user is not allowed to change the contents.
*/
extern chunk pins_chunk_get (model_t model, int type_no, int index);

#endif // PINS_UTIL
