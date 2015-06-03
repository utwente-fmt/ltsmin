#ifndef GREYBOX_H
#define GREYBOX_H

#include <popt.h>
#include <stdio.h>

#ifdef LTSMIN_CONFIG_INCLUDED
#include <util-lib/string-map.h>
#include <dm/dm.h>
#include <ltsmin-lib/lts-type.h>
#include <util-lib/chunk_support.h>
#else
#include <ltsmin/dm.h>
#include <ltsmin/string-map.h>
#include <ltsmin/lts-type.h>
#include <ltsmin/chunk_support.h>
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
\brief Options for greybox management module including
LTL options.
 */
extern struct poptOption greybox_options_ltl[];

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
\brief The POR mode:

no POR, POR, or POR with correctness check (invisible)
*/

typedef enum {
    PINS_POR_NONE,
    PINS_POR_ON,
    PINS_POR_CHECK,
} pins_por_t;

/**
 * \brief boolean indicating whether PINS uses POR
 */
extern pins_por_t PINS_POR;

/**
\brief The behaviour of the ltl buchi product

PINS_LTL_TEXTBOOK adds an initial state to the model and labels
the incoming edges with the properties of in the buchi automaton
PINS_LTL_SPIN labels the outgoing edges with the properties of
the buchi automaton. Additionally, the SPIN semantics accounts
for deadlocks in the LTS by letting the buchi continues upon deadlock.
PINS_LTL_LTSMIN Like SPIN semantics, but without the deadlock provision.
This allows LTSmin to maintain an efficient dependency matrix as
deadlock detection is non-local (it depends on the conjunction of all
guards from all transition groups).
*/
typedef enum {
    PINS_LTL_NONE,
    PINS_LTL_TEXTBOOK,
    PINS_LTL_SPIN,
    PINS_LTL_LTSMIN
} pins_ltl_type_t;

/**
 * \brief boolean indicating whether PINS uses LTL
 */
extern pins_ltl_type_t PINS_LTL;

/**
 * \brief Factory method for loading models.
 *
 * Given a model that has been initialized with data synchronization functions,
 * this method determines the type of model by extension and loads it.
 *
 * NOTE: Default wrappers are now applied by GBwrapModel.
 */
void GBloadFile(model_t model, const char *filename);

/**
\brief Factory method for loading models concurrently.

Given a model that has been initialized with data synchronization functions,
this method determines the type by the extension of the file and initializes
the read-only variables of model.

\see GBregisterPreLoader
*/
extern void GBloadFileShared(model_t model,const char *filename);

/**
 * \brief Method to wrap models according to the command line specification of users
 *
 * Given a model that has been initialized by GBloadFile, this method applies
 * the wrappers (default and command-line specified) to the model
 */
model_t GBwrapModel(model_t model);

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
extern matrix_t *GBgetExpandMatrix(model_t model);
extern matrix_t *GBgetProjectMatrix(model_t model);
extern int      GBsupportsCopy(model_t model);
extern void     GBsetSupportsCopy(model_t model);

/**
\brief Set the dependency matrix of the model
*/
extern void GBsetDMInfo(model_t model, matrix_t *dm_info);
extern void GBsetDMInfoRead(model_t model, matrix_t *dm_info);
extern void GBsetDMInfoMayWrite(model_t model, matrix_t *dm_info);
extern void GBsetDMInfoMustWrite(model_t model, matrix_t *dm_info);
extern void GBsetExpandMatrix(model_t model, matrix_t *dm_info);
extern void GBsetProjectMatrix(model_t model, matrix_t *dm_info);

extern void GBgetInitialState(model_t model,int *state);
/**< @brief Write the initial state of model into state. */

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

extern int GBgetAcceptingStateLabelIndex(model_t model);
/**<
\brief Get index of accepting state label
*/

extern int GBsetAcceptingStateLabelIndex(model_t model, int index);
/**<
\brief Set index of accepting state label
*/

extern int GBbuchiIsAccepting(model_t model, int* src);
/**<
\brief Return accepting/not-accepting for a given state, false if undefined
*/

extern int GBgetProgressStateLabelIndex(model_t model);
/**<
\brief Get index of progress state label
*/

extern int GBsetProgressStateLabelIndex(model_t model, int index);
/**<
\brief Set index of progress state label
*/

extern int GBstateIsProgress(model_t model, int* src);
/**<
\brief Return progress/non-progress for a given state, false if undefined
*/

extern int GBgetValidEndStateLabelIndex(model_t model);
/**<
\brief Get index of valid end state label
*/

extern int GBsetValidEndStateLabelIndex(model_t model, int index);
/**<
\brief Set index of valid end state label
*/

extern int GBstateIsValidEnd(model_t model, int* src);
/**<
\brief Return valid end/invalid end for a given state, false if undefined
*/

extern int GBtransitionInGroup(model_t model, int* labels, int group);
/**<
\brief Return if a transition labelled with labels potentially occurs in group

The number of labels in the labels parameter should be equal to the number of
labels with with each transition is labelled.
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
\brief Set a pointer to the user context;
*/
extern void GBsetContext(model_t model,void*context);

/**
\brief Get a pointer to the user context;
*/
extern void* GBgetContext(model_t model);

/**
\brief Add LTS structure information to a model.
*/
extern void GBsetLTStype(model_t model,lts_type_t info);

/**
\brief Add state label information to a model.
*/
extern void GBsetStateLabelInfo(model_t model, matrix_t *info);

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
\brief Set the initial state.

The initial state is needed if a short vector next state method
has to be implemented in terms of a long vector one to fill in
the empty spaces with legal values.
*/
extern void GBsetInitialState(model_t model,int *state);

/**
\brief Type of the greybox next state method.
*/
typedef int(*next_method_grey_t)(model_t self,int group,int*src,TransitionCB cb,void*user_context);

/**
\brief Set the next state method that works on long vectors.

If this method is not set then the short version is used.
*/
extern void GBsetNextStateLong(model_t model,next_method_grey_t method);

/**
\brief Set the next state method that works on short vectors.

If this method is not set then the long version is used.
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
\brief Set the black box next state method.

If this method is not set explicitly then the grey box calls are iterated.
*/
extern void GBsetNextStateAll(model_t model,next_method_black_t method);

/// Type of label retrieval methods.
typedef void (*get_label_all_method_t)(model_t self,int*src,int *label);

/**
\brief Set the method that retrieves all state labels.
*/
extern void GBsetStateLabelsAll(model_t model,get_label_all_method_t method);

/// Type of label retrieval methods.
typedef void (*get_label_group_method_t)(model_t self,sl_group_enum_t group, int*src,int *label);

/**
\brief Set the method that retrieves a group of state labels.
*/
extern void GBsetStateLabelsGroup(model_t model,get_label_group_method_t method);

/// Type of label retrieval methods.
typedef int (*get_label_method_t)(model_t self,int label,int*src);

/**
\brief Set the method that retrieves labels given long vectors.
*/
extern void GBsetStateLabelLong(model_t model,get_label_method_t method);

/**
\brief Set the method that retrieves labels given short vectors.
*/
extern void GBsetStateLabelShort(model_t model,get_label_method_t method);

/// Type of transition group retrieval method
typedef int (*transition_in_group_t)(model_t self,int *labels,int group);

/**
\brief Set the method for transition group retrieval
*/
extern void GBsetTransitionInGroup(model_t model,transition_in_group_t method);


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

/** The newmap method creates a new map, given the newmap context.
 */
typedef void*(*newmap_t)(void*newmap_context);
/** Translate the given chunk to an integer with repect to the given map.
 */
typedef int (*chunk2int_t)(void*map,void*chunk,int len);
/** Put the given chunk at a location in the map.
 */
typedef void (*chunkatint_t)(void*map,void*chunk,int len,int pos);
/** Translate the given integer to a chunk with repect to the given map.
 */
typedef void* (*int2chunk_t)(void*map,int idx,int*len);

typedef int (*get_count_t)(void* object);

/** Set the map factory method and the lookup methods for chunk maps.
 */
extern void GBsetChunkMethods(model_t model,
                              newmap_t newmap,
                              void *newmap_context,
                              int2chunk_t int2chunk,
                              chunk2int_t chunk2int,
                              chunkatint_t chunkat,
                              get_count_t get_count);

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
\brief Get the number of different chunks of type type_no.

Please note that this function is potentially expensive in a distributed setting
because it requires a non-authoritative database to query the authoritative one.
Moreover, the result can only be guaranteed to be correct if there are no Put-calls
in progress anywhere in the system during the time this call is made.
*/
extern int GBchunkCount(model_t model,int type_no);

/**
\brief Put a chunk into a table at a specific index.

WARNING: only to be used at initialization time! Otherwise this operation will
fail with an error.
*/
extern void GBchunkPutAt(model_t model,int type_no,const chunk c,int index);

/**
\brief Put a chunk into a table.

This call translates a chunk to an integer.
These integers must be from a range 0..count-1.
*/
extern int GBchunkPut(model_t model,int type_no, const chunk c);

/**
\brief Get the a chunk in a type.

\param type_no The number of the type.
\param chunk_no The numer of the chunk.
\returns The requested chunk. The user can assume that the data area of the chunk
will keep its contents forever. The user is not allowed to change the contents.
*/
extern chunk GBchunkGet(model_t model,int type_no,int chunk_no);

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
extern void* GBgetChunkMap(model_t model,int type_no);

//@}

/**
\defgroup greybox_operators The Greybox operator suite.
*/

//@{

/**
\brief Add caching of grey box short calls.
*/
extern model_t GBaddCache(model_t model);

/**
\brief Add LTL layer on top all other pins layers
*/
extern model_t GBaddLTL(model_t model);

extern struct poptOption ltl_options[];

/**
\brief Add POR layer before LTL layer
*/
extern model_t GBaddPOR(model_t model);

extern struct poptOption por_options[];

/**
\brief Add layer that checks vorrectness of POR reductions before LTL layer
*/
extern model_t GBaddPORCheck(model_t model);

/**
\brief Add mu-calculus layer
*/
extern model_t GBaddMucalc (model_t model, const char *mucalc_file);

/**
\brief Add multi-process fork wrapper
*/
extern model_t GBaddFork(model_t model);

/**
\brief Add mutex wrapper (for non thread-safe PINS models)
*/
extern model_t GBaddMutex(model_t model);


//@{

/**
\brief Reorder and regroup transitions and state vectors
*/
extern model_t GBregroup(model_t model, const char *group_spec);

/**
 * \brief Returns 1 if the mucalc wrapper is active; 0 otherwise.
 */
extern int GBhaveMucalc();

/**
 * \brief Gets the number of subformulae of the mu-calculus property that is being checked.
 * Needed for the parity game solver.
 */
extern int GBgetMucalcNodeCount(model_t model);

/**
 * \brief Sets the number of subformulae of the mu-calculus property that is being checked.
 * Needed for the parity game solver.
 */
extern void GBsetMucalcNodeCount(model_t model, int node_count);

//@}

/**
 * \brief Whether to use guards to speed up next-state computation.
 */
extern int GBgetUseGuards(model_t model);

#endif
