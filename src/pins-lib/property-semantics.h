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
#include <util-lib/util.h>

extern ltsmin_expr_t parse_file(const char *file, parse_f parser, model_t model);

extern ltsmin_expr_t parse_file_env(const char *file, parse_f parser,
                                    model_t model, ltsmin_parse_env_t env);

/* mark touched state variables in e->deps */
extern void mark_predicate(model_t m, ltsmin_expr_t e, ltsmin_parse_env_t env);

/* mark all groups that WRITE to a variable influencing the expression */
extern void mark_visible(model_t model, ltsmin_expr_t e, ltsmin_parse_env_t env);

/**
 * evaluate predicate on state
 * NUM values may be looked up in the chunk tables (for chunk types), hence the
 * conditional.
 */
static inline long
eval_predicate(model_t model, ltsmin_expr_t e, transition_info_t *ti, int *state,
               int N, ltsmin_parse_env_t env)
{
    switch (e->token) {
        case PRED_TRUE:
            return 1;
        case PRED_FALSE:
            return 0;
        case PRED_NUM:
            return -1 == e->num ? e->idx : e->num;
        case PRED_SVAR:
            if (e->idx < N) { // state variable
                return state[e->idx];
            } else { // state label
                return GBgetStateLabelLong(model, e->idx - N, state);
            }
        case PRED_CHUNK:
        case PRED_VAR:
            if (-1 == e->num) {
                LTSminLogExpr (error, "Unbound variable in predicate expression: ", e, env);
                HREabort (LTSMIN_EXIT_FAILURE);
            }
            return e->num;
        case PRED_NOT:
            return !eval_predicate(model, e->arg1, ti, state, N, env);
        case PRED_EQ:
            return eval_predicate(model, e->arg1, ti, state, N, env) ==
                    eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_NEQ:
            return eval_predicate(model, e->arg1, ti, state, N, env) !=
                    eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_AND:
            return eval_predicate(model, e->arg1, ti, state, N, env) &&
                    eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_OR:
            return eval_predicate(model, e->arg1, ti, state, N, env) ||
                    eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_IMPLY:
            return !eval_predicate(model, e->arg1, ti, state, N, env) ||
                      eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_EQUIV:
            return !eval_predicate(model, e->arg1, ti, state, N, env) ==
                    !eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_LT:
            return eval_predicate(model, e->arg1, ti, state, N, env) <
                    eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_LEQ:
            return eval_predicate(model, e->arg1, ti, state, N, env) <=
                    eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_GT:
            return eval_predicate(model, e->arg1, ti, state, N, env) >
                    eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_GEQ:
            return eval_predicate(model, e->arg1, ti, state, N, env) >=
                    eval_predicate(model, e->arg2, ti, state, N, env);
        case PRED_MULT: {
            const long l = eval_predicate(model, e->arg1, ti, state, N, env);
            const long r = eval_predicate(model, e->arg2, ti, state, N, env);
            if (long_mult_overflow(l, r)) {
                LTSminLogExpr (error, "integer overflow in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return l * r;
        }
        case PRED_DIV: {
            const long l = eval_predicate(model, e->arg1, ti, state, N, env);
            const long r = eval_predicate(model, e->arg2, ti, state, N, env);
            if (r == 0 || ((l == LONG_MIN) && r == -1)) {
                LTSminLogExpr (error, "division by zero in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return l / r;
        }
        case PRED_REM: {
            const long l = eval_predicate(model, e->arg1, ti, state, N, env);
            const long r = eval_predicate(model, e->arg2, ti, state, N, env);
            if (r == 0 || ((l == LONG_MIN) && r == -1)) {
                LTSminLogExpr (error, "division by zero in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return l % r;
        }
        case PRED_ADD: {
            const long l = eval_predicate(model, e->arg1, ti, state, N, env);
            const long r = eval_predicate(model, e->arg2, ti, state, N, env);
            if ((r > 0 && l > LONG_MAX - r) || (r < 0 && l < LONG_MIN - r)) {
                LTSminLogExpr (error, "integer overflow in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return l + r;
        }
        case PRED_SUB: {
            const long l = eval_predicate(model, e->arg1, ti, state, N, env);
            const long r = eval_predicate(model, e->arg2, ti, state, N, env);
            if ((r > 0 && l < LONG_MIN + r) || (r < 0 && l > LONG_MAX + r)) {
                LTSminLogExpr (error, "integer overflow in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }            
            return l - r;
        }
        default:
            LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
    }
    return 0;
}

#endif // PROPERTY_SEMANTICS_H
