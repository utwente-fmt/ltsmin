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

#include <hre/user.h>


/**
 * Exit codes
 */

#define LTSMIN_EXIT_COUNTER_EXAMPLE     1
#define LTSMIN_EXIT_SUCCESS             HRE_EXIT_SUCCESS
#define LTSMIN_EXIT_FAILURE             HRE_EXIT_FAILURE


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

/* tau actions */
#define LTSMIN_EDGE_VALUE_TAU           "tau"

/* actions (has to be a prefix: defined as "action_label" in mcrl) */
#define LTSMIN_EDGE_TYPE_ACTION_PREFIX  "action"

/**
 * Statements, used for:
 * - pretty printing model transitions
 * - extracting structural features of transitions, see LTSMIN_VALUE_STATEMENT_PROGRESS
 * The assumption is here that this type contains at least as many values as
 * transition groups.
 **/
#define LTSMIN_EDGE_TYPE_STATEMENT      "statement"


/**
 * States labels
 */

/* Guard prefixes */
#define LTSMIN_LABEL_TYPE_GUARD_PREFIX  "guard"


#endif /* LTSMIN_STANDARD_H_ */
