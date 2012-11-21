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
 * Edges
 */

/* tau actions */
#define LTSMIN_EDGE_VALUE_TAU           "tau"

/* actions (has to be a prefix: defined as "action_label" in mcrl) */
#define LTSMIN_EDGE_TYPE_ACTION_PREFIX  "action"

/* statements (promela code line numbers in SpinS) */
#define LTSMIN_EDGE_TYPE_STATEMENT      "statement"

/**
 * States labels
 */

/* Guard prefixes */
#define LTSMIN_LABEL_TYPE_GUARD_PREFIX  "guard"


#endif /* LTSMIN_STANDARD_H_ */
