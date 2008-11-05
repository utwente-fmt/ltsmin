#ifndef GREYBOX_H
#define GREYBOX_H


/**
 @file greybox.h
 @brief The grey box interface to models.

This version assumes a few things:
- Every states in the model have the same length.
- Every transition in the model has the same number of labels.
- The types of labels are the same for each transition.
*/

typedef struct grey_box_model* model_t;
/**< @brief Abstract type for a model.
*/

extern model_t GBcreateModel(char*model);
/**< @brief Factory method for creating a model.
 */

extern int GBgetStateLength(model_t model);
/**< @brief Get the length of the state vector of model.
 */

extern int GBgetLabelCount(model_t model);
/**< @brief Get the number of labels on each transition in model.
 */

extern char* GBgetLabelDescription(model_t model,int label);
/**< @brief Get a description of the label in model.
 */

extern int GBgetGroupCount(model_t model);
/**< @brief Get the number of groups of the model. */

extern void GBgetGroupInfo(model_t model,int group,int*length,int**indices);
/**< @brief Get the influenced vector positions of a group.
Given a model and a group, the other variables will be filled with information.
@param length Pointer to a place in memory where the number of influenced positions will be written.
@param indices Pointer to a place in memory we a pointer to an array of size length
            can be found that contains the influenced vector positions of the group.
	This array will be in strictly increasing order.
  */

extern void GBgetInitialState(model_t model,int *state);
/**< @brief Write the initial state of model into state. */

typedef void(*TransitionCB)(void*context,int*labels,int*dst);
/**< @brief Type of the callback function for returning lists of transitions.

We produce the list of transitions by means of a call back, which
is provided with a user context, an array of labels and a state vector.
*/

extern int GBgetTransitionsShort(model_t model,int group,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the transition of a group for a short state.

Given a group number and a short vector for that group, enumerate the local
    transitions. This function may be non-reentrant. A short state means just the values for the influenced positions.
 */

extern int GBgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the transition of a group for a long state.

Given a group number and a long vector for that group, enumerate the local
    transitions. This function may be non-reentrant. A long state means that an entire state vector is given.
   All positions in this vector must have legal values. However, the given state does not have to be reachable.
 */

extern int GBgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the transitions of all groups for a long state.

Of course it would be equivalent to just call GBgetTransitionsLong for all groups,
but for MCRL(2) it is more efficient if this call is implemented directly in the language module:
it avoids having to initialize the rewriter several times for the same state.
 */


#endif

