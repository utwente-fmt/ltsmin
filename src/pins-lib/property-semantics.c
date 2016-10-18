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
    int count = pins_chunk_count (m,type);
    e->num = pins_chunk_put (m,type,c);
    if (strict && count != pins_chunk_count (m,type)) // value was added
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
        c.data = SIgetC(env->values, e->idx, (int*) &c.len);
        lookup_type_value (e, typeno, c, model, format==LTStypeEnum);
        Debug ("Bound '%s' to %d in table for type '%s'", c.data,
               e->num, lts_type_get_state_type(GBgetLTStype(model),typeno));
        break;
    }
}

static inline int
type_check_get_type(lts_type_t lts_type, const char* type, ltsmin_expr_t e, ltsmin_parse_env_t env)
{
    const int typeno = lts_type_find_type(lts_type, type);
    if (typeno == -1) {
        const char* ex = LTSminPrintExpr(e, env);
        Abort("Expression with type \"%s\" can only be used if the language front-end defines it: \"%s\"", type, ex);
    } else return typeno;
}

static inline void
type_check_require_type(lts_type_t lts_type, int typeno, const char* type, ltsmin_expr_t e, ltsmin_parse_env_t env)
{
    const char* name = lts_type_get_type(lts_type, typeno);
    if (strcmp(name, type) != 0) {
        const char* ex = LTSminPrintExpr(e, env);
        Abort("Expression is not of required type \"%s\": \"%s\"", type, ex);
    }
}

static inline void
type_check_require_format(lts_type_t lts_type, int type, const data_format_t required[], int n, ltsmin_expr_t e, ltsmin_parse_env_t env, const char* msg)
{
    /* chunks for numeric types could be implemented with the bignum interface. */

    const data_format_t format = lts_type_get_format(lts_type, type);

    for (int i = 0; i < n; i++) {
        if (format == required[i]) return;
    }
    const char* ex = LTSminPrintExpr(e, env);
    Abort("Only %s type formats are supported: \"%s\"", msg, ex);
}

/* type checks the expression and binds values to types, e.g.: "init.a == Off" (Off = 2) */
/* avoid rebuilding the whole tree, by storing extra info for the chunks */
static int
ltsmin_expr_type_check(ltsmin_expr_t e,ltsmin_parse_env_t env,model_t model)
{
    switch(e->node_type) {
        case BINARY_OP: {
            const int left = ltsmin_expr_type_check(e->arg1, env, model);
            const int right = ltsmin_expr_type_check(e->arg2, env, model);

            // test for required type
            switch(e->token) {
                case PRED_LT:  case PRED_LEQ: case PRED_GT: case PRED_GEQ:
                case PRED_MULT: case PRED_DIV: case PRED_REM: case PRED_ADD: case PRED_SUB: {
                    type_check_require_type(GBgetLTStype(model), left, LTSMIN_TYPE_NUMERIC, e->arg1, env);
                    type_check_require_type(GBgetLTStype(model), right, LTSMIN_TYPE_NUMERIC, e->arg2, env);
                    break;
                }
                case PRED_OR: case PRED_AND: case PRED_EQUIV: case PRED_IMPLY: {
                    type_check_require_type(GBgetLTStype(model), left, LTSMIN_TYPE_BOOL, e->arg1, env);
                    type_check_require_type(GBgetLTStype(model), right, LTSMIN_TYPE_BOOL, e->arg2, env);
                    break;
                }
                case PRED_EQ: case PRED_NEQ: {
                    /* In the first two cases we must bind a chunk to a type.
                     * In the third case we have unbound chunks.
                     * In the fourth case we must check if the types are the same. */
                    if (left >= 0 && right < 0) {
                        ltsmin_expr_lookup_value (e, e->arg2, left, env, model);
                        return left;
                    } else if (right >= 0 && left < 0) {
                        ltsmin_expr_lookup_value (e, e->arg1, right, env, model);
                        return right;
                    } else if (left == -1 && right == -1) {
                        // this is something like "chunk1" == "chunk2"
                        const char* ex = LTSminPrintExpr(e, env);
                        Abort("Can not bind types for expression: \"%s\"", ex);
                    } else if (left != right) {
                        const char* ex = LTSminPrintExpr(e, env);
                        Abort("LHS (%s) and RHS (%s) are not of the same type: \"%s\"",
                                lts_type_get_type(GBgetLTStype(model), left),
                                lts_type_get_type(GBgetLTStype(model), right),
                                ex);
                    }
                    break;
                }
                default: {
                    LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
                    HREabort (LTSMIN_EXIT_FAILURE);
                }
            }

            // test for required format if the type is the same
            if (left >= 0 && right >= 0) { 
                switch(e->token) {
                    case PRED_LT:  case PRED_LEQ: case PRED_GT: case PRED_GEQ:
                    case PRED_MULT: case PRED_DIV: case PRED_REM: case PRED_ADD: case PRED_SUB: {
                        const data_format_t formats[2] = { LTStypeDirect, LTStypeRange };
                        type_check_require_format(GBgetLTStype(model), left, formats, 2, e->arg1, env, "direct, range");
                        type_check_require_format(GBgetLTStype(model), right, formats, 2, e->arg2, env, "direct, range");
                        break;
                    }
                    case PRED_OR: case PRED_AND: case PRED_EQUIV: case PRED_IMPLY:
                    case PRED_EQ: case PRED_NEQ: {
                        if (lts_type_get_format(GBgetLTStype(model), left) != lts_type_get_format(GBgetLTStype(model), right)) {
                            const char* ex = LTSminPrintExpr(e, env);
                            Abort("LHS and RHS do not have the same type format: \"%s\"", ex);
                        }
                        break;
                    }
                    default: {
                        LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
                        HREabort (LTSMIN_EXIT_FAILURE);
                    }
                }
            }

            // now determine the type of this expression
            switch(e->token) {
                case PRED_MULT: case PRED_DIV: case PRED_REM: case PRED_ADD: case PRED_SUB:
                    return type_check_get_type(GBgetLTStype(model), LTSMIN_TYPE_NUMERIC, e, env);
                default:
                    return type_check_get_type(GBgetLTStype(model), LTSMIN_TYPE_BOOL, e, env);
            }
        }
        case UNARY_OP: {
            const int type = ltsmin_expr_type_check(e->arg1, env, model);
            switch(e->token) {
                case PRED_NOT: {
                    type_check_require_type(GBgetLTStype(model), type, LTSMIN_TYPE_BOOL, e, env);
                    return type;
                }
                default: {
                    LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
                    HREabort (LTSMIN_EXIT_FAILURE);
                }
            }
        }
        default:
        switch(e->token) {
            case PRED_SVAR: {
                lts_type_t ltstype = GBgetLTStype (model);
                int N = lts_type_get_state_length (ltstype);
                if (e->idx < N)
                    return lts_type_get_state_typeno (ltstype, e->idx);
                else
                    return lts_type_get_state_label_typeno (ltstype, e->idx - N);
            }
            case PRED_NUM: {
                return type_check_get_type(GBgetLTStype(model), LTSMIN_TYPE_NUMERIC, e, env);
            }
            case PRED_CHUNK: {
                return -1;
            }
            case PRED_FALSE:
            case PRED_TRUE: {
                return type_check_get_type(GBgetLTStype(model), LTSMIN_TYPE_BOOL, e, env);
            }          
            default: {
                LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
                HREabort (LTSMIN_EXIT_FAILURE);
            }
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
    const int type = ltsmin_expr_type_check(expr, env, model);
    type_check_require_type(GBgetLTStype(model),type, LTSMIN_TYPE_BOOL, expr, env);
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
mark_predicate (model_t m, ltsmin_expr_t e, ltsmin_parse_env_t env)
{
    if (!e) return;
    
    bitvector_create(&e->deps, pins_get_state_variable_count(m));
    
    switch(e->node_type) {
    case BINARY_OP:
        mark_predicate(m,e->arg1,env);
        mark_predicate(m,e->arg2,env);
        bitvector_copy(&e->deps, &e->arg1->deps);
        bitvector_union(&e->deps, &e->arg2->deps);
        break;
    case UNARY_OP:
        mark_predicate(m,e->arg1,env);
        bitvector_copy(&e->deps, &e->arg1->deps);
        break;
    default:
        switch(e->token) {
        case PRED_TRUE:
        case PRED_FALSE:
        case PRED_NUM:
        case PRED_VAR:
        case PRED_CHUNK:
            break;
        case PRED_SVAR: {
            lts_type_t ltstype = GBgetLTStype(m);
            int N = lts_type_get_state_length (ltstype);
            if (e->idx < N) { // state variable
                bitvector_set(&e->deps, e->idx);
            } else { // state label
                HREassert (e->idx < N + lts_type_get_state_label_count(ltstype));
                matrix_t *sl = GBgetStateLabelInfo (m);
                HREassert (N == dm_ncols(sl));
                dm_bitvector_row(&e->deps, sl, e->idx - N);
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
