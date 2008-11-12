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

/**
\brief Structure defining the important characteristics of an LTS.

This is the common part for both black box and grey box.
 */
typedef struct lts_structure_s {
	int state_length; ///< The length of the state vector.
	int visible_count; ///< The number of visible elements in the state vector.
	int* visible_indices; ///< Sorted list of indices of visible elements.
	char** visible_name; ///< Variable names of the visible elements.
	int* visible_type; ///< Type numbers of the visible elements.
	int state_labels; ///< The number of state labels.
	char** state_label_name; ///< The names of the state labels.
	int* state_label_type; ///< The type numbers of the state labels.
	int edge_labels; ///< The number of state labels.
	char** edge_label_name; ///< The names of the edge labels.
	int* edge_label_type; ///< The type numbers of the edge labels.
	int type_count; ///< The number of different types.
	char** type_names; ///< The names of the types.
} *lts_struct_t;

/**
\defgroup greybox_user The Greybox user interface.
*/
//@{

/**
\brief Get the basic LTS type or structure of the model.
*/
extern lts_struct_t GBgetLTStype(model_t model);

extern int GBgetStateLength(model_t model);
/**< @brief Get the length of the state vector of model.
\deprecated Use GBgetLTSinfo instead.
 */


/**
\brief Group information. 

For each grey box model, anumber of groups must be determined and
for each of those groups the influenced variables must de given.
*/
struct group_info {
	int   groups;  ///< The number of groups.
	int*  length;  ///< The number of influenced variables per group.
	int** indices; ///< A sorted list of indices of influenced variables per group.
};
typedef struct group_info *group_info_t;


extern group_info_t GBgetEdgeGroupInfo(model_t model);
/**<
\brief Get the edge group information of a model.
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

extern group_info_t GBgetStateGroupInfo(model_t model);
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

extern int GBgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context);
/**< @brief Enumerate the transitions of all groups for a long state.

Of course it would be equivalent to just call GBgetTransitionsLong for all groups,
but for MCRL(2) it is more efficient if this call is implemented directly in the language module:
it avoids having to initialize the rewriter several times for the same state.
 */

extern void GBgetStateLabelsAll(model_t model,int*state,int*labels);
/**<
\brief retrieve all state labels in one call.
 */

extern int GBgetStateAll(model_t model,int*state,int*labels,TransitionCB cb,void*context);
/**<
\brief Get the state labels and all transitions in one call.
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
extern model_t GBcreateBase(int context_size);

/**
\brief Add LTS structure information to a model.
*/
extern void GBsetLTStype(model_t model,lts_struct_t info);

/**
\brief Add edge group information to a model.
*/
extern void GBsetEdgeInfo(model_t model,group_info_t info);

/**
\brief Add state label information to a model.
*/
extern void GBsetStateInfo(model_t model,group_info_t info);

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
typedef int(*next_method_grey_t)(void*model_context,int group,int*src,TransitionCB cb,void*user_context);

/**
\brief Set the next state method that works on long vectors.

If this method is not set then the short version is used.
*/
extern void GTsetNextStateLong(model_t model);

/**
\brief Set the next state method that works on short vectors.

If this method is not set then the long version is used.
*/
extern void GTsetNextStateShort(model_t model);

/**
\brief Type of the blackbox next state method.
*/
typedef int(*next_method_black_t)(void*model_context,int*src,TransitionCB cb,void*user_context);

/**
\brief Set the black box next state method.

If this method is not set explicitly then the grey box calls are iterated.
*/
extern void GTsetNextStateAll(model_t model);

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
extern int GBchunkPut(model_t model,int type_no,int len,void*chunk);

/**
\brief Get the size of a chunk in a type.

\param type_no The number of the type.
\param chunk_no The numer of the chunk.
\returns The length of chunk chunk_no in type  type_no.
*/
extern int GBchunkLength(model_t model,int type_no,int chunk_no);
/**
\brief Get the a chunk in a type.

\param type_no The number of the type.
\param chunk_no The numer of the chunk.
\param chunk A pointer to a piece of memory at least as long the lenght of the chunk.
\returns The given chunk with the chunk data written to it.
*/
extern void* GBchunkGet(model_t model,int type_no,int chunk_no,void* chunk);

/**
\brief Set the escape character for translating chunks to strings.

A chunk is translated to a string as follows:

Any printable, non-escape character is copied.
The escape caracter is encoded as two escape characters.
Any non-printable character is encoded as the escape character followed by the 
character in hex. (E.g. with escape ' (char)0 becomes '00).
*/
extern void GBsetEscapeChar(model_t model,char escape);
/// Get the size of the string representation of a chunk.

extern int GBstringLength(model_t model,int type_no,int string_no);
/** Get the string representation of a chunk.

Data must be string length plus 1 long, as the terminating 0 is also written.
*/
extern char* GBstringGet(model_t model,int type_no,int string_no,char* data);

//@}


#endif

