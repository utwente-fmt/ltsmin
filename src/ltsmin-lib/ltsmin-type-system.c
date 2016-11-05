#include <hre/config.h>

#include <ltsmin-lib/lts-type.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <ltsmin-lib/ltsmin-type-system.h>

static inline int
hint_binary(const format_table_t f[DATA_FORMAT_SIZE][DATA_FORMAT_SIZE], char *msg, size_t size)
{
    int l = 0;
    for (int i = 0; i < DATA_FORMAT_SIZE; i++) {
        for (int j = 0; j < DATA_FORMAT_SIZE; j++) {
            if (f[i][j].error != -1) {
                l += snprintf(msg + (msg?l:0), size - l, "%s(%s, %s)", l == 0 ? "" : ", ",
                    data_format_string_generic(i), data_format_string_generic(j));
            }
        }
    }
    return l;
}

data_format_t
get_data_format_binary(const format_table_t f[DATA_FORMAT_SIZE][DATA_FORMAT_SIZE],
        ltsmin_expr_t e, ltsmin_parse_env_t env, data_format_t l, data_format_t r)
{
    const format_table_t entry = f[l][r];
    if (entry.error == -1) {
        const char* ex = LTSminPrintExpr(e, env);
        char hint[hint_binary(f, NULL, 0) + 1];
        hint_binary(f, hint, sizeof(hint));
        Abort(
            "Incompatible type formats (LHS = \"%s\", RHS = \"%s\") for expression: \"%s\", "
            "expecting format pair (LHS, RHS) to be any pair in {%s}",
            data_format_string_generic(l), data_format_string_generic(r), ex, hint);
    } else return entry.df;
}

static inline int
hint_unary(const format_table_t f[DATA_FORMAT_SIZE], char *msg, size_t size)
{
    int l = 0;
    for (int i = 0; i < DATA_FORMAT_SIZE; i++) {
        if (f[i].error != -1) {
            l += snprintf(msg + (msg?l:0), size - l, "%s%s", l == 0 ? "" : ", ",
                data_format_string_generic(i));
        }
    }
    return l;
}

data_format_t
get_data_format_unary(const format_table_t f[DATA_FORMAT_SIZE],
        ltsmin_expr_t e, ltsmin_parse_env_t env, data_format_t c)
{
    const format_table_t entry = f[c];
    if (entry.error == -1) {
        const char* ex = LTSminPrintExpr(e, env);
        char hint[hint_unary(f, NULL, 0) + 1];
        hint_unary(f, hint, sizeof(hint));
        Abort(
            "Incompatible type format (\"%s\") for expression: \"%s\", "
            "expecting format to be any of {%s}",
            data_format_string_generic(c), ex, hint);
    }
    return entry.df;
}

int
get_typeno(ltsmin_expr_t e, lts_type_t lts_type)
{
    switch (e->token) {
        case SVAR: {
            const int N = lts_type_get_state_length(lts_type);
            if (e->idx < N) { // state variable
                return lts_type_get_state_typeno(lts_type, e->idx);
            } else { // state label
                return lts_type_get_state_label_typeno(lts_type, e->idx - N);
            }
        }
        case EVAR: {
            return lts_type_get_edge_label_typeno(lts_type, e->idx);
        }
        default: HREassert(false);
    }
}

void
verify_chunk(data_format_t df, ltsmin_expr_t e, ltsmin_parse_env_t env, int typeno, lts_type_t lts_type)
{
    if (df == LTStypeChunk) {
        const ltsmin_expr_t other = LTSminExprSibling(e);
        if (other != NULL) {
            if (typeno == -1) {
                if (other->token != SVAR && other->token != EVAR) {
                    LTSminLogExpr(error,
                        "A chunk should be paired with a state variable,"
                        " state label or edge label: ", e->parent, env);
                    HREabort(LTSMIN_EXIT_FAILURE);
                }
            } else {
                if (other->token == SVAR || other->token == EVAR) {
                    const int t = get_typeno(other, lts_type);
                    if (t != typeno) {
                        LTSminLogExpr(error,
                            "LHS and RHS are chunks and "
                            "should be of the same type: ", e->parent, env);
                        HREabort(LTSMIN_EXIT_FAILURE);
                    }
                }
            }
        } else {
            LTSminLogExpr(error,
                "A chunk should be paired.", e->parent, env);
            HREabort(LTSMIN_EXIT_FAILURE);
        }
    }
}

data_format_t
check_type_format_atom(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type)
{
    switch (e->node_type) {
        case BINARY_OP: {
            data_format_t l = check_type_format_atom(e->arg1, env, lts_type);
            data_format_t r = check_type_format_atom(e->arg2, env, lts_type);
            switch (e->token) {
                case S_MULT: case S_DIV: case S_REM:
                case S_ADD: case S_SUB:
                    return get_data_format_binary(MATH_OPS, e, env, l, r);
                case S_LEQ: case S_LT: case S_GEQ: case S_GT:
                    return get_data_format_binary(ORDER_OPS, e, env, l, r);
                case S_EQ: case S_NEQ:
                    return get_data_format_binary(REL_OPS, e, env, l, r);
                case S_EN: {
                    const data_format_t df = get_data_format_binary(REL_OPS, e, env, l, r);
                    if ((e->arg1->token == EVAR) == (e->arg2->token == EVAR)) {
                        LTSminLogExpr(error,
                            "Either the LHS or RHS (not both) should be an edge variable: ",
                            e, env);
                        HREabort(LTSMIN_EXIT_FAILURE);
                    }
                    return df;
                }
                default: {
                    LTSminLogExpr(error, "Unsupported expression: ", e, env);
                    HREabort(LTSMIN_EXIT_FAILURE);
                }
            }            
        }
        case UNARY_OP: {
            data_format_t c = check_type_format_atom(e->arg1, env, lts_type);
            return get_data_format_unary(UNARY_BOOL_OPS, e, env, c);
        }
        case SVAR:
        case EVAR: {
            const int typeno = get_typeno(e, lts_type);
            const data_format_t df = lts_type_get_format(lts_type, typeno);
            verify_chunk(df, e, env, typeno, lts_type);
            return df;
        }
        case CHUNK: {
            const data_format_t df = LTStypeChunk;
            verify_chunk(df, e, env, -1, lts_type);
            return df;
        }
        case INT: {
            return LTStypeSInt32;
        }
        case CONSTANT: {
            switch (e->token) {
                case S_FALSE: case S_TRUE: {
                    return LTStypeBool;
                }
                case S_MAYBE: {
                    return LTStypeTrilean;
                }
                default: {
                    LTSminLogExpr(error, "Unsupported expression: ", e, env);
                    HREabort(LTSMIN_EXIT_FAILURE);
                }
            }
        }
        default: {
            LTSminLogExpr(error, "Unsupported expression: ", e, env);
            HREabort(LTSMIN_EXIT_FAILURE);
        }
    }
}

data_format_t
check_type_format_pred(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type)
{
    switch (e->token) {
        case PRED_AND: case PRED_OR: case PRED_EQUIV: case PRED_IMPLY: {
            data_format_t l = check_type_format_pred(e->arg1, env, lts_type);
            data_format_t r = check_type_format_pred(e->arg2, env, lts_type);
            return get_data_format_binary(BOOL_OPS, e, env, l, r);
        }
        case PRED_NOT: {
            data_format_t c = check_type_format_pred(e->arg1, env, lts_type);
            return get_data_format_unary(UNARY_BOOL_OPS, e, env, c);
        }
        default: {
            return check_type_format_atom(e, env, lts_type);
        }
    }
}

data_format_t
check_type_format_LTL(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type)
{
    switch (e->token) {
        case LTL_AND: case LTL_OR: case LTL_EQUIV: case LTL_IMPLY:
        case LTL_RELEASE: case LTL_WEAK_UNTIL:
        case LTL_STRONG_RELEASE: case LTL_UNTIL: {
            data_format_t l = check_type_format_LTL(e->arg1, env, lts_type);
            data_format_t r = check_type_format_LTL(e->arg2, env, lts_type);
            return get_data_format_binary(BOOL_OPS, e, env, l, r);
        }            
        case LTL_NOT: case LTL_FUTURE: case LTL_GLOBALLY: case LTL_NEXT: {
            data_format_t c = check_type_format_LTL(e->arg1, env, lts_type);
            return get_data_format_unary(UNARY_BOOL_OPS, e, env, c);
        }
        default: {
            return check_type_format_atom(e, env, lts_type);
        }
    }        
}

data_format_t
check_type_format_CTL(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type)
{
    switch (e->token) {
        case CTL_AND: case CTL_OR: case CTL_EQUIV: case CTL_IMPLY:
        case CTL_UNTIL: {
            data_format_t l = check_type_format_CTL(e->arg1, env, lts_type);
            data_format_t r = check_type_format_CTL(e->arg2, env, lts_type);
            return get_data_format_binary(BOOL_OPS, e, env, l, r);
        }
        case CTL_NOT: case CTL_NEXT: case CTL_FUTURE:
        case CTL_GLOBALLY: case CTL_EXIST: case CTL_ALL: {
            data_format_t c = check_type_format_CTL(e->arg1, env, lts_type);
            return get_data_format_unary(UNARY_BOOL_OPS, e, env, c);
        }
        default: {
            return check_type_format_atom(e, env, lts_type);
        }
    }
}

data_format_t
check_type_format_MU(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type)
{
    switch (e->token) {
        case MU_AND: case MU_OR: {
            data_format_t l = check_type_format_MU(e->arg1, env, lts_type);
            data_format_t r = check_type_format_MU(e->arg2, env, lts_type);
            return get_data_format_binary(BOOL_OPS, e, env, l, r);
        }
        case MU_NOT: {
            data_format_t c = check_type_format_MU(e->arg1, env, lts_type);
            return get_data_format_unary(UNARY_BOOL_OPS, e, env, c);
        }                
        // Todo: not to sure about these cases below
        case MU_VAR:
        case MU_EDGE_EXIST: case MU_EDGE_ALL: case MU_EDGE_EXIST_LEFT:
        case MU_EDGE_EXIST_RIGHT: case MU_EDGE_ALL_LEFT: case MU_EDGE_ALL_RIGHT:
        case MU_MU: case MU_NU: case MU_NEXT: case MU_EXIST: case MU_ALL: {
            return LTStypeBool;
        }
        default: {
            return check_type_format_atom(e, env, lts_type);
        }
    }
}
