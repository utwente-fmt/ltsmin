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

/* set visibility in PINS and dependencies in e->annotation->state_deps */
extern void set_pins_semantics(model_t model, ltsmin_expr_t e, ltsmin_parse_env_t env, bitvector_t *deps);

extern long eval_predicate(model_t model, ltsmin_expr_t e, int *state, ltsmin_parse_env_t env);

#endif // PROPERTY_SEMANTICS_H
