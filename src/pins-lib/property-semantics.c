/*
 * property-semantics.c
 *
 *  Created on: Aug 8, 2012
 *      Author: laarman
 */
#include <hre/config.h>

#include <stdbool.h>

#include <pins-lib/property-semantics.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/por/pins2pins-por.h>

void set_pins_semantics(model_t model, ltsmin_expr_t e, ltsmin_parse_env_t env, bitvector_t *deps, bitvector_t *sl_deps)
{
    const lts_type_t lts_type = GBgetLTStype(model);

    switch (e->node_type) {
        case BINARY_OP: {
            set_pins_semantics(model, e->arg1, env, deps, sl_deps);
            set_pins_semantics(model, e->arg2, env, deps, sl_deps);
            break;
        }
        case UNARY_OP: {
            set_pins_semantics(model, e->arg1, env, deps, sl_deps);
            break;
        }
        case SVAR: {
            const int N = lts_type_get_state_length(lts_type);
            if (e->idx < N) { // state variable
                if (deps != NULL) bitvector_set(deps, e->idx);
                if (PINS_POR) pins_add_state_variable_visible(model, e->idx);
            } else { // state label
                if (sl_deps != NULL) bitvector_set(sl_deps, e->idx - N);
                if (deps != NULL) dm_row_union(deps, GBgetStateLabelInfo(model), e->idx - N);
                if (PINS_POR) pins_add_state_label_visible(model, e->idx - N);
            }
            break;
        }
        case EVAR: {
            const int type = lts_type_get_edge_label_typeno(GBgetLTStype(model), e->idx);
            const int n_chunks = pins_chunk_count(model, type);
            const int value = LTSminExprSibling(e)->idx;

            chunk c;
            c.data = SIgetC(env->values, value, (int*) &c.len);

            const int idx = pins_chunk_put(model, type, c);

            if (lts_type_get_format(GBgetLTStype(model), type) == LTStypeEnum) {
                if (pins_chunk_count(model, type) != n_chunks) {
                    char id[c.len * 2 + 6];
                    chunk2string(c, sizeof(id), id);
                    Warning(info, "Value for identifier '%s' cannot be found in table for enum type %s.",
                        id, lts_type_get_type(GBgetLTStype(model),type));
                }
            }

            int* groups = NULL;
            const int n = GBgroupsOfEdge(model, e->idx, idx, &groups);
            if (n > 0) {
                for (int k = 0; k < n; k++) {
                    const int group = groups[k];
                    if (PINS_POR) pins_add_group_visible(model, group);
                    if (deps != NULL) dm_row_union(deps, GBgetDMInfoRead(model), group);
                }
                RTfree(groups);
            } else {
                char s[c.len * 2 + 6];
                chunk2string(c, sizeof(s), s);
                Abort("There is no group that can produce edge label %s", s);
            }            
            break;
        }
        default:
            break;
    }
}

struct evar_info {
    int idx; // edge label to look for
    int num; // edge value to look for
    int exists; // whether an transition with such an edge exists
};

static void
evar_cb(void *context, transition_info_t *ti, int *dst, int *cpy)
{
    (void) dst; (void) cpy;
    struct evar_info* ctx = (struct evar_info*) context;
    ctx->exists = ctx->exists || ti->labels[ctx->idx] == ctx->num;
}

long
eval_predicate(model_t model, ltsmin_expr_t e, int *state, ltsmin_parse_env_t env)
{
    const int N = lts_type_get_state_length(GBgetLTStype(model));

    switch (e->token) {
        case PRED_TRUE:
            return 1;
        case PRED_FALSE:
            return 0;
        case PRED_NUM:
            return e->idx;
        case PRED_SVAR:
            if (e->idx < N) { // state variable
                return state[e->idx];
            } else { // state label
                return GBgetStateLabelLong(model, e->idx - N, state);
            }
        case PRED_EVAR: {
            // test whether the state has at least one transition (existential) with a specific edge
            struct evar_info ctx;
            ctx.idx = e->idx;
            ctx.num = eval_predicate(model, LTSminExprSibling(e), state, env);
            ctx.exists = 0;

            int* groups = NULL;
            const int n = GBgroupsOfEdge(model, e->idx, ctx.num, &groups);

            if (n > 0) {
                for (int i = 0; i < n && ctx.exists == 0; i++) {
                    GBgetTransitionsLong(model, groups[i], state, evar_cb, &ctx);
                }
                RTfree(groups);
                return ctx.exists ? ctx.num : -1;
            } else return -1;
        }
        case PRED_CHUNK: {
            chunk c;
            c.data = SIgetC(env->values, e->idx, (int*) &c.len);
            return pins_chunk_put(model, ltsmin_expr_type_check(LTSminExprSibling(e), env, GBgetLTStype(model)), c);
        }
        case PRED_NOT:
            return !eval_predicate(model, e->arg1, state, env);
        case PRED_EQ:
            return eval_predicate(model, e->arg1, state, env) ==
                    eval_predicate(model, e->arg2, state, env);
        case PRED_NEQ:
            return eval_predicate(model, e->arg1, state, env) !=
                    eval_predicate(model, e->arg2, state, env);
        case PRED_AND:
            return eval_predicate(model, e->arg1, state, env) &&
                    eval_predicate(model, e->arg2, state, env);
        case PRED_OR:
            return eval_predicate(model, e->arg1, state, env) ||
                    eval_predicate(model, e->arg2, state, env);
        case PRED_IMPLY:
            return !eval_predicate(model, e->arg1, state, env) ||
                      eval_predicate(model, e->arg2, state, env);
        case PRED_EQUIV:
            return !eval_predicate(model, e->arg1, state, env) ==
                    !eval_predicate(model, e->arg2, state, env);
        case PRED_LT:
            return eval_predicate(model, e->arg1, state, env) <
                    eval_predicate(model, e->arg2, state, env);
        case PRED_LEQ:
            return eval_predicate(model, e->arg1, state, env) <=
                    eval_predicate(model, e->arg2, state, env);
        case PRED_GT:
            return eval_predicate(model, e->arg1, state, env) >
                    eval_predicate(model, e->arg2, state, env);
        case PRED_GEQ:
            return eval_predicate(model, e->arg1, state, env) >=
                    eval_predicate(model, e->arg2, state, env);
        case PRED_MULT: {
            const long l = eval_predicate(model, e->arg1, state, env);
            const long r = eval_predicate(model, e->arg2, state, env);
            if (long_mult_overflow(l, r)) {
                LTSminLogExpr (error, "integer overflow in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return l * r;
        }
        case PRED_DIV: {
            const long l = eval_predicate(model, e->arg1, state, env);
            const long r = eval_predicate(model, e->arg2, state, env);
            if (r == 0 || ((l == LONG_MIN) && r == -1)) {
                LTSminLogExpr (error, "division by zero in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return l / r;
        }
        case PRED_REM: {
            const long l = eval_predicate(model, e->arg1, state, env);
            const long r = eval_predicate(model, e->arg2, state, env);
            if (r == 0 || ((l == LONG_MIN) && r == -1)) {
                LTSminLogExpr (error, "division by zero in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return l % r;
        }
        case PRED_ADD: {
            const long l = eval_predicate(model, e->arg1, state, env);
            const long r = eval_predicate(model, e->arg2, state, env);
            if ((r > 0 && l > LONG_MAX - r) || (r < 0 && l < LONG_MIN - r)) {
                LTSminLogExpr (error, "integer overflow in: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return l + r;
        }
        case PRED_SUB: {
            const long l = eval_predicate(model, e->arg1, state, env);
            const long r = eval_predicate(model, e->arg2, state, env);
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