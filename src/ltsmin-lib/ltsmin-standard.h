/*
 * ltsmin-standard.h
 *
 *  Defines constants that are used internally by LTSmin to represent
 *  transition systems and tool behavior.
 *
 *  Created on: Aug 9, 2012
 *      Author: laarman
 */

#ifndef LTSMIN_STANDARD_H_
#define LTSMIN_STANDARD_H_

/**
 * Exit codes
 */

#define LTSMIN_EXIT_COUNTER_EXAMPLE     1
#define LTSMIN_EXIT_SUCCESS             0
#define LTSMIN_EXIT_FAILURE             255
#define LTSMIN_EXIT_UNSOUND             2

/**
 * Matrices
 */

#define LTSMIN_MATRIX_ACTIONS_READS     "dm_actions_reads"
#define LTSMIN_MUST_DISABLE_MATRIX      "dm_must_disable"
#define LTSMIN_MUST_ENABLE_MATRIX       "dm_must_enable"
#define LTSMIN_NOT_LEFT_ACCORDS         "dm_not_left_accords"
/* weaker version of NEVER-coenabledness: guard/group instead of guard/guard */
/* note that we also take the inverse of coenabledness: never-coenabledness */
/* TODO: make GBsetGuardCoEnabledInfo obsolete */
#define LTSMIN_GUARD_GROUP_NOT_COEN     "dm_guard_group_not_coen"

/**
 * Types
 */

#define LTSMIN_TYPE_BOOL                "bool"


/**
 * Values
 */

#define LTSMIN_VALUE_BOOL_FALSE         "false" // GBchunkPutAt(.., 0)
#define LTSMIN_VALUE_BOOL_TRUE          "true"  // GBchunkPutAt(.., 1)
#define LTSMIN_VALUE_ACTION_PROGRESS    "progress"  // progress actions
/* A value that contains "<progress>" is counted as a progress transition */
#define LTSMIN_VALUE_STATEMENT_PROGRESS "<progress>"


/**
 * Edges
 */

/** The invisible action.
 */
#define LTSMIN_EDGE_VALUE_TAU           "tau"

/** actions (has to be a prefix: defined as "action_label" in mcrl) */
#define LTSMIN_EDGE_TYPE_ACTION_PREFIX  "action"

/** actions class, for use in e.g. Mapa models. */
#define LTSMIN_EDGE_TYPE_ACTION_CLASS  "action_class"

/**
 * Statements, used for:
 * - pretty printing model transitions
 * - extracting structural features of transitions, see LTSMIN_VALUE_STATEMENT_PROGRESS
 * The assumption is here that this type contains at least as many values as
 * transition groups.
 **/
#define LTSMIN_EDGE_TYPE_STATEMENT      "statement"

/**
@brief The name and type of the hyper edge group.

Hyper edges are represented using an extra edge label.
If the value of this label is 0 then the edge is not an hyper edge.
Otherwise edges, which start is the same state and are marked with the same hyper edge group
are part of a single hyper edge.
*/
#define LTSMIN_EDGE_TYPE_HYPEREDGE_GROUP "group" 

/**
 * States labels
 */

/* Guard prefixes */
#define LTSMIN_LABEL_TYPE_GUARD_PREFIX  "guard"


#endif /* LTSMIN_STANDARD_H_ */
