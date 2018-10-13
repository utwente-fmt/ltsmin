/*
 * property-semantics.c
 *
 *  Created on: Aug 8, 2012
 *      Author: laarman
 */

#ifndef PROPERTY_SEMANTICS_H
#define PROPERTY_SEMANTICS_H

#include <limits.h>

#include <ltsmin-lib/ltsmin-tl.h>
#include <ltsmin-lib/ltsmin-parse-env.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <util-lib/util.h>

/* This function must always be called once, before calls to eval_trans_predicate() and eval_state_predicate().
 * 1. Initialize a cache that contains the groups that may enable some edge variable.
 * 2. Initialize a cache that contains the type and value index for a chunk.
 */
extern void set_groups_of_edge(model_t model, ltsmin_expr_t e, ltsmin_parse_env_t env);

/* set visibility in PINS and dependencies in deps (if not NULL) */
extern void set_pins_semantics(model_t model, ltsmin_expr_t e, bitvector_t *deps, bitvector_t *sl_deps);

extern long eval_trans_predicate(model_t model, ltsmin_expr_t e, int *state, int* edge_labels, ltsmin_parse_env_t env);

extern long eval_state_predicate(model_t model, ltsmin_expr_t e, int *state, ltsmin_parse_env_t env);

#endif // PROPERTY_SEMANTICS_H
