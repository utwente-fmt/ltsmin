#ifndef GREYBOX_H
#define GREYBOX_H

#include <popt.h>
#include <stdio.h>

#ifdef LTSMIN_CONFIG_INCLUDED
#include <dm/dm.h>
#include <ltsmin-lib/lts-type.h>
#include <util-lib/chunk_support.h>
#include <util-lib/chunk_table_factory.h>
#include <util-lib/string-map.h>
#else
#include <ltsmin/chunk_support.h>
#include <ltsmin/chunk_table_factory.h>
#include <ltsmin/dm.h>
#include <ltsmin/lts-type.h>
#include <ltsmin/string-map.h>
#endif

/**
 @file pins.h
 @brief The Partitioned Next-State Interface (PINS) to models.
        Formerly: greybox interface (GB).

This version assumes a few things:
- Every states in the model have the same length.
- Every transition in the model has the same number of labels.
- The types of labels are the same for each transition.
*/

/**
\defgroup static_analysis Static Analysis for PINS models

@brief This group describes the functions that can be used by language
modules to provide static information to the backend algorithms.

@sa GBsetMatrix, static_analysis_getters


*/
//@{

/**
\brief Enumeration of possible connections between the index of a matrix and the elements of a PINS model.
 */
typedef enum {
/** Used for an index that corresponds to groups. */
PINS_INDEX_GROUP,
/** Used for an index that corresponds to state labels. */
PINS_INDEX_STATE_LABEL,
/** Used for an index that corresponds to state vector positions. */
PINS_INDEX_STATE_VECTOR,
/** Used for an index that corresponds to edge labels. */
PINS_INDEX_EDGE_LABEL,
/** Used for an index that corresponds to something else. */
PINS_INDEX_OTHER
} index_class_t;

/**
@brief Strictness of matrix entries.

Wrappers that can change the static structure of a model, such as the regrouping wrapper,
need to know how static information can be merged. For example, in the case of dependency
matrices adding ones to the matrix does not change the semantics of the model. Another
example is the inhibit matrix used for prioritisation. Every change to this
matrix potentially changes the semantics of the model.
*/
typedef enum {
  /** Static analysis matrix entries cannot be changed while preserving correctness. */
  PINS_STRICT,
  /** Static analysis matrix remains correct if entries are changed from 1 to 0. */
  PINS_MAY_CLEAR,
  /** Static analysis matrix remains correct if entries are changed from 0 to 1. */
  PINS_MAY_SET
} pins_strictness_t;

//@}


typedef struct grey_box_model* model_t;
/**< @brief Abstract type for a model.
*/

/**
@ingroup greybox_provider
@sa static_analysis

@brief Set a static analysis matrix.

@returns The matrix ID by which this matrix is known in the model.
*/
extern int GBsetMatrix(
    model_t model,
    const char*name,
    matrix_t *matrix,
    pins_strictness_t strictness,
    index_class_t row_info,
    index_class_t column_info);

/**
@defgroup static_analysis_getters Functions to extract static information.
@ingroup greybox_user
@sa static_analysis
*/
//@{

/**
@brief Get the numeric ID of a static analysis matrix.

@returns The ID of the matrix if it exists or a negative integer otherwise.
*/
extern int GBgetMatrixID(model_t model,char*name);

/**
@brief Get the number of matrices defined.
*/
extern int GBgetMatrixCount(model_t model);

/**
@brief Get a static analysis matrix by number.
*/
extern matrix_t* GBgetMatrix(model_t model,int ID);

/**
@brief Get the name of a static analysis matrix by number.
*/
extern const char* GBgetMatrixName(model_t model,int ID);

/**
@brief Get the strictness of a static analysis matrix by number.
*/
extern pins_strictness_t GBgetMatrixStrictness(model_t model,int ID);

/**
@brief Get the row correspondence of a static analysis matrix by number. 
*/
extern index_class_t GBgetMatrixRowInfo(model_t model,int ID);

/**
@brief Get the column correspondence of a static analysis matrix by number. 
*/
extern index_class_t GBgetMatrixColumnInfo(model_t model,int ID);
//@}

/**
\brief Struct to describe a transition. Holds edge label and group information
 */
typedef struct transition_info {
    int* labels;                    // edge labels, NULL, or pointer to the edge label(s)
    int  group;                     // holds transition group or -1 if unknown
    int  por_proviso;               // provides information on the cycle proviso (ltl) to the por layer
} transition_info_t;

#define GB_UNKNOWN_GROUP -1
#define GB_TI(A,B) {(A),(B), 0}     // transition_info_t initialization macro
static const transition_info_t GB_NO_TRANSITION = GB_TI(NULL, GB_UNKNOWN_GROUP);

/**
\brief Enum for state label groups (GBgetStateLabelGroup)
 */
typedef enum {
    GB_SL_ALL = 0,          // same as GBgetStateLabelAll
    GB_SL_GUARDS,           // get all labels used as guard
    GB_SL_GROUP_COUNT       // count elements of enum, must be last
} sl_group_enum_t;

/**
\brief A struct to store the indices of the state labels in a particular state label group
*/
typedef struct sl_group {
    int count;
    int sl_idx[];
} sl_group_t ;

/**
\brief Options for greybox management module.
 */
extern struct poptOption greybox_options[];

/**
\brief A struct to store guards per transition group
*/
typedef struct guard {
    int count;
    int guard[];
} guard_t ;

/**
\defgroup greybox_user The Greybox user interface.
*/
//@{

/**
 * \brief Factory method for loading models.
 *
 * Given a model that has been initialized with data synchronization functions,
 * this method determines the type of model by extension and loads it.
 */
void GBloadFile(model_t model, const char *filename, model_t *wrapped);

/**
\brief Factory method for loading models concurrently.

Given a model that has been initialized with data synchronization functions,
this method determines the type by the extension of the file and initializes
the read-only variables of model.

\see GBregisterPreLoader
*/
extern void GBloadFileShared(model_t model,const char *filename);

/**
\brief Get the basic LTS type or structure of the model.
*/
extern lts_type_t GBgetLTStype(model_t model);

/**
\brief Print the current dependency matrix in human readable form.
*/
extern void GBprintDependencyMatrix(FILE* file, model_t model);
extern void GBprintDependencyMatrixRead(FILE* file, model_t model);
extern void GBprintDependencyMatrixMayWrite(FILE* file, model_t model);
extern void GBprintDependencyMatrixMustWrite(FILE* file, model_t model);
extern void GBprintDependencyMatrixCombined(FILE* file, model_t model);
extern void GBprintStateLabelMatrix(FILE* file, model_t model);
extern void GBprintStateLabelGroupInfo(FILE* file, model_t model);

/**
\brief Get the dependency matrix of the model
*/
extern matrix_t *GBgetDMInfo(model_t model);
extern matrix_t *GBgetDMInfoRead(model_t model);
extern matrix_t *GBgetDMInfoMayWrite(model_t model);
extern matrix_t *GBgetDMInfoMustWrite(model_t model);

/**
 * Set the dependency matrix of the specified model to the specified
 * dependency matrix. This call specifies READ and WRITE dependencies.
 * Use only once per model.
 * Claims ownership of the memory used by dm_info.
 * The dependency matrix should be created by a call to dm_create():
 *   matrix_t* dm_info = (matrix_t*)malloc(sizeof(matrix_t));
 *   dm_create(dm_info, nr_transition_groups, state_length);
 * Where state_length is the number of state vector variables.
 * @param model The model of which the dependency matrix is to be set.
 * @param dm_info The dependency matrix to assign to the specified model.
 */
extern void GBsetDMInfo(model_t model, matrix_t *dm_info);

/**
 * Set the dependency matrix of the specified model to the specified
 * dependency matrix. These calls specify Read, MayWrite, or MustWrite
 * dependencies.
 * Use only once per model.
 * Claims ownership of the memory used by dm_info.
 * The dependency matrix should be created by a call to dm_create():
 *   matrix_t* dm_info = (matrix_t*)malloc(sizeof(matrix_t));
 *   dm_create(dm_info, nr_transition_groups, state_length);
 * Where state_length is the number of state vector variables.
 * @param model The model of which the dependency matrix is to be set.
 * @param dm_info The dependency matrix to assign to the specified model.
 */
extern void GBsetDMInfoRead(model_t model, matrix_t *dm_info);
extern void GBsetDMInfoMayWrite(model_t model, matrix_t *dm_info);
extern void GBsetDMInfoMustWrite(model_t model, matrix_t *dm_info);

/**< @brief Write the initial state of model into state. */
extern void GBgetInitialState(model_t model, int* state);

/** 
 * Type of the callback function for returning lists of transitions.
 * We produce the list of transitions by means of a callback, which
 * is provided with a user context, an array of labels and a state vector.
 * The context is usually obtained from either the next_method_black_t
 * callback or the next_method_grey_t callback.
 * @param context The context of where to write the new transition to.
 * @param transition_info The transition group responsible for the new state.
 * @param dst The new state.
 * @param cpy Array indicating which indices i of the dst have been written (cpy[i] == 0)
 */
typedef void(*TransitionCB)(void*context,transition_info_t*transition_info,int*dst,int*cpy);
/**< @brief Type of the callback function for returning lists of transitions.

We produce the list of transitions by means of a call back, which
is provided with a user context, an array of labels and a state vector.
*/

extern int GBgetTransitionsShort(model_t model,int group,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the transition of a group for a short state.

Given a group number and a short vector for that group, enumerate the local
    transitions. This function may be non-reentrant. A short state means just the values for the influenced positions.
 */


extern int GBgetTransitionsShortR2W(model_t model,int group,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the transition of a group for a read short state.

Given a group number and a short vector for that group, enumerate the local
    transitions. This function may be non-reentrant. A short state means just the values for the influenced positions.
 */

extern int GBgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the transition of a group for a long state.

Given a group number and a long vector for that group, enumerate the local
    transitions. This function may be non-reentrant. A long state means that an entire state vector is given.
   All positions in this vector must have legal values. However, the given state does not have to be reachable.
 */

extern int GBgetActionsShort(model_t model,int group,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the actions (without guards) for a short state

Given a group number and a short vector for that group, enumerate the local
    transitions. This function may be non-reentrant. A short state means just the values for the influenced positions.
 */

extern int GBgetActionsShortR2W(model_t model,int group,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the actions (without guards) for a read short state

Given a group number and a short vector for that group, enumerate the local
    transitions. This function may be non-reentrant. A short state means just the values for the influenced positions.
 */

extern int GBgetActionsLong(model_t model,int group,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the actions (without guards) for a long state

Given a group number and a long vector for that group, enumerate the local
    transitions. This function may be non-reentrant. A long state means that an entire state vector is given.
   All positions in this vector must have legal values. However, the given state does not have to be reachable.
 */

/**
 @brief Get the transition generated by the group indicated by a row in a matrix.
 
 The input state must be a long state.
 */
extern int GBgetTransitionsMarked(model_t model,matrix_t* matrix,int row,int*src,TransitionCB cb,void*context);

/**
 * @brief Return the transitions which have a value at a specific edge label position.
 */
extern int GBgetTransitionsMatching(model_t model,int label_idx,int value,int*src,TransitionCB cb,void*context);


extern int GBgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the transitions of all groups for a long state.

Of course it would be equivalent to just call GBgetTransitionsLong for all groups,
but for MCRL(2) it is more efficient if this call is implemented directly in the language module:
it avoids having to initialize the rewriter several times for the same state.
 */

typedef int (*covered_by_grey_t)(int*,int*);
/**< @brief Type of the isCoveredBy function.
*/

extern void GBsetIsCoveredBy(model_t model,covered_by_grey_t covered_by);
/**
\brief Set the covered_by method.
*/

extern void GBsetIsCoveredByShort(model_t model,covered_by_grey_t covered_by);
/**
\brief Set the covered_by_short method.
*/

extern int GBisCoveredByShort(model_t model,int*a,int*b);
/**< @brief Symbolic state a is covered by symbolic state b
 */

extern int GBisCoveredBy(model_t model,int*a,int*b);
/**< @brief Symbolic part of state a is covered by symbolic part of state b
 */

extern matrix_t *GBgetStateLabelInfo(model_t model);
/**<
\brief Get the state group information of a model.
*/

extern int GBgetStateLabelShort(model_t model,int label,int *state);
/**<
\brief Return the value of state label label given a short state
*/

extern int GBgetStateLabelLong(model_t model,int label,int *state);
/**<
\brief Return the value of state label label given a long state
*/

extern void GBgetStateLabelsGroup(model_t model, sl_group_enum_t group, int*state, int*labels);
/**<
\brief Retrieve a group of state labels
*/

extern void GBgetStateLabelsAll(model_t model,int*state,int*labels);
/**<
\brief retrieve all state labels in one call.
 */

extern sl_group_t* GBgetStateLabelGroupInfo(model_t model, sl_group_enum_t group);
/**<
\brief Get the state labels indices (and count) beloning to a particular state label group
*/

extern void GBsetStateLabelGroupInfo(model_t model, sl_group_enum_t group, sl_group_t *group_info);
/**<
\brief Get the state labels indices (and count) beloning to a particular state label group
*/

extern int GBgetStateAll(model_t model,int*state,int*labels,TransitionCB cb,void*context);
/**<
\brief Get the state labels and all transitions in one call.
*/

extern int GBgroupsOfEdge(model_t model, int edgeno, int index, int** groups);
/**<
\brief Computes group numbers that may produce an edge

Returns the number of groups that may produce an edge with a specific number (edgeno) and value (index).
Sets groups to the exact groups that may produce the edge. Groups must be freed by the caller with free().
*/

/**
\brief Get the default output filter of the model.

The PINS stack may add labels to the LTS type that are meant to exchange
statci information, such as the guard labels used in partial order reduction.
This function allows teh user to obtain a filter that describes those 
labels that were intended to be written to disk.
*/
string_set_t GBgetDefaultFilter(model_t model);

//@}

/**
\defgroup greybox_provider The Greybox provider interface.

The provider interface enables the implementor of a greybox model
to build a greybox object piece by piece.
*/
//@{

/**
\brief
Create a greybox object.

\param context_size The size of the model context.
*/
extern model_t GBcreateBase();

typedef void(*pins_loader_t)(model_t model,const char*filename);

/**
\brief Get a parent model (or NULL, if none);
*/
extern model_t GBgetParent(model_t model);

/**
\brief Register a loader for an extension.
 */
extern void GBregisterLoader(const char*extension,pins_loader_t loader);

/**
\brief Register a loader for an extension.
 */
extern void GBregisterPreLoader(const char*extension,pins_loader_t loader);

/**
 * Assign the specified context to the specified model.
 * Use this to specify a context pointer to whatever data you want available
 * during the callbacks. You can obtain this pointer using GBgetContext().
 * @param model The model of which the context will be set.
 * @param context The context to assign to the specified model.
 */
extern void GBsetContext(model_t model,void*context);

/**
 * Obtain the context of the specified model, assigned earlier using
 * GBsetContext(). This can be useful duing callbacks.
 * @param model The model of which the context will be returned.
 * @return The context of the specified model.
 */
extern void* GBgetContext(model_t model);

/**
 * Add the specified LTS structure information to the specified model.
 * See lts_type_t for more information about the LTS structure.
 * @param model The model of which the LTS structure will be set.
 * @param info The LTS structure information.
 */
extern void GBsetLTStype(model_t model, lts_type_t info);

/**
 * Add the specified state label dependency matrix to the specified model.
 * This specifies for each label on which state variables the label depends
 * on.
 * Use only once per model.
 * Claims ownership of the memory used by sl_info.
 * The label dependency matrix should be created by a call to dm_create():
 *   matrix_t* sl_info = (matrix_t*)malloc(sizeof(matrix_t));
 *   dm_create(sl_info, nr_state_labels, state_length);
 * Where state_length is the number of state vector variables, and
 * nr_state_labels is the value set earlier using:
 *   lts_type_set_state_label_count (ltstype, nr_state_labels);
 * @param model The model of which the state label info matrix will be set.
 * @param sl_info The state label information matrix.
 */
extern void GBsetStateLabelInfo(model_t model, matrix_t *sl_info);

/**
\brief Checks whether a transition group has guards
 This method is used for partial order reduction
*/
extern int GBhasGuardsInfo(model_t model);

/**
\brief Set the guard array for a model
*/
extern void GBsetGuardsInfo(model_t model, guard_t** guard);

/**
\brief Get guard array for a model
*/
extern guard_t** GBgetGuardsInfo(model_t model);

/**
\brief Set guards for a transition group
*/
extern void GBsetGuard(model_t model, int group, guard_t* guard);

/**
\brief Set the guard may be co-enabled matrix to a model
*/
extern void GBsetGuardCoEnabledInfo(model_t model, matrix_t *info);

/**
\brief Set the do not accord matrix to a model
*/
extern void GBsetDoNotAccordInfo(model_t model, matrix_t *info);

/**
\brief Get the do not accord matrix of a model.
*/
extern matrix_t *GBgetDoNotAccordInfo(model_t model);

/**
\brief Set the commutes matrix to a model
*/
extern void GBsetCommutesInfo(model_t model, matrix_t *info);

/**
\brief Get the commutes matrix of a model.
*/
extern matrix_t *GBgetCommutesInfo(model_t model);

/**
\brief Get the guard may be co-enabled matrix of a model.
*/
extern matrix_t *GBgetGuardCoEnabledInfo(model_t model);

/**
\brief Get guards for a transition group
*/
extern guard_t* GBgetGuard(model_t model, int group);

/**
\brief Set the guard NES matrix to a model
*/
extern void GBsetGuardNESInfo(model_t model, matrix_t *info);

/**
\brief Get the guard NES matrix of a model.
*/
extern matrix_t *GBgetGuardNESInfo(model_t model);

/**
\brief Set the guard NDS matrix to a model
*/
extern void GBsetGuardNDSInfo(model_t model, matrix_t *info);

/**
\brief Get the guard NDS matrix of a model.
*/
extern matrix_t *GBgetGuardNDSInfo(model_t model);

/**
\brief Set the POR group visibility info.
*/
extern void GBsetPorGroupVisibility(model_t model, int*bv);

/**
\brief Get the POR group visibility info, i.e. which group touches an LTL variable.
*/
extern int *GBgetPorGroupVisibility(model_t model);

/**
\brief Set the POR group visibility info.
*/
extern void GBsetPorStateLabelVisibility(model_t model, int*bv);

/**
\brief Get the POR group visibility info, i.e. which state labels are in the LTL formula.
        This is dynamically added to the visibility info by the POR layer.
*/
extern int *GBgetPorStateLabelVisibility(model_t model);

/**
 * \brief Set the initial state.
 *
 * The initial state is needed if a short vector next state method
 * has to be implemented in terms of a long vector one to fill in
 * the empty spaces with legal values.
 */
extern void GBsetInitialState(model_t model, int*state);

/**
 * \brief Type of the greybox next state method.
 * This is the definition of the callback method that will be called in the
 * search for next states given a current state. See GBsetNextStateLong().
 * The user_context should be passed to the callback cb when a new transition
 * has been found.
 * An example grey box method yielding 6 states:
 *   typedef struct StateVector {
 *     uint32_t a;
 *     uint32_t b;
 *   } StateVector;
 *   int gb_next(model_t self, int group, struct StateVector* src, TransitionCB cb, void* user_context) {
 *     switch(group) {
 *     case 0:
 *       StateVector tmp = *src;
 *       transition_info_t transition_info = { NULL, group };
 *       tmp.a = (src->a + 1) % 6;
 *       cb(user_context, &transition_info, (int*)&tmp);
 *       return 1;
 *       break;
 *     }
 *     return 0;
 *   }
 * @param self The model on which this callback method was called.
 * @param group The transition group to check if it is enabled or not
 * @param src The current state.
 * @param cb The callback method to report new transitions. See TransitionCB.
 * @param user_context The context of where to write the new transition to.
 * @return The number of new transitions found (how many times cb was called).
 */
typedef int(*next_method_grey_t)(model_t self,int group,int*src,TransitionCB cb,void*user_context);

/**
 * Set the grey box next state method of the specified model to the specified
 * function. See next_method_grey_t for a description of the function.
 * For every transition group the specified callback will be called in the
 * search of new transitions. The group parameter will be in the range [0,N),
 * where N is the number of transitions groups specified when the dependency
 * matrix was set using GBsetDMInfo, GBsetDMInfoRead or GBsetDMInfoWrite.
 * This is the LONG version, which means the entire current state vector will
 * be passed as src. This in contrast to the SHORT version, which can be
 * specified using GBsetNextStateShort().
 * If this method is not set then the short version is used.
 * @param model The model on which to set the grey box next state method.
 * @param method The function that will be assigned to the specified model.
 */
extern void GBsetNextStateLong(model_t model,next_method_grey_t method);

/**
 * Set the grey box next state method of the specified model to the specified
 * function. See next_method_grey_t for a description of the function.
 * For every transition group the specified callback will be called in the
 * search of new transitions. The group parameter will be in the range [0,N),
 * where N is the number of transitions groups specified when the dependency
 * matrix was set using GBsetDMInfo, GBsetDMInfoRead or GBsetDMInfoWrite.
 * This is the SHORT version, which means only the part of the current state
 * vector that is either read or written to will be passed as src, based on
 * the dependency matrix. This in contrast to the SHORT version, which can be
 * specified using GBsetNextStateLong().
 * If this method is not set then the long version is used.
 * An example of a short grey box next state method:
 * int gb_next_short(model_t self, int group, int const* src, TransitionCB cb, void* user_context) {
 *   int tmp = *src;
 *   transition_info_t transition_info = { NULL, group };
 *   tmp = (*src + 1) % 6;
 *   cb(user_context, &transition_info, (int*)&tmp);
 *   return 1;
 * }
 * This example uses a 2x2 dependency matrix:
 *   1 0
 *   0 1
 * This means for both the first state vector variable and the second state
 * vector variable that all integral values in the range [0,6) are iterated,
 * due to the dependency matrix and the fact that the SHORT version is used.
 * All combinations are iterated as well, thus yielding 36 states and 72
 * transitions.
 * @param model The model on which to set the grey box next state method.
 * @param method The function that will be assigned to the specified model.
 */
extern void GBsetNextStateShort(model_t model,next_method_grey_t method);

/**
\brief Set the next state method that works on short vectors.
A short vector is either projected using read dependencies (source state)
or write dependencies (target state).

If this method is not set then the long version is used.
*/
extern void GBsetNextStateShortR2W(model_t model,next_method_grey_t method);

/**
\brief Set the next state method that works on long vectors.

If this method is not set then the short version is used.
*/
extern void GBsetActionsLong(model_t model,next_method_grey_t method);

/**
\brief Set the next state method that works on short vectors.

If this method is not set then the long version is used.
*/
extern void GBsetActionsShort(model_t model,next_method_grey_t method);

/**
\brief Set the next state method that works on short vectors.
A short vector is either projected using read dependencies (source state)
or write dependencies (target state).

If this method is not set then the long version is used.
*/
extern void GBsetActionsShortR2W(model_t model,next_method_grey_t method);

/**
\brief Type of the greybox next state matching method.
*/
typedef int(*next_method_matching_t)(model_t self,int label_idx,int value,int*src,TransitionCB cb,void*user_context);

/**
\brief Set the get transitions matching method.
 */
extern void GBsetNextStateMatching(model_t model,next_method_matching_t method);
 
/**
\brief Type of the blackbox next state method.
*/
typedef int(*next_method_black_t)(model_t self,int*src,TransitionCB cb,void*user_context);

/**
 * Set the black box next state method of the specified model to the specified
 * function. See next_method_black_t for a description of the function.
 * This function will be called in the search of next states given a current
 * state. See next_method_black_t for an example.
 * If this method is not set explicitly then the grey box calls are iterated.
 * @param model The model on which to set the black box next state method.
 * @param method The function that will be assigned to the specified model.
 */
extern void GBsetNextStateAll(model_t model, next_method_black_t method);

/**
 * \brief Type of the get all labels method.
 * This is the definition of the callback method that will be called to
 * determine the values of the labels in the specified state (src).
 * To set the value of a label with index i, write to label:
 *   label[i] = myValue;
 * This index corresponds with the index of the label dependency matrix,
 * specified by GBsetStateLabelInfo().
 * @param self The model on which this callback method was called.
 * @param src The state on which the values of the labels should be based.
 * @param label The array to which the values of the labels will be assigned.
 */
typedef void (*get_label_all_method_t)(model_t self, int *src, int *label);

/**
 * 
 * @param model The model on which to set the get all labels method.
 * @param method The function that will be assigned to the specified model.
 */
extern void GBsetStateLabelsAll(model_t model,get_label_all_method_t method);

/// Type of label retrieval methods.
typedef void (*get_label_group_method_t)(model_t self, sl_group_enum_t group, int *src, int *label);

/**
\brief Set the method that retrieves a group of state labels.
*/
extern void GBsetStateLabelsGroup(model_t model,get_label_group_method_t method);

/// Type of label retrieval methods.
typedef int (*get_label_method_t)(model_t self, int label, int *src);

/**
\brief Set the method that retrieves labels given long vectors.
*/
extern void GBsetStateLabelLong(model_t model,get_label_method_t method);

/**
\brief Set the method that retrieves labels given short vectors.
*/
extern void GBsetStateLabelShort(model_t model,get_label_method_t method);

/// Type of groups of edge method
typedef int (*groups_of_edge_t)(model_t model,int edgeno,int index,int** groups);

/**
\brief Set the method for groups of edge method.
*/
extern void GBsetGroupsOfEdge(model_t model,groups_of_edge_t method);


/**
\brief Set the default output filter of the model.

The creator of a PINS model should use this method to set a filter
whose intended use is to select the labels that are typically written
to disk.
*/
void GBsetDefaultFilter(model_t model,string_set_t filter);
//@}

/**
\defgroup greybox_serialisation The Greybox (de)serialisation interface.

The implementation will replicate a global database for each type.
This can be bad if your model contains one variable with a very
big domain. If this is the case then you might have to write functions
that can do a sensible split and merge. E.g. if you have a set of
integers split the set into the subsets of even integers and odd integers.
*/

//@{

/** \brief Set the chunk map.
 *
 *  The map factory should produce proper ValueTable objects
 *  (see util-lib/chunk_table_factory.h)
 */

extern void GBsetChunkMap(model_t model, table_factory_t factory);

/**
\brief Copy map factory methods, lookup methods AND chunk maps.
*/
extern void GBcopyChunkMaps(model_t dst, model_t src);

/**
\brief Adds extra chunk maps besides the existing ones

Used in the pins ltl layer to when the lts type is appended with the buchi automaton type
*/
extern void GBgrowChunkMaps(model_t model, int old_n);

/**
\brief Initializes unset model parameters from default_src.
*/
extern void GBinitModelDefaults (model_t *p_model, model_t default_src);

/**
\brief Fetch a chunk, pretty print it and put the pretty printed
       chunk into a table.

\param type_no The number of the type.
\param chunk_no The numer of the chunk.
\returns The number of the pretty printed version.
*/
extern int GBchunkPrettyPrint(model_t model,int type_no,int chunk_no);

typedef int (*chunk2pretty_t) (model_t model, int pos, int idx);

/**
*/
extern void GBsetPrettyPrint(model_t model,chunk2pretty_t chunk2pretty);


/** Retrieve the map used for a specific type. */
extern value_table_t GBgetChunkMap(model_t model,int type_no);

//@}

/**
\defgroup greybox_operators The Greybox operator suite.
*/

typedef void(*ExitCB)(model_t model);

/**
 * \brief Set the exit function.
 */
extern void GBsetExit(model_t model, ExitCB exit);

/**
 * \brief Run the exit function.
 */
extern void GBExit(model_t model);

/**
 * \brief set state vector permutation.
 */
extern void GBsetVarPerm(model_t model, int* perm);

/**
 * \brief get state vector permutation.
 */
extern int* GBgetVarPerm(model_t model);

/**
 * \brief set group permutation.
 */
extern void GBsetGroupPerm(model_t model, int* perm);

/**
 * \brief get group permutation.
 */
extern int* GBgetGroupPerm(model_t model);

#endif
