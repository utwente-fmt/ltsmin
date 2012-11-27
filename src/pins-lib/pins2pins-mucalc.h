/*
 * pins2pins-mucalc.h
 *
 *  Created on: 23 Nov 2012
 *      Author: kant
 */

#ifndef PINS2PINS_MUCALC_H_
#define PINS2PINS_MUCALC_H_

#include <ltsmin-lib/mucalc-parse-env.h>
#include <ltsmin-lib/mucalc-syntax.h>
//#include <ltsmin-lib/mucalc-lexer.h>
#include <pins-lib/pg-types.h>

typedef struct mucalc_node {
    mucalc_expr_t       expression;
    pg_player_enum_t    player;
    int                 priority;
} mucalc_node_t;


typedef struct mucalc_group_entry {
    mucalc_node_t       node;
    int                 parent_group;
} mucalc_group_entry_t;


typedef struct mucalc_groupinfo {
    int                     group_count;
    mucalc_group_entry_t   *entries;
    int                     node_count;
    mucalc_node_t          *nodes;
    int                     variable_count;
    int                    *fixpoint_nodes; // mapping from variable index to node number
    int                     initial_node;
} mucalc_groupinfo_t;


typedef struct mucalc_context {
    model_t         parent;
    int             action_label_index;
    int             action_label_type_no;
    mucalc_parse_env_t env;
    int             mu_idx;
    int             len;
    int             groups;
    mucalc_groupinfo_t groupinfo;
} mucalc_context_t;


#endif /* PINS2PINS_MUCALC_H_ */
