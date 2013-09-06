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

/**
 * Enums need their values to be present in the tables, hence the strict lookup.
 */
static void
lookup_type_value (ltsmin_expr_t e, int type, const chunk c,
                   model_t m, bool strict)
{
    HREassert (NULL != c.data, "Empty chunk");
    int count = GBchunkCount(m,type);
    e->num = GBchunkPut(m,type,c);
    if (strict && count != GBchunkCount(m,type)) // value was added
        Warning (info, "Value for identifier '%s' cannot be found in table for enum type '%s'.",
                 c.data, lts_type_get_type(GBgetLTStype(m),type));
    e->lts_type = type;
}

static void
ltsmin_expr_lookup_value(ltsmin_expr_t top, ltsmin_expr_t e, int typeno,
                         ltsmin_parse_env_t env, model_t model)
{
    switch(e->node_type) {
    case VAR:
    case CHUNK:
    case INT:
        break;
    default:
        return;
    }
    chunk c;
    data_format_t format = lts_type_get_format(GBgetLTStype(model), typeno);
    switch (format) {
    case LTStypeDirect:
    case LTStypeRange:
        if (INT != e->node_type)
            Abort ("Expected an integer value for comparison: %s", LTSminPrintExpr(top, env));
        break;
    case LTStypeEnum:
    case LTStypeChunk:
        c.data = env->buffer;
        c.len = LTSminSPrintExpr(c.data, e, env);
        HREassert (c.len < ENV_BUFFER_SIZE, "Buffer overflow in print expression");
        lookup_type_value (e, typeno, c, model, format==LTStypeEnum);
        Debug ("Bound '%s' to %d in table for type '%s'", c.data,
               e->num, lts_type_get_state_type(GBgetLTStype(model),typeno));
        break;
    }
}

/* looks up the type values in expressions, e.g.: "init.a == Off" (Off = 2) */
/* avoid rebuilding the whole tree, by storing extra info for the chunks */
static int
ltsmin_expr_lookup_values(ltsmin_expr_t ltl,ltsmin_parse_env_t env,model_t model)
{ //return type(SVAR) idx or -1
    if (!ltl) return -1;
    int left, right;
    switch(ltl->node_type) {
    case BINARY_OP:
        left = ltsmin_expr_lookup_values(ltl->arg1, env, model);
        right = ltsmin_expr_lookup_values(ltl->arg2, env, model);
        switch(ltl->token) {
        case PRED_EQ:
            if (left >= 0) { // type(SVAR)
                ltsmin_expr_lookup_value (ltl, ltl->arg2, left, env, model);
            } else if (right >= 0) { // type(SVAR)
                ltsmin_expr_lookup_value (ltl, ltl->arg1, right, env, model);
            }
        }
        return -1;
    case UNARY_OP:
        ltsmin_expr_lookup_values(ltl->arg1, env, model);
        return -1;
    default:
        switch(ltl->token) {
        case SVAR: {
            lts_type_t ltstype = GBgetLTStype (model);
            int N = lts_type_get_state_length (ltstype);
            if (ltl->idx < N)
                return lts_type_get_state_typeno (ltstype, ltl->idx);
            else
                return lts_type_get_state_label_typeno (ltstype, ltl->idx - N);
        }
        default:
            return -1;
        }
    }
}

/**
 * Parses file using parser
 * Chunk values are looked up in PINS.
 */
ltsmin_expr_t
parse_file_env(const char *file, parse_f parser, model_t model,
               ltsmin_parse_env_t env)
{
    lts_type_t ltstype = GBgetLTStype(model);
    ltsmin_expr_t expr = parser(file, env, ltstype);
    ltsmin_expr_lookup_values(expr, env, model);
    env->expr = NULL;
    return expr;
}

ltsmin_expr_t
parse_file(const char *file, parse_f parser, model_t model)
{
    ltsmin_parse_env_t env = LTSminParseEnvCreate();
    ltsmin_expr_t expr = parse_file_env(file, parser, model, env);
    LTSminParseEnvDestroy(env);
    return expr;
}

void
mark_predicate (model_t m, ltsmin_expr_t e, int *dep, ltsmin_parse_env_t env)
{
    if (!e) return;
    switch(e->node_type) {
    case BINARY_OP:
        mark_predicate(m,e->arg1,dep,env);
        mark_predicate(m,e->arg2,dep,env);
        break;
    case UNARY_OP:
        mark_predicate(m,e->arg1,dep,env);
        break;
    default:
        switch(e->token) {
        case PRED_TRUE:
        case PRED_FALSE:
        case PRED_NUM:
        case PRED_VAR:
        case PRED_CHUNK:
            break;
        case PRED_EQ:
            mark_predicate(m,e->arg1, dep,env);
            mark_predicate(m,e->arg2, dep,env);
            break;
        case PRED_SVAR: {
            lts_type_t ltstype = GBgetLTStype(m);
            int N = lts_type_get_state_length (ltstype);
            if (e->idx < N) { // state variable
                dep[e->idx] = 1;
            } else { // state label
                HREassert (e->idx < N + lts_type_get_state_label_count(ltstype));
                matrix_t *sl = GBgetStateLabelInfo (m);
                HREassert (N == dm_ncols(sl));
                for (int i = 0; i < N; i++) {
                    if (dm_is_set(sl, e->idx - N, i)) dep[i] = 1;
                }
            }
            break;
        }
        default:
            LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
        }
        break;
    }
}

void
mark_visible(model_t model, ltsmin_expr_t e, ltsmin_parse_env_t env)
{
    int *visibility = GBgetPorGroupVisibility (model);
    HREassert (visibility != NULL, "POR layer present, but no visibility info found.");
    if (!e) return;
    switch(e->node_type) {
    case BINARY_OP:
        mark_visible(model,e->arg1,env);
        mark_visible(model,e->arg2,env);
        break;
    case UNARY_OP:
        mark_visible(model,e->arg1,env);
        break;
    default:
        switch(e->token) {
        case PRED_TRUE:
        case PRED_FALSE:
        case PRED_NUM:
        case PRED_VAR:
        case PRED_CHUNK:
            break;
        case PRED_EQ:
            mark_visible(model, e->arg1,env);
            mark_visible(model, e->arg2,env);
            break;
        case PRED_SVAR: {
            int                 N = pins_get_state_variable_count (model);
            if (e->idx < N) {
                pins_add_state_variable_visible (model, e->idx);
            } else { // state label
                HREassert (e->idx < N + (int)pins_get_state_label_count(model));
                pins_add_state_label_visible (model, e->idx - N);
            }
          } break;
        default:
            LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
        }
        break;
    }
}
