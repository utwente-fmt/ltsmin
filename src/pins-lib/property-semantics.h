/*
 * property-semantics.c
 *
 *  Created on: Aug 8, 2012
 *      Author: laarman
 */

#include <ltsmin-lib/ltsmin-tl.h>
#include <pins-lib/pins.h>
#include <ltsmin-lib/ltsmin-parse-env.h>
#include <ltsmin-lib/ltsmin-standard.h>

extern ltsmin_expr_t parse_file(const char *file, parse_f parser, model_t model);

extern ltsmin_expr_t parse_file_env(const char *file, parse_f parser,
                                    model_t model, ltsmin_parse_env_t env);

/* mark touched variables in a state-sized array */
extern void mark_predicate(model_t m, ltsmin_expr_t e, int *dep, ltsmin_parse_env_t env);

/* mark all groups that WRITE to a variable influencing the expression */
extern void mark_visible(model_t model, ltsmin_expr_t e, ltsmin_parse_env_t env);

/**
 * evaluate predicate on state
 * NUM values may be looked up in the chunk tables (for chunk types), hence the
 * conditional.
 */
static inline int
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
            return (eval_predicate(model, e->arg1, ti, state, N, env) ==
                    eval_predicate(model, e->arg2, ti, state, N, env));
        case PRED_AND:
            return (eval_predicate(model, e->arg1, ti, state, N, env) &&
                    eval_predicate(model, e->arg2, ti, state, N, env));
        case PRED_OR:
            return (eval_predicate(model, e->arg1, ti, state, N, env) ||
                    eval_predicate(model, e->arg2, ti, state, N, env));
        case PRED_IMPLY:
            return ((!eval_predicate(model, e->arg1, ti, state, N, env)) ||
                      eval_predicate(model, e->arg2, ti, state, N, env));
        case PRED_EQUIV:
            return ((!eval_predicate(model, e->arg1, ti, state, N, env)) ==
                    (!eval_predicate(model, e->arg2, ti, state, N, env)) );
        default:
            LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
    }
    return 0;
}
