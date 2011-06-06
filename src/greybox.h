#ifndef GREYBOX_H
#define GREYBOX_H

#include <popt.h>
#include <stdio.h>
#include "chunk_support.h"
#include "lts-type.h"
#include "dm/dm.h"

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

/**
\brief Struct to describe a transition. Holds edge label and group information
 */
typedef struct transition_info {
    int* labels;                    // edge labels, NULL, or pointer to the edge label(s)
    int  group;                     // holds transition group or -1 if unknown
    int  por_proviso;               // provides infomation on the cycle proviso (ltl) to the por layer
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
\brief Enum to describe the type of property already in the model provided by the frondend
 */
typedef enum { PROPERTY_NONE, PROPERTY_LTL_SPIN, PROPERTY_LTL_TEXTBOOK, PROPERTY_CTL, PROPERTY_CTL_STAR, PROPERTY_MU } property_enum_t;
typedef property_enum_t (*fn_has_property_t)();
typedef int (*fn_buchi_is_accepting_t)(model_t model, int*src);

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
\brief Factory method for loading models.

Given a model that has been initialized with data synchronization functions,
this method determines the type of model by extension and loads it. If
the parameter wrapped is not NULL then the default wrappers are applied to
the model and the result is put in wrapped.
*/
extern void GBloadFile(model_t model,const char *filename,model_t *wrapped);
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
extern void GBprintDependencyMatrixWrite(FILE* file, model_t model);
extern void GBprintDependencyMatrixCombined(FILE* file, model_t model);

/**
\brief Get the dependency matrix of the model
*/
extern matrix_t *GBgetDMInfo(model_t model);
extern matrix_t *GBgetDMInfoRead(model_t model);
extern matrix_t *GBgetDMInfoWrite(model_t model);

/**
\brief Set the dependency matrix of the model
*/
extern void GBsetDMInfo(model_t model, matrix_t *dm_info);
extern void GBsetDMInfoRead(model_t model, matrix_t *dm_info);
extern void GBsetDMInfoWrite(model_t model, matrix_t *dm_info);

extern void GBgetInitialState(model_t model,int *state);
/**< @brief Write the initial state of model into state. */

typedef void(*TransitionCB)(void*context,transition_info_t*transition_info,int*dst);
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
\brief Return accepting/not-accepting for a given state
*/

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
/** Translate the given integer to a chunk with repect to the given map.
 */
typedef void* (*int2chunk_t)(void*map,int idx,int*len);

typedef int (*get_count_t)(void* object);

/** Set the map factory method and the lookup methods for chunk maps.
 */
extern void GBsetChunkMethods(model_t model,newmap_t newmap,void*newmap_context,
	int2chunk_t int2chunk,chunk2int_t chunk2int,get_count_t get_count);

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
\brief The behaviour of the ltl buchi product

PINS_LTL_TEXTBOOK adds an initial state to the model and labels
the incoming edges with the properties of in the buchi automaton
PINS_LTL_SPIN labels the outgoing edges with the properties of
the buchi automaton
*/
typedef enum {PINS_LTL_TEXTBOOK, PINS_LTL_SPIN} pins_ltl_type_t;

/**
\brief Add LTL layer on top all other pins layers
*/
extern model_t GBaddLTL(model_t model, const char *ltl_file, pins_ltl_type_t type, model_t por_model);

/**
\brief Add POR layer before LTL layer
*/
extern model_t GBaddPOR(model_t model, const int has_ltl);

/**
\brief connection from ltl to por layer

Note: this is a hack because the layers are closely coupled
      a better solution must be provided sometime in the future
*/
extern void por_visibility(model_t model, int group, int visibility);

//@{

/**
\brief Reorder and regroup transitions and state vectors
*/
extern model_t GBregroup(model_t model, const char *group_spec);


//@}

#endif
