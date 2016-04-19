#include <hre/config.h>

#include <hre/stringindex.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-grammar.h>
#include <ltsmin-lib/ltsmin-parse-env.h> // required for ltsmin-lexer.h!
#include <ltsmin-lib/ltsmin-lexer.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <util-lib/chunk_support.h>
#include <util-lib/dynamic-array.h>

const char *
S_NAME(ltsmin_expr_case s)
{
    switch (s) {
    case S_TRUE:             return "true";
    case S_FALSE:            return "false";
    case S_NOT:              return "!";
    case S_LT:               return "<";
    case S_LEQ:              return "<=";
    case S_GT:               return ">";
    case S_GEQ:              return ">=";
    case S_OR:               return "||";
    case S_AND:              return "&&";
    case S_EQ:               return "==";
    case S_NEQ:              return "!=";
    case S_EQUIV:            return "<->";
    case S_IMPLY:            return "->";
    case S_MULT:             return "*";
    case S_DIV:              return "/";
    case S_REM:              return "%";
    case S_ADD:              return "+";
    case S_SUB:              return "-";
    default:        Abort ("Not a keyword/operator, token id: %d", s);
    }
}

const char *
PRED_NAME(Pred pred)
{
    return S_NAME((ltsmin_expr_case) pred);
}

const char *
LTL_NAME(LTL ltl)
{
    switch (ltl) {
    case LTL_FUTURE:            return "<>";
    case LTL_GLOBALLY:          return "[]";
    case LTL_RELEASE:           return "R";
    case LTL_WEAK_UNTIL:        return "W";
    case LTL_STRONG_RELEASE:    Abort ("Strong release isn't implemented!");
    case LTL_NEXT:              return "X";
    case LTL_UNTIL:             return "U";
    default:                    return PRED_NAME((Pred)ltl);
    }
}

const char *
CTL_NAME(CTL ctl)
{
    switch (ctl) {
    case CTL_NEXT:          return "X";
    case CTL_UNTIL:         return "U";
    case CTL_FUTURE:        return "<>";
    case CTL_GLOBALLY:      return "[]";
    case CTL_EXIST:         return "E";
    case CTL_ALL:           return "A";
    default:                return PRED_NAME((Pred)ctl);
    }
}

const char *
MU_NAME(MU mu)
{
    switch (mu) {
    case MU_EDGE_EXIST:         Abort ("Unimplemented: MU_EDGE_EXIST");
    case MU_EDGE_ALL:           Abort ("Unimplemented: MU_EDGE_ALL");
    case MU_EDGE_EXIST_LEFT:    return "<";
    case MU_EDGE_EXIST_RIGHT:   return ">";
    case MU_EDGE_ALL_LEFT:      return "[";
    case MU_EDGE_ALL_RIGHT:     return "]";
    case MU_MU:                 return "mu";
    case MU_NU:                 return "nu";
    case MU_NEXT:               return "X";
    case MU_EXIST:              return "E";
    case MU_ALL:                return "A";
    default:                    return PRED_NAME((Pred)mu);
    }
}

static stream_t
read_formula (const char *file)
{
    FILE *in=fopen( file, "r" );
    size_t *used = RTmalloc(sizeof(size_t));
    if (in) {
        return stream_input(in);
    } else {
        return stream_read_mem((void*)file, strlen(file), used);
    }
}

static void
fill_env (ltsmin_parse_env_t env, lts_type_t ltstype)
{
    int N = lts_type_get_state_length(ltstype);
    for (int i = 0; i < N; i++) {
        char*name=lts_type_get_state_name(ltstype,i);
        HREassert (name);
        int idx = LTSminStateVarIndex(env,name);
        HREassert (i == idx, "Model has equally named state variables ('%s') at index %d and %d", name, i, idx);
    }

    int L = lts_type_get_state_label_count(ltstype);
    for (int i = 0; i < L; i++) {
        char*name=lts_type_get_state_label_name(ltstype,i);
        HREassert (name);
        // consider state label an state variable with idx >= N
        int idx = LTSminStateVarIndex(env,name);
        HREassert (N + i == idx, "Model has equally named state labels ('%s') at index %d and %d", name, i - N, idx);
    }

    int E=lts_type_get_edge_label_count(ltstype);
    for (int i = 0; i < E; i++) {
        char*name=lts_type_get_edge_label_name(ltstype,i);
        HREassert (name);
        int idx = LTSminEdgeVarIndex(env,name);
        HREassert (i == idx);
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
    HREassert(name != NULL);
    
    if (strcmp(name, type) != 0) {
        const char* ex = LTSminPrintExpr(e, env);
        Abort("Expression, with type \"%s\" is not of required type \"%s\": \"%s\"", name, type, ex);
    }
}

static inline void
type_check_require_format(lts_type_t lts_type, int type, const data_format_t required[], int n, ltsmin_expr_t e, ltsmin_parse_env_t env, const char* msg)
{
    /* chunks for numeric types could be implemented with the bignum interface. */

    const data_format_t format = lts_type_get_format(lts_type, type);
    HREassert(type >= 0 && type < lts_type_get_type_count(lts_type));

    for (int i = 0; i < n; i++) {
        if (format == required[i]) return;
    }
    const char* ex = LTSminPrintExpr(e, env);
    Abort("Only %s type formats are supported: \"%s\"", msg, ex);
}

static inline void
type_check_equal_format(lts_type_t lts_type, int left, int right, ltsmin_expr_t e, ltsmin_parse_env_t env)
{
    if (lts_type_get_format(lts_type, left) != lts_type_get_format(lts_type, right)) {
        const char* ex = LTSminPrintExpr(e, env);
        Abort("LHS and RHS do not have the same type format: \"%s\"", ex);
    }
}

static inline void
type_check_equal_type(lts_type_t lts_type, int left, int right, ltsmin_expr_t e, ltsmin_parse_env_t env)
{
    if (left != right) {
        const char* ex = LTSminPrintExpr(e, env);
        Abort("LHS (%s) and RHS (%s) are not of the same type: \"%s\"",
                lts_type_get_type(lts_type, left),
                lts_type_get_type(lts_type, right),
                ex);
    }
}

int
ltsmin_expr_type_check(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type)
{
    switch (e->token) {
        case SVAR: {
            const int N = lts_type_get_state_length(lts_type);
            if (e->idx < N) { // state variable
                return lts_type_get_state_typeno(lts_type, e->idx);
            } else {
                return lts_type_get_state_label_typeno(lts_type, e->idx - N);
            }
        }
        case EVAR: {
            const ltsmin_expr_t other = LTSminExprSibling(e);
            if (other->token != CHUNK) {
                LTSminLogExpr (error, "An edge should be paired with a chunk: ", e, env);
                HREabort (LTSMIN_EXIT_FAILURE);
            }
            return lts_type_get_edge_label_typeno(lts_type, e->idx);
        }
        case INT:
            return type_check_get_type(lts_type, LTSMIN_TYPE_NUMERIC, e, env);
        case CHUNK: {
            const ltsmin_expr_t other = LTSminExprSibling(e);
            if (other == NULL) {
                LTSminLogExpr(error, "Unbound chunk: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            return ltsmin_expr_type_check(other, env, lts_type);
        }
        case S_FALSE: case S_TRUE:
            return type_check_get_type(lts_type, LTSMIN_TYPE_BOOL, e, env);
        case VAR:
            return -1;
        case S_LT: case S_LEQ: case S_GT: case S_GEQ: {
            const int left = ltsmin_expr_type_check(e->arg1, env, lts_type);
            type_check_require_type(lts_type, left, LTSMIN_TYPE_NUMERIC, e->arg1, env);
            const int right = ltsmin_expr_type_check(e->arg2, env, lts_type);
            type_check_require_type(lts_type, right, LTSMIN_TYPE_NUMERIC, e->arg2, env);

            type_check_equal_format(lts_type, left, right, e, env);

            return type_check_get_type(lts_type, LTSMIN_TYPE_BOOL, e, env);
        }
        case S_EQ: case S_NEQ: {
            const int left = ltsmin_expr_type_check(e->arg1, env, lts_type);
            const int right = ltsmin_expr_type_check(e->arg2, env, lts_type);

            type_check_equal_type(lts_type, left, right, e, env);
            type_check_equal_format(lts_type, left, right, e, env);

            return type_check_get_type(lts_type, LTSMIN_TYPE_BOOL, e, env);
        }
        case S_OR: case S_AND: case S_EQUIV: case S_IMPLY: {
            const int left = ltsmin_expr_type_check(e->arg1, env, lts_type);
            const int right = ltsmin_expr_type_check(e->arg2, env, lts_type);

            type_check_require_type(lts_type, left, LTSMIN_TYPE_BOOL, e->arg1, env);
            type_check_require_type(lts_type, right, LTSMIN_TYPE_BOOL, e->arg2, env);

            type_check_equal_format(lts_type, left, right, e, env);

            return left;
        }
        case S_MULT: case S_DIV: case S_REM: case S_ADD: case S_SUB: {
            const int left = ltsmin_expr_type_check(e->arg1, env, lts_type);
            const int right = ltsmin_expr_type_check(e->arg2, env, lts_type);
            
            type_check_require_type(lts_type, left, LTSMIN_TYPE_NUMERIC, e->arg1, env);
            type_check_require_type(lts_type, right, LTSMIN_TYPE_NUMERIC, e->arg2, env);

            type_check_equal_format(lts_type, left, right, e, env);

            return left;
        }
        case S_NOT: case MU_FIX: case NU_FIX: case EXISTS_STEP: case FORALL_STEP: case EDGE_EXIST: case EDGE_ALL: {
            const int child = ltsmin_expr_type_check(e->arg1, env, lts_type);

            type_check_require_type(lts_type, child, LTSMIN_TYPE_BOOL, e->arg1, env);

            return child;
        }
        default: {
            switch (e->node_type) {
                case BINARY_OP: {
                    const int left = ltsmin_expr_type_check(e->arg1, env, lts_type);
                    const int right = ltsmin_expr_type_check(e->arg2, env, lts_type);

                    type_check_equal_type(lts_type, left, right, e, env);
                    type_check_equal_format(lts_type, left, right, e, env);

                    return left;
                }
                case UNARY_OP: {
                    const int child = ltsmin_expr_type_check(e->arg1, env, lts_type);
                    
                    return child;
                }
                default: {
                    LTSminLogExpr(error, "Unsupported expression: ", e, env);
                    HREabort(LTSMIN_EXIT_FAILURE);
                }
            }
        }
    }
}

ltsmin_expr_t
ltsmin_expr_optimize(ltsmin_expr_t e, const ltsmin_parse_env_t env)
{
    ltsmin_expr_t left, right;
    left = right = NULL;

    switch (e->node_type) {
        case BINARY_OP: {

            left = ltsmin_expr_optimize(e->arg1, env);
            right = ltsmin_expr_optimize(e->arg2, env);

            switch (e->token) {
                case S_AND: {
                    if (left->token == S_FALSE) { // false /\ a is false
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        return left;
                    } else if (right->token == S_FALSE) { // a /\ false is false
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        return right;
                    } else if (left->token == S_TRUE) { // true /\ a is a
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        return right;
                    } else if (right->token == S_TRUE) { // a /\ true is a
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        return left;
                    } else if (LTSminExprEq(left, right)) { // a /\ a is a
                        LTSminExprDestroy(e, 0);
                        LTSminExprDestroy(right, 1);
                        return left;
                    }
                    break;
                }
                case S_OR: {
                    if (left->token == S_TRUE) { // true \/ a is true
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        return left;
                    } else if (right->token == S_TRUE) { // a \/ true is true
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        return right;
                    } else if (left->token == S_FALSE) { // false \/ a is a
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        return right;
                    } else if (right->token == S_FALSE) { // a \/ false is a
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        return left;
                    } else if (LTSminExprEq(left, right)) { // a \/ a is a
                        LTSminExprDestroy(e, 0);
                        LTSminExprDestroy(right, 1);
                        return left;
                    }
                    break;
                }
                case S_EQ: case S_EQUIV: {
                    if (LTSminExprEq(left, right)) { // a {<->, ==} a is true
                        LTSminExprDestroy(e, 1);
                        return LTSminExpr(CONSTANT, S_TRUE, LTSminConstantIdx(env, S_NAME(S_TRUE)), 0, 0);
                    } else if (left->token == S_TRUE) { // true {<->,==} a is a
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        return right;
                    } else if (right->token == S_TRUE) { // a {<->,==} true is a
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        return left;
                    } else if (left->token == S_FALSE) { // false {<->,==} a is !a, !a can be optimized
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        e = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), right, 0);
                        return ltsmin_expr_optimize(e, env);
                    } else if (right->token == S_FALSE) { // a {<->,==} false is !a, !a can be optimized
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        e = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), left, 0);
                        return ltsmin_expr_optimize(e, env);
                    }
                    break;
                }
                case S_NEQ: {
                    if (LTSminExprEq(left, right)) { // a != a is false
                        LTSminExprDestroy(e, 1);
                        return LTSminExpr(CONSTANT, S_FALSE,  LTSminConstantIdx(env, S_NAME(S_FALSE)), 0, 0);
                    } else if (left->token == S_FALSE) { // false != a is a
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        return right;
                    } else if (right->token == S_FALSE) { // a != false is a
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        return left;
                    } else if (left->token == S_TRUE) { // true != a is !a, !a can be optimized
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        e = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), right, 0);
                        return ltsmin_expr_optimize(e, env);
                    } else if (right->token == S_TRUE) { // a != true is !a, !a can be optimized
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        e = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), left, 0);
                        return ltsmin_expr_optimize(e, env);
                    }
                    break;
                }
                case S_IMPLY: {
                    if (LTSminExprEq(left, right)) { // a {->} a is true
                        LTSminExprDestroy(e, 1);
                        return LTSminExpr(CONSTANT, S_TRUE, LTSminConstantIdx(env, S_NAME(S_TRUE)), 0, 0);
                    } else if (left->token == S_FALSE || right->token == S_TRUE) { // false -> a is true, a -> true is true
                        LTSminExprDestroy(e, 1);
                        return LTSminExpr(CONSTANT, S_TRUE, LTSminConstantIdx(env, S_NAME(S_TRUE)), 0, 0);
                    } else if (left->token == S_TRUE) { // true -> a is a
                        LTSminExprDestroy(left, 1);
                        LTSminExprDestroy(e, 0);
                        return right;
                    } else if (right->token == S_FALSE) { // a -> false is !a, !a can be optimized
                        LTSminExprDestroy(right, 1);
                        LTSminExprDestroy(e, 0);
                        e = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), left, 0);
                        return ltsmin_expr_optimize(e, env);
                    }
                    break;
                }
                case S_LEQ: case S_GEQ: {
                    if (LTSminExprEq(left, right)) { // a {<=, >=} a is true
                        LTSminExprDestroy(e, 1);
                        return LTSminExpr(CONSTANT, S_TRUE, LTSminConstantIdx(env, S_NAME(S_TRUE)), 0, 0);
                    }
                    break;
                }
                case S_LT: case S_GT: {
                    if (LTSminExprEq(left, right)) { // a {<, >} a is false
                        LTSminExprDestroy(e, 1);
                        return LTSminExpr(CONSTANT, S_FALSE,  LTSminConstantIdx(env, S_NAME(S_FALSE)), 0, 0);
                    }
                    break;
                }
                case S_REM: {
                    if (right->idx == 0) {
                        LTSminLogExpr (error, "division by zero in: ", e, env);
                        HREabort(LTSMIN_EXIT_FAILURE);
                    }
                }
                case S_SUB: {
                    if (LTSminExprEq(left, right)) { // a {-, %} a is 0
                        LTSminExprDestroy(e, 1);
                        return LTSminExpr(INT, INT, 0, 0, 0);
                    }
                    break;
                }
                case S_DIV: {
                    if (right->idx == 0) {
                        LTSminLogExpr (error, "division by zero in: ", e, env);
                        HREabort(LTSMIN_EXIT_FAILURE);
                    }
                    if (LTSminExprEq(left, right)) { // a / a is 1
                        LTSminExprDestroy(e, 1);
                        return LTSminExpr(INT, INT, 1, 0, 0);
                    }
                    break;
                }
                default: {
                    break;
                }
            }

            if (e->arg1 != left || e->arg2 != right) {
                e->arg1 = left; left->parent = e;
                e->arg2 = right; right->parent = e;
                LTSminExprRehash(e);
            }
            return e;
        }
        case UNARY_OP: {
            ltsmin_expr_t n = e;
            left = e->arg1;
            switch (e->token) {
                case S_NOT: {
                    switch (left->token) {
                        case S_LT: { // !(a < b) is a >= b
                            LTSminExprDestroy(e, 0);
                            n = LTSminExpr(BINARY_OP, S_GEQ, LTSminBinaryIdx(env, S_NAME(S_GEQ)), left->arg1, left->arg2);
                            break;
                        }
                        case S_LEQ: { // !(a <= b) is a > b
                            LTSminExprDestroy(e, 0);
                            n = LTSminExpr(BINARY_OP, S_GT, LTSminBinaryIdx(env, S_NAME(S_GT)), left->arg1, left->arg2);
                            break;
                        }
                        case S_GT: { // !(a > b) is a <= b
                            LTSminExprDestroy(e, 0);
                            n = LTSminExpr(BINARY_OP, S_LEQ, LTSminBinaryIdx(env, S_NAME(S_LEQ)), left->arg1, left->arg2);
                            break;
                        }
                        case S_GEQ: { // !(a >= b) is a < b
                            LTSminExprDestroy(e, 0);
                            n = LTSminExpr(BINARY_OP, S_LT, LTSminBinaryIdx(env, S_NAME(S_LT)), left->arg1, left->arg2);
                            break;
                        }
                        case S_EQ: { // !(a == b) is a != b
                            LTSminExprDestroy(e, 0);
                            n = LTSminExpr(BINARY_OP, S_NEQ, LTSminBinaryIdx(env, S_NAME(S_NEQ)), left->arg1, left->arg2);
                            break;
                        }
                        case S_NEQ: { // !(a != b) is a == b
                            LTSminExprDestroy(e, 0);
                            n = LTSminExpr(BINARY_OP, S_EQ, LTSminBinaryIdx(env, S_NAME(S_EQ)), left->arg1, left->arg2);
                            break;
                        }
                        case S_TRUE: { // !true is false
                            LTSminExprDestroy(e, 1);
                            n = LTSminExpr(CONSTANT, S_FALSE, LTSminConstantIdx(env, S_NAME(S_FALSE)), 0, 0);
                            break;
                        }
                        case S_FALSE: { // !false is true
                            LTSminExprDestroy(e, 1);
                            n = LTSminExpr(CONSTANT, S_TRUE, LTSminConstantIdx(env, S_NAME(S_TRUE)), 0, 0);
                            break;
                        }
                        case S_NOT: { // !!a is a
                            LTSminExprDestroy(left, 0);
                            LTSminExprDestroy(e, 0);
                            n = left->arg1;
                            break;
                        }
                        case S_OR: { // !(a || b) is !a && !b
                            LTSminExprDestroy(e, 0);
                            ltsmin_expr_t child = left;
                            left = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), child->arg1, 0);
                            right = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), child->arg2, 0);
                            n = LTSminExpr(BINARY_OP, S_AND, LTSminBinaryIdx(env, S_NAME(S_AND)), left, right);
                            break;
                        }
                        case S_AND: { // !(a && b) is !a || !b
                            LTSminExprDestroy(e, 0);
                            ltsmin_expr_t child = left;
                            left = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), child->arg1, 0);
                            right = LTSminExpr(UNARY_OP, S_NOT, LTSminUnaryIdx(env, S_NAME(S_NOT)), child->arg2, 0);
                            n = LTSminExpr(BINARY_OP, S_OR,  LTSminBinaryIdx(env, S_NAME(S_OR)), left, right);
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                    break;
                }
                default: {
                    break;
                }
            }

            if (e != n) {
                n->arg1->parent = n;
                LTSminExprRehash(n);
                return ltsmin_expr_optimize(n, env);
            } else {
                e->arg1 = ltsmin_expr_optimize(e->arg1, env);
                if (e->arg1 != n->arg1) {
                    e->arg1->parent = e;
                    LTSminExprRehash(e);
                }
                return e;
            }
            break;
        }
        default: {
            break;
        }
    }
    return e;
}

ltsmin_expr_t
ltl_optimize(ltsmin_expr_t e, ltsmin_parse_env_t env)
{
    switch (e->token) {
        case LTL_WEAK_UNTIL: {
            ltsmin_expr_t u = LTSminExpr(BINARY_OP, LTL_UNTIL, LTSminBinaryIdx(env, LTL_NAME(LTL_UNTIL)), e->arg1, e->arg2);
            ltsmin_expr_t g = LTSminExpr(UNARY_OP, LTL_GLOBALLY, LTSminUnaryIdx(env, LTL_NAME(LTL_GLOBALLY)), e->arg1, NULL);
            LTSminExprDestroy(e, 0);
            return LTSminExpr(BINARY_OP, LTL_OR, LTSminBinaryIdx(env, LTL_NAME(LTL_OR)), u, g);
        }
    }

    return e;
}

static void
create_pred_env(ltsmin_parse_env_t env)
{
    LTSminConstant      (env, PRED_FALSE,  PRED_NAME(PRED_FALSE));
    LTSminConstant      (env, PRED_TRUE,   PRED_NAME(PRED_TRUE));

    LTSminBinaryOperator(env, PRED_MULT,   PRED_NAME(PRED_MULT), 1);
    LTSminBinaryOperator(env, PRED_DIV,    PRED_NAME(PRED_DIV), 1);
    LTSminBinaryOperator(env, PRED_REM,    PRED_NAME(PRED_REM), 1);
    LTSminBinaryOperator(env, PRED_ADD,    PRED_NAME(PRED_ADD), 2);
    LTSminBinaryOperator(env, PRED_SUB,    PRED_NAME(PRED_SUB), 2);

    LTSminBinaryOperator(env, PRED_LT,     PRED_NAME(PRED_LT), 3);
    LTSminBinaryOperator(env, PRED_LEQ,    PRED_NAME(PRED_LEQ), 3);
    LTSminBinaryOperator(env, PRED_GT,     PRED_NAME(PRED_GT), 3);
    LTSminBinaryOperator(env, PRED_GEQ,    PRED_NAME(PRED_GEQ), 3);

    LTSminBinaryOperator(env, PRED_EQ,     PRED_NAME(PRED_EQ), 4);
    LTSminBinaryOperator(env, PRED_NEQ,    PRED_NAME(PRED_NEQ), 4);

    LTSminPrefixOperator(env, PRED_NOT,    PRED_NAME(PRED_NOT), 5);

    LTSminBinaryOperator(env, PRED_AND,    PRED_NAME(PRED_AND), 6);
    LTSminBinaryOperator(env, PRED_OR,     PRED_NAME(PRED_OR), 7);

    LTSminBinaryOperator(env, PRED_EQUIV,  PRED_NAME(PRED_EQUIV), 8);
    LTSminBinaryOperator(env, PRED_IMPLY,  PRED_NAME(PRED_IMPLY), 9);
}

ltsmin_expr_t
pred_parse_file(const char *file, ltsmin_parse_env_t env, lts_type_t lts_type)
{
    stream_t stream = read_formula (file);

    fill_env (env, lts_type);

    create_pred_env(env);

    ltsmin_parse_stream(TOKEN_EXPR,env,stream);

    const int type = ltsmin_expr_type_check(env->expr, env, lts_type);

    type_check_require_type(lts_type, type, LTSMIN_TYPE_BOOL, env->expr, env);
    
    return ltsmin_expr_optimize(env->expr, env);
}

static void
create_ltl_env(ltsmin_parse_env_t env)
{
    LTSminConstant      (env, LTL_FALSE,        LTL_NAME(LTL_FALSE));
    LTSminConstant      (env, LTL_TRUE,         LTL_NAME(LTL_TRUE));

    LTSminBinaryOperator(env, LTL_MULT,         LTL_NAME(LTL_MULT), 1);
    LTSminBinaryOperator(env, LTL_DIV,          LTL_NAME(LTL_DIV), 1);
    LTSminBinaryOperator(env, LTL_REM,          LTL_NAME(LTL_REM), 1);
    LTSminBinaryOperator(env, LTL_ADD,          LTL_NAME(LTL_ADD), 2);
    LTSminBinaryOperator(env, LTL_SUB,          LTL_NAME(LTL_SUB), 2);

    LTSminBinaryOperator(env, LTL_LT,           LTL_NAME(LTL_LT), 3);
    LTSminBinaryOperator(env, LTL_LEQ,          LTL_NAME(LTL_LEQ), 3);
    LTSminBinaryOperator(env, LTL_GT,           LTL_NAME(LTL_GT), 3);
    LTSminBinaryOperator(env, LTL_GEQ,          LTL_NAME(LTL_GEQ), 3);

    LTSminBinaryOperator(env, LTL_EQ,           LTL_NAME(LTL_EQ), 4);
    LTSminBinaryOperator(env, LTL_NEQ,          LTL_NAME(LTL_NEQ), 4);
    LTSminPrefixOperator(env, LTL_NOT,          LTL_NAME(LTL_NOT), 5);

    LTSminPrefixOperator(env, LTL_GLOBALLY,     LTL_NAME(LTL_GLOBALLY), 6);
    LTSminPrefixOperator(env, LTL_FUTURE,       LTL_NAME(LTL_FUTURE), 6);
    LTSminPrefixOperator(env, LTL_NEXT,         LTL_NAME(LTL_NEXT), 6);

    LTSminBinaryOperator(env, LTL_AND,          LTL_NAME(LTL_AND), 7);
    LTSminBinaryOperator(env, LTL_OR,           LTL_NAME(LTL_OR), 8);

    LTSminBinaryOperator(env, LTL_EQUIV,        LTL_NAME(LTL_EQUIV), 9);
    LTSminBinaryOperator(env, LTL_IMPLY,        LTL_NAME(LTL_IMPLY), 10);

    LTSminBinaryOperator(env, LTL_UNTIL,        LTL_NAME(LTL_UNTIL), 11);
    LTSminBinaryOperator(env, LTL_WEAK_UNTIL,   LTL_NAME(LTL_WEAK_UNTIL), 11); // translated to U \/ []
    LTSminBinaryOperator(env, LTL_RELEASE,      LTL_NAME(LTL_RELEASE), 11);
}

/* Parser Priorities:
 * HIGH
 *     binary priority 1          : *, /, %
 *     binary priority 2          : +, -
 *     binary priority 3          : <, <=, >, >=
 *     binary priority 4          : ==, !=, etc
 *     prefix priority 5          : !
 *     prefix priority 6          : G,F,X
 *     binary priority 7          : &
 *     binary priority 8          : |
 *     binary priority 9          : <->
 *     binary priority 10         : ->
 *     binary priority 11         : U,R
 * LOW
 */
ltsmin_expr_t
ltl_parse_file(const char *file, ltsmin_parse_env_t env, lts_type_t ltstype)
{
    stream_t stream = read_formula (file);

    fill_env (env, ltstype);

    create_ltl_env(env);

    ltsmin_parse_stream(TOKEN_EXPR,env,stream);

    const int type = ltsmin_expr_type_check(env->expr, env, ltstype);

    type_check_require_type(ltstype, type, LTSMIN_TYPE_BOOL, env->expr, env);

    return ltsmin_expr_optimize(env->expr, env);
}

static void
create_ctl_env(ltsmin_parse_env_t env)
{
    LTSminConstant      (env, CTL_FALSE,        CTL_NAME(CTL_FALSE));
    LTSminConstant      (env, CTL_TRUE,         CTL_NAME(CTL_TRUE));
    
    LTSminBinaryOperator(env, CTL_MULT,         CTL_NAME(CTL_MULT), 1);
    LTSminBinaryOperator(env, CTL_DIV,          CTL_NAME(CTL_DIV), 1);
    LTSminBinaryOperator(env, CTL_REM,          CTL_NAME(CTL_REM), 1);
    LTSminBinaryOperator(env, CTL_ADD,          CTL_NAME(CTL_ADD), 2);
    LTSminBinaryOperator(env, CTL_SUB,          CTL_NAME(CTL_SUB), 2);

    LTSminBinaryOperator(env, CTL_LT,           CTL_NAME(CTL_LT), 3);
    LTSminBinaryOperator(env, CTL_LEQ,          CTL_NAME(CTL_LEQ), 3);
    LTSminBinaryOperator(env, CTL_GT,           CTL_NAME(CTL_GT), 3);
    LTSminBinaryOperator(env, CTL_GEQ,          CTL_NAME(CTL_GEQ), 3);

    LTSminBinaryOperator(env, CTL_EQ,           CTL_NAME(CTL_EQ), 4);
    LTSminBinaryOperator(env, CTL_NEQ,          CTL_NAME(CTL_NEQ), 4);
    LTSminPrefixOperator(env, CTL_NOT,          CTL_NAME(CTL_NOT), 5);

    LTSminPrefixOperator(env, CTL_EXIST,        CTL_NAME(CTL_EXIST), 6);
    LTSminPrefixOperator(env, CTL_ALL,          CTL_NAME(CTL_ALL), 6);

    LTSminPrefixOperator(env, CTL_GLOBALLY,     CTL_NAME(CTL_GLOBALLY), 6);
    LTSminPrefixOperator(env, CTL_FUTURE,       CTL_NAME(CTL_FUTURE), 6);
    LTSminPrefixOperator(env, CTL_NEXT,         CTL_NAME(CTL_NEXT), 6);

    LTSminBinaryOperator(env, CTL_AND,          CTL_NAME(CTL_AND), 7);
    LTSminBinaryOperator(env, CTL_OR,           CTL_NAME(CTL_OR), 8);

    LTSminBinaryOperator(env, CTL_EQUIV,        CTL_NAME(CTL_EQUIV), 9);
    LTSminBinaryOperator(env, CTL_IMPLY,        CTL_NAME(CTL_IMPLY), 10);

    LTSminBinaryOperator(env, CTL_UNTIL,        CTL_NAME(CTL_UNTIL), 11);
}

ltsmin_expr_t
ctl_optimize(ltsmin_expr_t e, ltsmin_parse_env_t env)
{
    if (e->node_type == UNARY_OP && e->token == CTL_NOT) {
        if (e->arg1->token == CTL_EXIST) {
            if (e->arg1->arg1->token == CTL_NEXT) { // !EX p is AX !p, !p can be optimized
                e->arg1->arg1->arg1 = LTSminExpr(UNARY_OP, CTL_NOT, LTSminUnaryIdx(env, CTL_NAME(CTL_NOT)), e->arg1->arg1->arg1, NULL);
                e->arg1->arg1->arg1 = ltsmin_expr_optimize(e->arg1->arg1->arg1, env);
                LTSminExprRehash(e->arg1->arg1->arg1);
                e->arg1->arg1->arg1->parent = e->arg1->arg1;
                e->arg1->token = CTL_ALL;
                e->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_ALL));
                LTSminExprRehash(e->arg1);
                LTSminExprDestroy(e, 0);
                return e->arg1;
            } else if (e->arg1->arg1->token == CTL_FUTURE) { // !EF p is AG !p, !p can be optimized
                e->arg1->arg1->arg1 = LTSminExpr(UNARY_OP, CTL_NOT, LTSminUnaryIdx(env, CTL_NAME(CTL_NOT)), e->arg1->arg1->arg1, NULL);
                e->arg1->arg1->arg1 = ltsmin_expr_optimize(e->arg1->arg1->arg1, env);
                LTSminExprRehash(e->arg1->arg1->arg1);
                e->arg1->arg1->arg1->parent = e->arg1->arg1;
                e->arg1->arg1->token = CTL_GLOBALLY;
                e->arg1->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_GLOBALLY));
                LTSminExprRehash(e->arg1->arg1);
                e->arg1->token = CTL_ALL;
                e->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_ALL));
                LTSminExprRehash(e->arg1);
                LTSminExprDestroy(e, 0);
                return e->arg1;
            } else if (e->arg1->arg1->token == CTL_GLOBALLY) { // !EG p is AF !p, !p can be optimized
                e->arg1->arg1->arg1 = LTSminExpr(UNARY_OP, CTL_NOT, LTSminUnaryIdx(env, CTL_NAME(CTL_NOT)), e->arg1->arg1->arg1, NULL);
                e->arg1->arg1->arg1 = ltsmin_expr_optimize(e->arg1->arg1->arg1, env);
                LTSminExprRehash(e->arg1->arg1->arg1);
                e->arg1->arg1->arg1->parent = e->arg1->arg1;
                e->arg1->arg1->token = CTL_FUTURE;
                e->arg1->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_FUTURE));
                LTSminExprRehash(e->arg1->arg1);
                e->arg1->token = CTL_ALL;
                e->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_ALL));
                LTSminExprRehash(e->arg1);
                LTSminExprDestroy(e, 0);
                return e->arg1;
            }
        } else if (e->arg1->token == CTL_ALL) {
            if (e->arg1->arg1->token == CTL_NEXT) { // !AX p is EX !p, !p can be optimized
                e->arg1->arg1->arg1 = LTSminExpr(UNARY_OP, CTL_NOT, LTSminUnaryIdx(env, CTL_NAME(CTL_NOT)), e->arg1->arg1->arg1, NULL);
                e->arg1->arg1->arg1 = ltsmin_expr_optimize(e->arg1->arg1->arg1, env);
                LTSminExprRehash(e->arg1->arg1->arg1);
                e->arg1->arg1->arg1->parent = e->arg1->arg1;
                e->arg1->token = CTL_EXIST;
                e->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_EXIST));
                LTSminExprRehash(e->arg1);
                LTSminExprDestroy(e, 0);
                return e->arg1;
            } else if (e->arg1->arg1->token == CTL_FUTURE) { // !AF p is EG !p, !p can be optimized
                e->arg1->arg1->arg1 = LTSminExpr(UNARY_OP, CTL_NOT, LTSminUnaryIdx(env, CTL_NAME(CTL_NOT)), e->arg1->arg1->arg1, NULL);
                e->arg1->arg1->arg1 = ltsmin_expr_optimize(e->arg1->arg1->arg1, env);
                LTSminExprRehash(e->arg1->arg1->arg1);
                e->arg1->arg1->arg1->parent = e->arg1->arg1;
                e->arg1->arg1->token = CTL_GLOBALLY;
                e->arg1->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_GLOBALLY));
                LTSminExprRehash(e->arg1->arg1);
                e->arg1->token = CTL_EXIST;
                e->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_EXIST));
                LTSminExprRehash(e->arg1);
                LTSminExprDestroy(e, 0);
                return e->arg1;
            } else if (e->arg1->arg1->token == CTL_GLOBALLY) { // !AG p is EF !p, !p can be optimized
                e->arg1->arg1->arg1 = LTSminExpr(UNARY_OP, CTL_NOT, LTSminUnaryIdx(env, CTL_NAME(CTL_NOT)), e->arg1->arg1->arg1, NULL);
                e->arg1->arg1->arg1 = ltsmin_expr_optimize(e->arg1->arg1->arg1, env);
                LTSminExprRehash(e->arg1->arg1->arg1);
                e->arg1->arg1->arg1->parent = e->arg1->arg1;
                e->arg1->arg1->token = CTL_FUTURE;
                e->arg1->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_FUTURE));
                LTSminExprRehash(e->arg1->arg1);
                e->arg1->token = CTL_EXIST;
                e->arg1->idx = LTSminUnaryIdx(env, CTL_NAME(CTL_EXIST));
                LTSminExprRehash(e->arg1);
                LTSminExprDestroy(e, 0);
                return e->arg1;
            }
        }
    }
    
    return e;
}

/* CTL:
 *     E. M. Clarke, E. A. Emerson and A. P. Sistla,
 *     Automatic Verification of Finite-State Concurrent Systems Using Temporal Logic Specifications,
 *     ACM Transactions on Programming Languages and Systems,
 *     vol 8-2, pages 244-263, April, 1986
 *
 * Operator percendence
 *  HIGH
 *     =, <=, >=, != (state label expression)
 *     !
 *     AG, AF, AX, EG, EF, EX
 *     &
 *     |
 *     ^ (XOR)
 *     <-> (EQUIV)
 *     -> (IMPLY)
 *     U (?AU, EU?)
 *  LOW
 */
ltsmin_expr_t
ctl_parse_file(const char *file, ltsmin_parse_env_t env, lts_type_t ltstype)
{
    stream_t stream = read_formula (file);

    fill_env (env, ltstype);

    create_ctl_env(env);

    ltsmin_parse_stream(TOKEN_EXPR,env,stream);

    const int type = ltsmin_expr_type_check(env->expr, env, ltstype);

    type_check_require_type(ltstype, type, LTSMIN_TYPE_BOOL, env->expr, env);

    return ltsmin_expr_optimize(env->expr, env);
}

static void
create_mu_env(ltsmin_parse_env_t env)
{
    LTSminConstant(env, MU_FALSE, MU_NAME(MU_FALSE));
    LTSminConstant(env, MU_TRUE, MU_NAME(MU_TRUE));

    LTSminKeyword(env, MU_MU, MU_NAME(MU_MU));
    LTSminKeyword(env, MU_NU, MU_NAME(MU_NU));
    LTSminKeyword(env, MU_EDGE_EXIST_LEFT, MU_NAME(MU_EDGE_EXIST_LEFT));
    LTSminKeyword(env, MU_EDGE_EXIST_RIGHT, MU_NAME(MU_EDGE_EXIST_RIGHT));
    LTSminKeyword(env, MU_EDGE_ALL_LEFT, MU_NAME(MU_EDGE_ALL_LEFT));
    LTSminKeyword(env, MU_EDGE_ALL_RIGHT, MU_NAME(MU_EDGE_ALL_RIGHT));

    LTSminBinaryOperator(env, MU_MULT, MU_NAME(MU_MULT), 1);
    LTSminBinaryOperator(env, MU_DIV, MU_NAME(MU_DIV), 1);
    LTSminBinaryOperator(env, MU_REM, MU_NAME(MU_REM), 1);
    LTSminBinaryOperator(env, MU_ADD, MU_NAME(MU_ADD), 2);
    LTSminBinaryOperator(env, MU_SUB, MU_NAME(MU_SUB), 2);

    LTSminBinaryOperator(env, MU_LT,  MU_NAME(MU_LT), 3);
    LTSminBinaryOperator(env, MU_LEQ, MU_NAME(MU_LEQ), 3);
    LTSminBinaryOperator(env, MU_GT,  MU_NAME(MU_GT), 3);
    LTSminBinaryOperator(env, MU_GEQ, MU_NAME(MU_GEQ), 3);

    LTSminBinaryOperator(env, MU_EQ,  MU_NAME(MU_EQ), 4);
    LTSminBinaryOperator(env, MU_NEQ, MU_NAME(MU_NEQ), 4);
    LTSminPrefixOperator(env, MU_NOT, MU_NAME(MU_NOT), 5);

    LTSminBinaryOperator(env, MU_AND, MU_NAME(MU_AND), 6);
    LTSminBinaryOperator(env, MU_OR, MU_NAME(MU_OR), 7);

    LTSminPrefixOperator(env, MU_NEXT, MU_NAME(MU_NEXT), 8);
    LTSminPrefixOperator(env, MU_EXIST, MU_NAME(MU_EXIST), 8);
    LTSminPrefixOperator(env, MU_ALL, MU_NAME(MU_ALL), 8);
}
    
/*
 * From: Modal mu-calculi, Julian Bradfield and Colin Stirling
 * For the concrete syntax, we shall assume that modal operators have higher
 * precedence than boolean, and that fixpoint operators have lowest precedence,
 * so that the scope of a fixpoint extends as far to the right as possible.
 *
 * note: when checking priorities, use ltsmin-grammer.lemon
 * for priorities, notice that prefix 1 has a higher priority than bin 1
 */
ltsmin_expr_t
mu_parse_file(const char *file, ltsmin_parse_env_t env, lts_type_t ltstype)
{
    stream_t stream = read_formula (file);

    fill_env (env, ltstype);

    create_mu_env(env);

    ltsmin_parse_stream(TOKEN_EXPR,env,stream);

    const int type = ltsmin_expr_type_check(env->expr, env, ltstype);

    type_check_require_type(ltstype, type, LTSMIN_TYPE_BOOL, env->expr, env);

    return ltsmin_expr_optimize(env->expr, env);
}


/* Translate LTL token into semantically equivalent CTL token */
ltsmin_expr_t ltl_to_ctl_star_1(ltsmin_expr_t in)
{
    ltsmin_expr_t res = RT_NEW(struct ltsmin_expr_s);
    memcpy(res, in, sizeof(struct ltsmin_expr_s));
    switch (in->token) {
        case LTL_SVAR:      res->token = CTL_SVAR;      break;
        case LTL_EVAR:      res->token = CTL_EVAR;      break;
        case LTL_NUM:       res->token = CTL_NUM;       break;
        case LTL_CHUNK:     res->token = CTL_CHUNK;     break;
        case LTL_VAR:       res->token = CTL_VAR;       break;
        case LTL_LT:        res->token = CTL_LT;        break;
        case LTL_LEQ:       res->token = CTL_LEQ;       break;
        case LTL_GT:        res->token = CTL_GT;        break;
        case LTL_GEQ:       res->token = CTL_GEQ;       break;
        case LTL_EQ:        res->token = CTL_EQ;        break;
        case LTL_NEQ:       res->token = CTL_NEQ;       break;
        case LTL_TRUE:      res->token = CTL_TRUE;      break;
        case LTL_FALSE:     res->token = CTL_FALSE;     break;
        case LTL_OR:        res->token = CTL_OR;        break;
        case LTL_AND:       res->token = CTL_AND;       break;
        case LTL_NOT:       res->token = CTL_NOT;       break;
        case LTL_MULT:      res->token = CTL_MULT;      break;
        case LTL_DIV:       res->token = CTL_DIV;       break;
        case LTL_REM:       res->token = CTL_REM;       break;
        case LTL_ADD:       res->token = CTL_ADD;       break;
        case LTL_SUB:       res->token = CTL_SUB;       break;
        case LTL_NEXT:      res->token = CTL_NEXT;      break;
        case LTL_UNTIL:     res->token = CTL_UNTIL;     break;
        case LTL_IMPLY:     res->token = CTL_IMPLY;     break;
        case LTL_EQUIV:     res->token = CTL_EQUIV;     break;
        case LTL_FUTURE:    res->token = CTL_FUTURE;    break;
        case LTL_GLOBALLY:  res->token = CTL_GLOBALLY;  break;
        //case LTL_RELEASE:   // convert..
        //case LTL_WEAK_UNTIL // convert..
        default:
            // unhandled?
            Abort("unhandled case in ltl_to_ctl_star");
    }
    // handle sub-expressions
    switch (in->node_type) {
        // UNARY
        case UNARY_OP:
            res->arg1 = ltl_to_ctl_star_1(res->arg1);
            break;
        // BINARY
        case BINARY_OP:
            res->arg1 = ltl_to_ctl_star_1(res->arg1);
            res->arg2 = ltl_to_ctl_star_1(res->arg2);
            break;
        default:
            break;
    }
    LTSminExprRehash(res);
    return res;
}

/* Translate LTL formula phi into CTL* formula A (phi) */
ltsmin_expr_t ltl_to_ctl_star(ltsmin_expr_t in)
{
    ltsmin_expr_t res = LTSminExpr(UNARY_OP, CTL_ALL, 0, ltl_to_ctl_star_1(in), NULL);
    return res;
}

ltsmin_expr_t ltl_normalize(ltsmin_expr_t in)
{
    return in;
}

/* Any CTL formula is also a CTL* formula */
ltsmin_expr_t ctl_to_ctl_star(ltsmin_expr_t in)
{
    // just return the ctl furmula
    return in;
}

ltsmin_expr_t ctl_normalize(ltsmin_expr_t in)
{
    /* AP is the set of atomic propositions. CTL is the
       smalles set of all state formulas such that
       * p \in AP is a state formula
       * if f and g are state formulas then !f and f & g are state formulas.
       * if f and g are state formulas then Xf, Ff, Gf, fUg and fRg are path
         formulas.
       * If f is a path formula, then Af and Ef are state formulas.
       *
       * The then compound operators can all be expressed in terms of
       * three operators, EX, EG and EU.
       *
       * AX f = !EX(!f)
       * EF f = E(true U f)
       * AG f = !EF(!f) = !E(true U !f)
       * AF f = !EG(!f)
       * A[f U g] = !E[!gU(!f & !g)] & !EG(!g)
       * A[f R g] = !E[!f U !g]
       * E[f R g] = !A[!f U !g]
       */
    return in;
}

MU ctl2mu_token(CTL token) { // precondition: token is not a path operator (A,E) or temporal operator (F,G,X,U,...)
      switch (token) {
        case CTL_SVAR:      return MU_SVAR;
        case CTL_EVAR:      return MU_EVAR;
        case CTL_NUM:       return MU_NUM;
        case CTL_CHUNK:     return MU_CHUNK;
        case CTL_VAR:       return MU_VAR;
        case CTL_TRUE:      return MU_TRUE;
        case CTL_FALSE:     return MU_FALSE;
        case CTL_OR:        return MU_OR;
        case CTL_AND:       return MU_AND;
        case CTL_NOT:       return MU_NOT;
        case CTL_NEXT:      return MU_NEXT;
        case CTL_ALL:       return MU_ALL;   // TODO: remove these two? potentially dangerous
        case CTL_EXIST:     return MU_EXIST;
        case CTL_LT:        return MU_LT;
        case CTL_LEQ:       return MU_LEQ;
        case CTL_GT:        return MU_GT;
        case CTL_GEQ:       return MU_GEQ;
        case CTL_EQ:        return MU_EQ;
        case CTL_NEQ:       return MU_NEQ;
        case CTL_MULT:      return MU_MULT;
        case CTL_DIV:       return MU_DIV;
        case CTL_REM:       return MU_REM;
        case CTL_ADD:       return MU_ADD;
        case CTL_SUB:       return MU_SUB;
        default:
	  Abort("ctl_to_mu cannot handle path or temporal operator: %s",CTL_NAME(token));
    }
}

static void
create_mu_var(int num, ltsmin_parse_env_t env)
{
    const char f[] = "Z%d";
    char b[snprintf(NULL, 0, f, num) + 1];
    sprintf(b, f, num);

    SIputAt(env->idents, b, num);
}

ltsmin_expr_t ctlmu(ltsmin_expr_t in, ltsmin_parse_env_t env, int free)
{   // free is the next free variable to use in mu/nu expressions
    //    return ctl_star_to_mu(in);

    // TODO provide direct recursive translation
    /* Source: formalising the translation of CTL into L_mu (Hasan Amjad)
     * T is the translation from CTL to mu
     * T(p \in AP) = p
     * T(!f) = !T(f)
     * T(f & g) = T(f) & T(g)
     * T(EX f) = <.> T(f)
     * T(EG f) = nu Q. T(f) & <.> Q
     * T(E[f U g]) = mu Q. T(g) | (T(f) & <.> Q)
     */

    const int mu_token = ctl2mu_token(in->token);

    ltsmin_expr_t res = LTSminExpr(in->node_type, mu_token, in->idx, NULL, NULL);

    switch (in->node_type) {
        case BINARY_OP: {
            res->idx = LTSminBinaryIdx(env, MU_NAME(mu_token));
            switch (in->token) {
                case CTL_UNTIL: {
                    Abort("CTL (sub)formula should not start with U");
                }
                default: { // traverse children
                    res->arg1 = ctlmu(in->arg1, env, free);
                    res->arg1->parent = res;
                    res->arg2 = ctlmu(in->arg2, env, free);
                    res->arg2->parent = res;
                    LTSminExprRehash(res);
                    return res;
                }
            }
        }
	
        case UNARY_OP: {
            res->idx = LTSminUnaryIdx(env, MU_NAME(mu_token));
            switch (in->token) {
                case CTL_NOT: { // traverse child
                    res->arg1 = ctlmu(in->arg1, env, free);
                    res->arg1->parent = res;
                    LTSminExprRehash(res);
                    return res;
                }
                case CTL_EXIST: case CTL_ALL: {
                    switch (in->arg1->token) {
                    case CTL_UNTIL: {
			// translate E p U q  into  MU Z. q \/ (p /\ E X Z)
			// translate A p U q  into  MU Z. q \/ (p /\ (A X Z /\ EX true))
                        create_mu_var(free, env);
                        ltsmin_expr_t Z = LTSminExpr(MU_VAR, MU_VAR, free, 0, 0);
                        ltsmin_expr_t X = LTSminExpr(UNARY_OP, MU_NEXT, LTSminUnaryIdx(env, MU_NAME(MU_NEXT)), Z, 0);
                        res->arg1 = X;
                        res->arg1->parent = res;
                        LTSminExprRehash(res);
			if (in->token == CTL_ALL) {
			    ltsmin_expr_t True = LTSminExpr(CONSTANT, MU_TRUE, LTSminConstantIdx(env, MU_NAME(MU_TRUE)), NULL, NULL);
			    ltsmin_expr_t XTrue = LTSminExpr(UNARY_OP, MU_NEXT, LTSminUnaryIdx(env, MU_NAME(MU_NEXT)), True, 0);
			    ltsmin_expr_t NoDeadlock = LTSminExpr(UNARY_OP, MU_EXIST, LTSminUnaryIdx(env, MU_NAME(MU_EXIST)), XTrue, 0);
			    res = LTSminExpr(BINARY_OP, MU_AND, LTSminBinaryIdx(env, MU_NAME(MU_AND)), res, NoDeadlock);
			}
                        ltsmin_expr_t P = ctlmu(in->arg1->arg1, env, free + 1);
                        ltsmin_expr_t A = LTSminExpr(BINARY_OP, MU_AND, LTSminBinaryIdx(env, MU_NAME(MU_AND)), P, res);
                        ltsmin_expr_t Q = ctlmu(in->arg1->arg2, env, free + 1);
                        ltsmin_expr_t O = LTSminExpr(BINARY_OP, MU_OR, LTSminBinaryIdx(env, MU_NAME(MU_OR)), Q, A);
                        ltsmin_expr_t result = LTSminExpr(MU_FIX, MU_MU, free, O, 0);
                        return result;
                    }
                    case CTL_FUTURE: {
			// translate E F p into MU Z. p \/ E X Z
			// translate A F p into MU Z. p \/ (A X Z /\ E X true)
                        create_mu_var(free, env);
                        ltsmin_expr_t Z = LTSminExpr((ltsmin_expr_case)MU_VAR, MU_VAR, free, 0, 0);
                        ltsmin_expr_t X = LTSminExpr(UNARY_OP, MU_NEXT, LTSminUnaryIdx(env, MU_NAME(MU_NEXT)), Z, 0);
                        res->arg1 = X;
                        res->arg1->parent = res;
                        LTSminExprRehash(res);
			if (in->token == CTL_ALL) {
			    ltsmin_expr_t True = LTSminExpr(CONSTANT, MU_TRUE, LTSminConstantIdx(env, MU_NAME(MU_TRUE)), NULL, NULL);
			    ltsmin_expr_t XTrue = LTSminExpr(UNARY_OP, MU_NEXT, LTSminUnaryIdx(env, MU_NAME(MU_NEXT)), True, 0);
			    ltsmin_expr_t NoDeadlock = LTSminExpr(UNARY_OP, MU_EXIST, LTSminUnaryIdx(env, MU_NAME(MU_EXIST)), XTrue, 0);
			    res = LTSminExpr(BINARY_OP, MU_AND, LTSminBinaryIdx(env, MU_NAME(MU_AND)), res, NoDeadlock);
			}
                        ltsmin_expr_t P = ctlmu(in->arg1->arg1, env, free + 1);
                        ltsmin_expr_t O = LTSminExpr(BINARY_OP, MU_OR, LTSminBinaryIdx(env, MU_NAME(MU_OR)), P, res);
                        ltsmin_expr_t result = LTSminExpr(MU_FIX, MU_MU, free, O, 0);
                        return result;
                    }
                    case CTL_GLOBALLY: {
			// translate A G p into NU Z. p /\ A X Z
			// translate E G p into NU Z. p /\ (E X Z \/ AX false)
                        create_mu_var(free, env);
                        ltsmin_expr_t Z = LTSminExpr((ltsmin_expr_case)MU_VAR, MU_VAR, free, 0, 0);
                        ltsmin_expr_t X = LTSminExpr(UNARY_OP, MU_NEXT, LTSminUnaryIdx(env, MU_NAME(MU_NEXT)), Z, 0);
                        res->arg1 = X;
                        res->arg1->parent = res;
                        LTSminExprRehash(res);
			if (in->token == CTL_EXIST) {
			    ltsmin_expr_t False = LTSminExpr(CONSTANT, MU_FALSE, LTSminConstantIdx(env, MU_NAME(MU_FALSE)), NULL, NULL);
			    ltsmin_expr_t XFalse = LTSminExpr(UNARY_OP, MU_NEXT, LTSminUnaryIdx(env, MU_NAME(MU_NEXT)), False, 0);
			    ltsmin_expr_t Deadlock = LTSminExpr(UNARY_OP, MU_ALL, LTSminUnaryIdx(env, MU_NAME(MU_ALL)), XFalse, 0);
			    res = LTSminExpr(BINARY_OP, MU_OR, LTSminBinaryIdx(env, MU_NAME(MU_OR)), res, Deadlock);
			}

                        ltsmin_expr_t P = ctlmu(in->arg1->arg1, env, free + 1);
                        ltsmin_expr_t A = LTSminExpr(BINARY_OP, MU_AND, LTSminBinaryIdx(env, MU_NAME(MU_AND)), P, res);
                        ltsmin_expr_t result = LTSminExpr(NU_FIX, MU_NU, free, A, 0);
                        return result;
                    }
                    case CTL_NEXT: { // translate A/E X to A/E X
                        ltsmin_expr_t resX = LTSminExpr(
                                UNARY_OP,
                                MU_NEXT,
                                LTSminUnaryIdx(env, MU_NAME(MU_NEXT)),
                                ctlmu(in->arg1->arg1, env, free),
                                NULL);
                        res->arg1 = resX;
                        res->arg1->parent = res;
                        LTSminExprRehash(res);
                        return res;
                    }
                    default:
                        Abort("ctl_to_mu: subformula of A/E should start with U,F,G, or X");
                    }
                }
                case CTL_FUTURE: case CTL_GLOBALLY: case CTL_NEXT: {
                    Abort("ctl_to_mu: (sub)formula should not start with F, G, X");
                }
                default: {
                    res->arg1 = ctlmu(in->arg1, env, free);
                    res->arg1->parent = res;
                    LTSminExprRehash(res);
                    return res;
                }
            }
        }
        case CONSTANT: {
            res->idx = LTSminConstantIdx(env, MU_NAME(mu_token));
            LTSminExprRehash(res);
            return res;
        }
        default: {
            return res;
        }
    }
}

ltsmin_expr_t ctl_to_mu(ltsmin_expr_t in, ltsmin_parse_env_t env, lts_type_t lts_type)
{
    HREassert(SIgetCount(env->idents) == 0);
    
    LTSminParseEnvReset(env);
    fill_env(env, lts_type);
    create_mu_env(env);
    ltsmin_expr_t n = ctlmu(in, env, 0);
    LTSminExprDestroy(in, 1);
    return n;
}

ltsmin_expr_t ltl_to_mu(ltsmin_expr_t in)
{ // TODO use BA or TGBA from SPOT
  return ctl_star_to_mu(in);
}

int tableaux_node_eq(tableaux_node_t *n1, tableaux_node_t *n2)
{
    if (!n1 && !n2) return 1;
    if (!n1 || !n2) return 0;
    if (n1 == n2) return 1;
    if (n1->hash != n2->hash) return 0;
    // check quantifier
    if (n1->quantifier != n2->quantifier) return 0;
    // check expression list
    tableaux_expr_list_t *n1l = n1->expr_list;
    tableaux_expr_list_t *n2l = n2->expr_list;
    while(n1l && n2l) {
        // expressions are supposed to be unique, ie
        // there is at most 1 pointer to one expression
        if (n1l->expr != n2l->expr) return 0;
        n1l = n1l->next; n2l = n2l->next;
    }
    return (!n1l && !n2l);
}

/* create a structure that holds the tableaux to convert the ctl star to mu calculus */
tableaux_t* tableaux_create()
{
    tableaux_t *t = RTmallocZero(sizeof(tableaux_t));
    t->expressions.size = 1 << 5;
    t->expressions.table = RTmallocZero(sizeof(tableaux_table_item_t)*t->expressions.size);
    t->expressions.fn_eq = (int (*)(void*,void*))LTSminExprEq;

    t->nodes.size = 1 << 5;
    t->nodes.table = RTmallocZero(sizeof(tableaux_table_item_t)*t->nodes.size);
    t->nodes.fn_eq = (int (*)(void*,void*))tableaux_node_eq;
    return t;
}

/* destroy the tableaux */
void tableaux_destroy(tableaux_t *t)
{
    RTfree(t->expressions.table);
    RTfree(t->nodes.table);
    RTfree(t);
}

/* grow the simple hash table, size = size * 2 */
void tableaux_table_grow(tableaux_table_t *t)
{
    tableaux_table_t new_t;
    new_t.size = t->size << 1;
    new_t.count = 0;
    new_t.table = RTmallocZero(sizeof(tableaux_table_item_t)*new_t.size);
    if (new_t.table) {
        // move all elements
        for(int el=0; el < t->size; el++) {
            if (t->table[el].data != NULL)
                tableaux_table_add(&new_t, t->table[el].hash, t->table[el].data);
        }
        // swap internal tables
        RTfree(t->table);
        t->table = new_t.table;
        t->size = new_t.size;
        return;
    }
    Abort("tl tableaux realloc failed");
}

/* add an element to the simple hash table
 * data is added by linear probing, if bucket already used, try bucket+1, +2,...
 * Requires:
 * (equal) element not already in the table
 * data != NULL
 */
void tableaux_table_add(tableaux_table_t *t, uint32_t hash, void* data)
{
    //HREassert(hash != NULL, "no hash");
    uint32_t key = hash & (t->size-1);
    while(1) {
        // empty bucket?
        if (t->table[key].data == NULL) {
            t->table[key].hash = hash; t->table[key].data = data;
            break;
        } else {
            // linear probing
            key = (key+1) % t->size;
        }
    }
    t->count++;

    // grow table if load factor > 0.7
    if (t->count > t->size * 0.7)
        tableaux_table_grow(t);
}

/* lookup a hash in the table, use fn_eq set in struct to compare data */
void* tableaux_table_lookup(tableaux_table_t *t, uint32_t hash, void* data)
{
    uint32_t key = hash & (t->size-1);
    while(1) {
        // found?
        if (t->table[key].hash == hash) {
            if (t->fn_eq(t->table[key].data, data)) {
                return t->table[key].data;
            }

        // empty bucket?
        } else if (t->table[key].data == NULL) {
            return NULL;
        }

        // linear probing
        key = (key+1) % t->size;
    }
}

/* print ctl/ctl* expression in a buffer
 * returns the buffer + size taken for the expression
 * assumes buffer is large enough */
char* ltsmin_expr_print_ctl(ltsmin_expr_t ctl, char* buf)
{
    // no equation
    if (!ctl) return buf;

    // left eq
    switch(ctl->node_type) {
        case BINARY_OP:
            *buf++='(';
            buf = ltsmin_expr_print_ctl(ctl->arg1, buf);
        default:;
    }
    // middle
    switch(ctl->token) {
        case CTL_SVAR: sprintf(buf, "@S%d", ctl->idx); break;
        case CTL_EVAR: sprintf(buf, "@E%d", ctl->idx); break;
        case CTL_NUM: sprintf(buf, "%d", ctl->idx); break;
        case CTL_CHUNK: sprintf(buf, "@H%d", ctl->idx); break;
        case CTL_LT: sprintf(buf, " < "); break;
        case CTL_LEQ: sprintf(buf, " <= "); break;
        case CTL_GT: sprintf(buf, " > "); break;
        case CTL_GEQ: sprintf(buf, " >= "); break;
        case CTL_EQ: sprintf(buf, " == "); break;
        case CTL_NEQ: sprintf(buf, " != "); break;
        case CTL_TRUE: sprintf(buf, "true"); break;
        case CTL_OR: sprintf(buf, " or "); break;
        case CTL_NOT: sprintf(buf, "!"); break;
        case CTL_NEXT: sprintf(buf, "X "); break;
        case CTL_UNTIL: sprintf(buf, " U "); break;
        case CTL_FALSE: sprintf(buf, "false"); break;
        case CTL_AND: sprintf(buf, " and "); break;
        case CTL_EQUIV: sprintf(buf, " <-> "); break;
        case CTL_IMPLY: sprintf(buf, " -> "); break;
        case CTL_FUTURE: sprintf(buf, "F "); break;
        case CTL_GLOBALLY: sprintf(buf, "G "); break;
        case CTL_EXIST: sprintf(buf, "E "); break;
        case CTL_ALL: sprintf(buf, "A "); break;
        case CTL_MULT: sprintf(buf, " * "); break;
        case CTL_DIV: sprintf(buf, " / "); break;
        case CTL_REM: sprintf(buf, " %% "); break;
        case CTL_ADD: sprintf(buf, " + "); break;
        case CTL_SUB: sprintf(buf, " - "); break;
        default:
            Abort("unknown CTL token");
    }
    buf += strlen(buf);
    // right eq
    switch(ctl->node_type) {
        case UNARY_OP:
            buf = ltsmin_expr_print_ctl(ctl->arg1, buf);
            break;
        case BINARY_OP:
            buf = ltsmin_expr_print_ctl(ctl->arg2, buf);
            *buf++=')';
            *buf='\0';
            break;
        default:;
    }
    *buf='\0';
    return buf;
}

/* print a node in a buffer */
void tableaux_node_print(tableaux_node_t *n, char* buf)
{
    if (n == NULL) {
        sprintf(buf, "NULL"); return;
    }
    char Q[] = {'N', 'A', 'E'};
    sprintf(buf, "%c: [", Q[n->quantifier]);
    buf += strlen(buf);
    tableaux_expr_list_t *l = n->expr_list;
    while(l != NULL) {
        if (l != n->expr_list) { sprintf(buf, ","); buf++; }
        buf = ltsmin_expr_print_ctl(l->expr, buf);
        /* the following lines also print the generating expressions
        if (l->generating_expr[0]) {
        sprintf(buf, "<~");
        buf += strlen(buf);
        buf = ltsmin_expr_print_ctl(l->generating_expr[0]->expr, buf);
        }
        if (l->generating_expr[1]) {
        sprintf(buf, "<~");
        buf += strlen(buf);
        buf = ltsmin_expr_print_ctl(l->generating_expr[1]->expr, buf);
        } */
        l = l->next;
    }
    sprintf(buf, "]");
}

/* Add an expression to a node
 * If no node (n == NULL), create a new node structure
 * otherwise, add the expression in the expression list, in increasing order of
 * the hash
 */
tableaux_node_t* tableaux_node_add_expr(tableaux_node_quantifier_t q, tableaux_node_t *n, ltsmin_expr_t e, tableaux_expr_list_t *generating_e)
{
    tableaux_node_t *res = n;
    // add expression to tableaux node
    if (res == NULL) {
        res = RTmallocZero(sizeof(tableaux_node_t));
        res->quantifier = q;
        res->hash = (0x9AB724D1 * q) + e->hash;
        // add first expression
        res->expr_list = RTmalloc(sizeof(tableaux_expr_list_t));
        res->expr_list->expr = e;
        res->expr_list->next = NULL;
        res->expr_list->generating_expr[0] = generating_e;
        res->expr_list->generating_expr[1] = NULL;
        return res;
    } else {
        if (res->quantifier != q) Abort("Tableaux node quantifiers don't match")
    }
    // add to tableaux list
    // note: list is sorted in increasing order of the hash values of the expressions
    // new expression
    tableaux_expr_list_t *ne = RTmalloc(sizeof(tableaux_expr_list_t));
    ne->expr = e;
    ne->generating_expr[0] = generating_e;
    ne->generating_expr[1] = NULL;
    // list pointer for updating
    tableaux_expr_list_t **l = &n->expr_list;
    while (*l != NULL) {
        if ( (*l)->expr->hash < e->hash) break;
        // assume unique expressions are added
        if ( (*l)->expr == e) {
            RTfree(ne);
            // add generating expression
            if ((*l)->generating_expr[0] != generating_e && (*l)->generating_expr[1] != generating_e) {
                if ((*l)->generating_expr[0] == NULL) {
                    (*l)->generating_expr[0] = generating_e;
                } else {
                    if ((*l)->generating_expr[1] != NULL) Abort("invalid assumption: generating_expr[<2]");
                    (*l)->generating_expr[1] = generating_e;
                }
            }
            return res;
        }
        l =  &(*l)->next;
    }
    // add new item to list
    ne->next = *l;
    *l = ne;

    // update hash
    // hash = sum of hashes of the expressions + (0x9AB724D1 * quantifier);
    res->hash += e->hash;
    return res;
}

/* for debuggin, build a test node instead of a parsed ctl* formula */
tableaux_node_t* build_test_node(tableaux_t *t)
{
    ltsmin_expr_t n = LTSminExpr(INT, CTL_NUM, 1, NULL, NULL);
    ltsmin_expr_t v = LTSminExpr(VAR, CTL_VAR, 0, NULL, NULL);
    ltsmin_expr_t eq = LTSminExpr(BINARY_OP, CTL_EQ, 0, v, n);
    ltsmin_expr_t g = LTSminExpr(UNARY_OP, CTL_GLOBALLY, 0, eq, NULL);
    ltsmin_expr_t xg = LTSminExpr(UNARY_OP, CTL_NEXT, 0, g, NULL);
    ltsmin_expr_t fxg = LTSminExpr(UNARY_OP, CTL_FUTURE, 0, xg, NULL);
    ltsmin_expr_t xfxg = LTSminExpr(UNARY_OP, CTL_NEXT, 0, fxg, NULL);

    // add expressions
    tableaux_table_add(&t->expressions, xg->hash, xg);
    tableaux_table_add(&t->expressions, xfxg->hash, xfxg);

    // create node
    tableaux_node_t *res = tableaux_node_add_expr(NODE_ALL, NULL, xg, NULL);
    res = tableaux_node_add_expr(NODE_ALL, res, xfxg, NULL);

    // add node
    tableaux_table_add(&t->nodes, res->hash, res);

    return res;
}

/* Extend a syntax tree node on branch b with a label and a node
 * Some care must be taken to extend TABLEAUX rules
 * If a NULL node is added to the left side of a tableaux expression
 * The tableaux rule is set to TABLEAUX_IDENTITY instead
 * 1) if a NULL node is added, this tableaux is not extended and the sytax
 *    tree is labelled TABLEAUX_IDENTITY instead
 * 2) if the right side is added, the left side is used instead
 *
 * 3) If both the nodes are NULL, this is handled by apply rule
 */
syntax_tree_t* tableaux_extend_syntax_tree(tableaux_t *t, syntax_tree_t *s, syntax_tree_branch_t b, syntax_tree_label_t label, tableaux_node_t *n)
{
    // whoops, no expressions in node
    if (n == NULL) {
        s->label = TABLEAUX_IDENTITY;
        return NULL;
    }

    // allocate syntax tree ndoe
    syntax_tree_t *st = RTmallocZero(sizeof(syntax_tree_t));

    // lookup n, already in tableaux?
    tableaux_node_t *n_old;
    if ((n_old = tableaux_table_lookup(&t->nodes, n->hash, n))) {
        // is this a preterminal?
        // is there a node n' strictly above n that has an equal labelling?
        {
            syntax_tree_t *p = s->parent;
            while(p) {
                if (tableaux_node_eq(p->node, n_old)) {
                    st->companion = p;
                    p->is_companion = 1;

                    st->label = TABLEAUX_PRETERMINAL;
                    break;
                }
                p = p->parent;
            }
        }
    } else {
        // add node to tableaux table
        tableaux_table_add(&t->nodes, n->hash, n);
    }
    st->node = n;
    st->parent = s;
    if (s->label != TABLEAUX_IDENTITY) {
        s->label = label;
        s->branch[b] = st;
    } else {
        s->branch[ST_LEFT] = st;
    }
    return st;
}

/* Adds all unused expressions (not in rule application) to a new node */
tableaux_node_t* tableaux_node_delta_phi(tableaux_node_quantifier_t q, tableaux_node_t *n_origin, tableaux_expr_list_t *generating_e)
{
    tableaux_node_t *n = NULL;
    tableaux_expr_list_t *l = n_origin->expr_list;
    while(l) {
        if (l != generating_e)
            n = tableaux_node_add_expr(q, n, l->expr, l);
        l = l->next;
    }
    return n;
}

/* returns the equivalent expression already in the tableaux table, or
 * adds the expression to the tableaux table */
ltsmin_expr_t tableaux_expr(tableaux_t *t, ltsmin_expr_t e)
{
    // convert expression to positive normal form
    e = ctl_star_to_pnf(e);
    ltsmin_expr_t result = NULL;
    // assume arg1 is set
    if (!(result = tableaux_table_lookup(&t->expressions, e->hash, e))) {
        result = e;
        tableaux_table_add(&t->expressions, e->hash, e);
    } else {
        LTSminExprDestroy(e, 1);
    }
    return result;
}

/* return the tail of a unary expression: X phi returns phi */
ltsmin_expr_t tableaux_expr_unary_tail(tableaux_t *t, ltsmin_expr_t e)
{
    // return tail of a unary expression UNARY_OP phi thus phi
    // the function also looks up the expression in the expression
    // table, such that no duplicates can exist
    if (e->node_type != UNARY_OP) Abort("tableaux_expr_unary_tail on non-unary operator");
    return tableaux_expr(t, e->arg1);
}

/* prepend a unary_op to an expression */
ltsmin_expr_t tableaux_expr_unary_cons(tableaux_t *t, CTL op, ltsmin_expr_t e)
{
    // convert node to positive normal form
    ltsmin_expr_t ee = LTSminExpr(UNARY_OP, op, 0, e, NULL);
    ltsmin_expr_t res = tableaux_expr(t, ee);
    RTfree(ee);
    return res;
}

/* Apply a rule in the tableaux */
void tableaux_apply_rule(tableaux_t *t, syntax_tree_t *s)
{
    // need to fill label, and all nodes below the line in the syntax tree
    switch(s->node->quantifier)
    {
        case NODE_EXIST:
        case NODE_ALL: {
            // transition rule:
            //  A ( X phi_1, ... , X phi_n )
            // ----------------------------
            //  A ( phi_1, ... , phi_n )

            // applicable?
            // do all expressions start with CTL_NEXT?
            tableaux_expr_list_t *el = s->node->expr_list;
            while(el) {
                if (el->expr->token == CTL_NEXT) {
                    el = el->next;
                } else break;
            }
            // apply transition rule if at end of list
            if (!el) {
                tableaux_node_t *n = NULL;
                // insert all expressions in table
                el = s->node->expr_list;
                while(el) {
                    ltsmin_expr_t e = tableaux_expr_unary_tail(t, el->expr);
                    n = tableaux_node_add_expr(s->node->quantifier, n, e, el);
                    el = el->next;
                }

                tableaux_extend_syntax_tree(t, s, ST_LEFT, s->node->quantifier == NODE_ALL?TABLEAUX_ALL_NEXT:TABLEAUX_EXIST_NEXT, n);
            } else {
                // note: el->expr is the first expression in the list for which a rule is applicable
                tableaux_node_t *n_left = NULL;
                tableaux_node_t *n_right = NULL;
                switch(el->expr->token) {
                    case CTL_FUTURE:
                    case CTL_GLOBALLY: {

                        /* The four cases
                         *           A(PHI, F phi)                    A(PHI, G phi)
                         *  I  ----------------------   /\  ----------------------------------
                         *      A(PHI, phi, O F phi))        A(PHI, phi)  A(PSI, phi, O G phi)
                         *
                         *           E(PHI, G phi)                    E(PHI, F phi)
                         *  I  ----------------------   \/  ----------------------------------
                         *      E(PHI, phi, O G phi))        E(PHI, phi)  E(PSI, phi, O F phi)
                         */

                        int split = ((s->node->quantifier == NODE_ALL && el->expr->token == CTL_GLOBALLY ) ||
                                     (s->node->quantifier == NODE_EXIST && el->expr->token == CTL_FUTURE) );
                        syntax_tree_label_t l = el->expr->token == CTL_GLOBALLY ? TABLEAUX_AND : TABLEAUX_OR;
                        if (split) {
                            n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                            n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr_unary_tail(t, el->expr), el);
                            tableaux_extend_syntax_tree(t, s, ST_LEFT, l, n_left);

                            n_right = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                            n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr_unary_cons(t, CTL_NEXT, el->expr), el);
                            tableaux_extend_syntax_tree(t, s, ST_RIGHT, l, n_right);
                        } else {
                            n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                            n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr_unary_tail(t, el->expr), el);
                            n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr_unary_cons(t, CTL_NEXT, el->expr), el);
                            tableaux_extend_syntax_tree(t, s, ST_LEFT, TABLEAUX_IDENTITY, n_left);
                        }

                        } break;
                    case CTL_TRUE:
                    case CTL_FALSE:
                    case CTL_EQ: {
                        /* The two cases
                         *       A(PHI, Y)         E(PHI, Y)
                         *  \/  -----------   /\  -----------
                         *       A(PHI)  Y        E(PHI)   Y
                         */
                        syntax_tree_label_t l = s->node->quantifier == NODE_ALL ? TABLEAUX_OR : TABLEAUX_AND;

                        n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                        tableaux_extend_syntax_tree(t, s, ST_LEFT, l, n_left);

                        n_right = tableaux_node_add_expr(NODE_NONE, n_right, el->expr, el);
                        tableaux_extend_syntax_tree(t, s, ST_RIGHT, l, n_right);
                        } break;
                    case CTL_UNTIL: {
                        /* The two cases
                         *                  A(PHI, phi U psi)
                         *  /\  ---------------------------------------------
                         *       A(PHI, psi, phi)  A(PHI, psi, X (phi U psi) )
                         *
                         *                  E(PHI, phi U psi)
                         *  \/  ---------------------------------------------
                         *       E(PHI, psi)       E(PHI, phi, X (phi U psi) )
                         */
                        switch (s->node->quantifier) {
                            case NODE_ALL: {
                                n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr(t, el->expr->arg2), el);
                                n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr(t, el->expr->arg1), el);
                                tableaux_extend_syntax_tree(t, s, ST_LEFT, TABLEAUX_AND, n_left);

                                n_right = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr(t, el->expr->arg2), el);
                                n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr_unary_cons(t, CTL_NEXT, el->expr), el);
                                tableaux_extend_syntax_tree(t, s, ST_RIGHT, TABLEAUX_AND, n_right);
                                } break;
                            case NODE_EXIST: {
                                n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr(t, el->expr->arg2), el);
                                tableaux_extend_syntax_tree(t, s, ST_LEFT, TABLEAUX_OR, n_left);

                                n_right = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr(t, el->expr->arg1), el);
                                n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr_unary_cons(t, CTL_NEXT, el->expr), el);
                                tableaux_extend_syntax_tree(t, s, ST_RIGHT, TABLEAUX_OR, n_right);
                                } break;
                            case NODE_NONE:;
                        }
                        } break;
                    case CTL_NOT: {
                        // for not, check two tokens
                        switch(el->expr->arg1->token)
                        {
                            case CTL_NOT: {
                                /* The two cases
                                 *       A(PHI, !!Y)       E(PHI, !!Y)
                                 *   I  -------------   I -------------
                                 *       A(PHI)    Y        E(PHI)   Y
                                 */
                                n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr_unary_tail(t, el->expr->arg1), el);

                                tableaux_extend_syntax_tree(t, s, ST_LEFT, TABLEAUX_IDENTITY, n_left);
                                } break;

                            case CTL_FALSE:
                            case CTL_TRUE:
                            case CTL_EQ: {
                                // * same as CTL_EQ */

                                /* The two cases
                                 *       A(PHI, !Y)         E(PHI, !Y)
                                 *  \/  ------------   /\  ------------
                                 *       A(PHI)  !Y         E(PHI)  !Y
                                 */
                                syntax_tree_label_t l = s->node->quantifier == NODE_ALL ? TABLEAUX_OR : TABLEAUX_AND;

                                n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                tableaux_extend_syntax_tree(t, s, ST_LEFT, l, n_left);

                                n_right = tableaux_node_add_expr(NODE_NONE, n_right, el->expr, el);
                                tableaux_extend_syntax_tree(t, s, ST_RIGHT, l, n_right);
                                } break;
                            case CTL_UNTIL: {
                                /* Dual of CTL_UNTIL */

                                /* The two cases
                                 *                  A(PHI, !(phi U psi))
                                 *  /\  -------------------------------------------------
                                 *       A(PHI, !psi)      A(PHI, !phi, X (!(phi U psi)) )
                                 *
                                 *                  E(PHI, !(phi U psi))
                                 *  \/  ---------------------------------------------------
                                 *       E(PHI, !psi, !phi)  E(PHI, !psi, X(!(phi U psi)) )
                                 */
                                switch (s->node->quantifier) {
                                    case NODE_ALL: {
                                        n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                        n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr_unary_cons(t, CTL_NOT, el->expr->arg1->arg2), el);
                                        tableaux_extend_syntax_tree(t, s, ST_LEFT, TABLEAUX_AND, n_left);

                                        n_right = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                        n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr_unary_cons(t, CTL_NOT, el->expr->arg1->arg1), el);
                                        n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr_unary_cons(t, CTL_NEXT, el->expr), el);
                                        tableaux_extend_syntax_tree(t, s, ST_RIGHT, TABLEAUX_AND, n_right);
                                        } break;
                                    case NODE_EXIST: {
                                        n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                        n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr_unary_cons(t, CTL_NOT, el->expr->arg1->arg2), el);
                                        n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr_unary_cons(t, CTL_NOT, el->expr->arg1->arg1), el);
                                        tableaux_extend_syntax_tree(t, s, ST_LEFT, TABLEAUX_OR, n_left);

                                        n_right = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                                        n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr_unary_cons(t, CTL_NOT, el->expr->arg1->arg2), el);
                                        n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr_unary_cons(t, CTL_NEXT, el->expr), el);
                                        tableaux_extend_syntax_tree(t, s, ST_RIGHT, TABLEAUX_OR, n_right);
                                        } break;
                                    case NODE_NONE:;
                                }
                                } break;
                            default:
                                // what about ! A (..) and ! E( ..) -> should possibly use tableaux_not?
                                Abort("unhandled in conversion not expr");
                        }

                        } break;
                    case CTL_OR:
                    case CTL_AND: {
                        /* The four cases
                         *        A(PHI, phi /\ psi)           A(PHI, phi \/ psi)
                         * /\  -----------------------     I  --------------------
                         *     A(PHI, phi) A(PHI, psi)          A(PHI, phi, psy)
                         *
                         *        E(PHI, phi \/ psi)           E(PHI, phi /\ psi)
                         * \/  -----------------------     I  --------------------
                         *     E(PHI, phi) E(PHI, psi)          E(PHI, phi, psy)
                         *
                         * Thus:
                         *           split                         identity
                         *  ^ label  =    ctl ^ label
                         */
                        int split = ((s->node->quantifier == NODE_ALL && el->expr->token == CTL_AND ) ||
                                     (s->node->quantifier == NODE_EXIST && el->expr->token == CTL_OR) );
                        syntax_tree_label_t l = el->expr->token == CTL_AND ? TABLEAUX_AND : TABLEAUX_OR;

                        if (split) {
                            n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                            n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr(t, el->expr->arg1), el);
                            tableaux_extend_syntax_tree(t, s, ST_LEFT, l, n_left);

                            n_right = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                            n_right = tableaux_node_add_expr(s->node->quantifier, n_right, tableaux_expr(t, el->expr->arg2), el);
                            tableaux_extend_syntax_tree(t, s, ST_RIGHT, l, n_right);
                        } else {
                            n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                            n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr(t, el->expr->arg1), el);
                            n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr(t, el->expr->arg2), el);

                            tableaux_extend_syntax_tree(t, s, ST_LEFT, TABLEAUX_IDENTITY, n_left);
                        }
                        } break;
                    case CTL_EXIST:
                    case CTL_ALL: {
                        /* The four cases
                         *      A(PHI, A(PSI))         A(PHI, E(PSI))
                         * \/  ---------------   \/  ----------------
                         *      A(PHI) A(PSI)          A(PHI) E(PSI)
                         *
                         *      E(PHI, A(PSI))         E(PHI, E(PSI))
                         * /\  ---------------   /\  ----------------
                         *      E(PHI) A(PSI)          E(PHI) E(PSI)
                         */
                        tableaux_node_quantifier_t expr_quantifier = el->expr->token == CTL_ALL ? NODE_ALL : NODE_EXIST;
                        syntax_tree_label_t l = s->node->quantifier == NODE_ALL ? TABLEAUX_OR : TABLEAUX_AND;

                        n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                        tableaux_extend_syntax_tree(t, s, ST_LEFT, l, n_left);
                        n_right = tableaux_node_add_expr(expr_quantifier, n_right, tableaux_expr_unary_tail(t, el->expr), el);
                        tableaux_extend_syntax_tree(t, s, ST_RIGHT, l, n_right);
                        } break;
                    case CTL_IMPLY:
                        /* convert phi -> psi into !phi | psi */
                        n_left = tableaux_node_delta_phi(s->node->quantifier, s->node, el);
                        // split imply expression
                        ltsmin_expr_t e = LTSminExpr(BINARY_OP, CTL_OR, 0,
                            LTSminExpr(UNARY_OP, CTL_NOT, 0, el->expr->arg1, NULL),
                            el->expr->arg2);
                        n_left = tableaux_node_add_expr(s->node->quantifier, n_left, tableaux_expr(t,e), el);
                        tableaux_extend_syntax_tree(t, s, ST_LEFT, TABLEAUX_IDENTITY, n_left);
                        // free created expression
                        RTfree(e->arg1);
                        RTfree(e);

                        break;
                    default:
                        Abort("unhandled case in tableaux apply_rule");
                }
                return;
            }

            } break;
        case NODE_NONE:
            s->label = TABLEAUX_TERMINAL;
            break;
    }
}

/* for debugging only: print the syntax tree */

static array_manager_t line_man=NULL;
static char** tableaux_lines;
static int max_level = 0;

#define LMRLINE 4096
static char* tableaux_label[] = {"  ", "  ", "  ", " !", "/\\","\\/","AX","EX"};
// pstart initially specifies minimal left position of the parent
// after function call it is the start position of the drawn child
#ifndef MAX
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#endif
void tableaux_print_compressed(syntax_tree_t *s, int level, int* pstart, int* pstop)
{
    // reserve two line buffers
    ensure_access(line_man, level+1);
    if (max_level<level) max_level = level+1;
    if (tableaux_lines[level] == NULL)
        tableaux_lines[level] = RTmallocZero(LMRLINE);
    if (tableaux_lines[level+1] == NULL)
        tableaux_lines[level+1] = RTmallocZero(LMRLINE);

    // plot node
    char* line = tableaux_lines[level];
    int line_len = strlen(line);

    // print node
    char node_str[4096];
    tableaux_node_print(s->node, node_str);
    int  node_len  = strlen(node_str);

    // copy start stop
    int  start = *pstart;
    int  between = *pstart+node_len;
    int  stop = between;

    // move start/stop
    if (line_len > (start - 6)) {
        line_len += (line_len-(start-6)<6)?line_len-(start-6):6; // min(6, line_len-(start-6)); // correct?
        stop += (line_len - start);
        start = line_len; // += (line_len - start);
    }

    // print left branch
    if (s->branch[ST_LEFT] != NULL) {
        tableaux_print_compressed(s->branch[ST_LEFT], level+2, &start, &between);

        // assumption right branch is only set when left branch != NULL
        // fix truncated stop value
        stop = MAX(start + node_len, between);

        // print right branch
        if (s->branch[ST_RIGHT] != NULL) {
            // add some extra spacing between the nodes for readability
            between+=6; stop +=6;
            // print the node
            tableaux_print_compressed(s->branch[ST_RIGHT], level+2, &between, &stop);
        }
        //Warning(info, "after left/right start %d between %d stop %d", start, between, stop);
    }

    // at this point stop - start should be >= node_len
    // furthermore, start is fixed after this

    //Warning(info, "after node start %d stop %d", start, stop);
    if (stop - start < node_len) {
        //Abort("failed node_len assumption");
        // hmmz, the right child should be aligned to right instead..
        stop = start + node_len;
    }

    // need more padding
    if (line_len <= start) {
        sprintf(line, "%-*s", start, line);
        line_len = start;
    }

    // print node, centered
    int padding_left = 0;
    int padding_right = 0;
    int padding_len = stop - start;
    if (node_len < padding_len) {
        int padding = padding_len - node_len;
        padding_left  = padding / 2;
        padding_right = padding - padding_left;
    }
    sprintf(line+line_len, "%*s%s", padding_left, "", node_str);

    // print vertical line (start -- stop)
    // except below a terminal
    if (s->branch[ST_LEFT] != NULL) {
        sprintf(tableaux_lines[level+1], "%-*s", start, tableaux_lines[level+1]);
        sprintf(tableaux_lines[level+1]+start-3, "%s ", tableaux_label[s->label]);
        memset(tableaux_lines[level+1]+start, '-', stop-start);
    }

    // fix parents start/stop
    *pstart = start + padding_left;
    *pstop =  stop - padding_right;
}

/* print the tableux */
void tableaux_print(tableaux_t *t)
{
    max_level = 0;
    line_man = create_manager(65536);
    ADD_ARRAY(line_man, tableaux_lines, char*);
    int start = 6;
    int stop = start;
    tableaux_print_compressed(&t->root, 0, &start, &stop);
    for(int i=0; i <= max_level; i++) {
        Warning(infoLong, "%s", tableaux_lines[i]);
    }
    destroy_manager(line_man);
}

/* count the variables in some expression in the tableaux */
/* counting is used to prevent clashes between variables assigned to mu/nu operators */
void tableaux_count_vars_e(tableaux_t *t, ltsmin_expr_t e)
{
    // find expression terminals
    if (e->arg1 != NULL) tableaux_count_vars_e(t, e->arg1);
    if (e->arg2 != NULL) tableaux_count_vars_e(t, e->arg2);
    if (e->arg1 == NULL && e->arg2 == NULL && e->token == CTL_VAR) {
        t->used_var = MAX(t->used_var, e->idx + 1);
    }
}
/* count the variables in some syntax tree */
/* counting is used to prevent clashes between variables assigned to mu/nu operators */
void tableaux_count_vars_s(tableaux_t *t, syntax_tree_t *s)
{
    // find tableaux terminals
    if (s->branch[ST_LEFT]) tableaux_count_vars_s(t,s->branch[ST_LEFT]);
    if (s->branch[ST_RIGHT]) tableaux_count_vars_s(t,s->branch[ST_RIGHT]);
    // look for use of token = MU_VAR in equation
    if (!s->branch[ST_LEFT] && !s->branch[ST_RIGHT]) {
        tableaux_expr_list_t *el = s->node->expr_list;
        while(el) {
            tableaux_count_vars_e(t, el->expr);
            el = el->next;
        }

    }
}

/* apply rule to the root node, continue until terminal node is encountered */
void tableaux_build_syntax_tree(tableaux_t *t, syntax_tree_t* s)
{
    if (s->label == TABLEAUX_PRETERMINAL) return;
    if (s->label == TABLEAUX_IDENTITY && s->branch[ST_LEFT] == NULL && s->branch[ST_RIGHT] == NULL) {
        // this should destroy the syntax_tree node, and set the TABLEAUX_IDENTITY flag if the parent
        // this can probably only happen if MU_TRUE and MU_FALSE are converted to NULL nodes for the
        // tableaux. For simplicity, just let them in the formula, and simplify afterwards.
        Abort("TABLAUX_IDENTITY empty on both sides!");
    } else {
        tableaux_apply_rule(t, s);
        if (s->branch[ST_LEFT])  tableaux_build_syntax_tree(t, s->branch[ST_LEFT]);
        if (s->branch[ST_RIGHT]) tableaux_build_syntax_tree(t, s->branch[ST_RIGHT]);
    }

    // count the variables already used, set var to maximum
    if (!s->parent) tableaux_count_vars_s(t, s);
}

/* return the dual of a ctl token */
int ctl_star_dual(int token) {
    switch(token) {
        case CTL_ALL: return CTL_EXIST;
        case CTL_EXIST: return CTL_ALL;
        case CTL_FUTURE: return CTL_GLOBALLY;
        case CTL_GLOBALLY: return CTL_FUTURE;
        case CTL_AND: return CTL_OR;
        case CTL_OR: return CTL_AND;
        case CTL_NEXT: return CTL_NEXT;
        // case CTL_UNTIL: return CTL_RELEASE;
        // case CTL_WEAK_UNTIL: return CTL_STRONG_RELEASE;
    }
    Abort("invalid dual");
    return token;
}

/* convert ctl* to positive normal form */
/* De Morgan:
 * ! (p | q ) = !p & !q
 * ! (q & q ) = !p | !q
 * ! A p = E ! p
 * ! E p = A ! p
 * ! X p = X ! p
 * ! F p = G ! p
 * ! G p = F ! p
 */
ltsmin_expr_t ctl_star_to_pnf_1(ltsmin_expr_t e, int negated)
{
    if (e == NULL) return NULL;
    ltsmin_expr_t res = NULL;
    switch (e->token) {
        case CTL_UNTIL:
            // since the tableaux rules don't yet have a rule for release,
            // encode release using until rule:, thus, don't touch this negation
            // p V q = ! (!p U !q) (dual of until)
            // ! (p U q) = (!p V !q)
            // ! (p V q) = (!p U !q)
            // thus
            // ! (p U q) = (!p V !q), = ! (!!p U !!q) = !(p U q) :)
            if (negated) {
                res = LTSminExpr(e->node_type, e->token, e->idx,
                        ctl_star_to_pnf_1(e->arg1, !negated), ctl_star_to_pnf_1(e->arg2, !negated));
                res = LTSminExpr(UNARY_OP, CTL_NOT, 0, res, NULL);
            } else {
                res = LTSminExpr(e->node_type, e->token, e->idx,
                        ctl_star_to_pnf_1(e->arg1, negated), ctl_star_to_pnf_1(e->arg2, negated));
            }
            break;
        case CTL_NOT:
            res = ctl_star_to_pnf_1(e->arg1, !negated);
            break;
        case CTL_FALSE:
            if (negated) {
                res = LTSminExpr(CONSTANT, CTL_TRUE, 0, NULL, NULL);
            } else {
                res = LTSminExpr(CONSTANT, CTL_FALSE, 0, NULL, NULL);
            }
            break;
        case CTL_TRUE:
            if (negated) {
                res = LTSminExpr(CONSTANT, CTL_FALSE, 0, NULL, NULL);
            } else {
                res = LTSminExpr(CONSTANT, CTL_TRUE, 0, NULL, NULL);
            }
            break;
        case CTL_EVAR:
        case CTL_SVAR: case CTL_EQ: case CTL_VAR:
            // copy, if (negated), add CTL_NOT
            res = LTSminExprClone(e);
            if (negated)
                res = LTSminExpr(UNARY_OP, CTL_NOT, 0, res, NULL);
            break;
        default:
            res = LTSminExpr(e->node_type, negated? ctl_star_dual(e->token) : e->token, e->idx,
                        ctl_star_to_pnf_1(e->arg1, negated), ctl_star_to_pnf_1(e->arg2, negated));
    }
    return res;
}

/* convert ctl* formula to positive normal form */
ltsmin_expr_t ctl_star_to_pnf(ltsmin_expr_t e)
{
    return ctl_star_to_pnf_1(e, 0);
}

/* build the initial tableaux node (root node) */
tableaux_node_t* build_initial_node(tableaux_t *t, ltsmin_expr_t e)
{
    ltsmin_expr_t e_pnf = ctl_star_to_pnf(e);

    tableaux_node_quantifier_t q = NODE_ALL;

    if (e_pnf->token == CTL_ALL || e_pnf->token == CTL_EXIST) {
        // get tail
        q = (e_pnf->token == CTL_ALL) ? NODE_ALL : NODE_EXIST;
        e = tableaux_expr_unary_tail(t, e_pnf);
    } else {
        // add expression
        e = tableaux_expr(t, e_pnf);
    }
    // build node
    tableaux_node_t *n = tableaux_node_add_expr(q, NULL, e, NULL);
    // add node
    tableaux_table_add(&t->nodes, n->hash, n);
    // free pnf expression
    LTSminExprDestroy(e_pnf, 1);
    // return node
    return n;
}

/* print a mu expression */
char* ltsmin_expr_print_mu(ltsmin_expr_t mu, char* buf)
{
    // no equation
    if (!mu) return buf;

    // left eq
    switch(mu->node_type) {
        case BINARY_OP:
            *buf++ = '(';
            buf = ltsmin_expr_print_mu(mu->arg1, buf);
        default:;
    }
    // middle
    switch(mu->token) {
        case MU_SVAR: sprintf(buf, "@S%d", mu->idx); break;
        case MU_EVAR: sprintf(buf, "@E%d", mu->idx); break;
        case VAR: sprintf(buf, "@V%d", mu->idx); break;
        case MU_NUM: sprintf(buf, "%d", mu->idx); break;
        case MU_CHUNK: sprintf(buf, "@H%d", mu->idx); break;
        case MU_EQ: sprintf(buf, " == "); break;
        case MU_TRUE: sprintf(buf, "true"); break;
        case MU_OR: sprintf(buf, " \\/ "); break;
        case MU_NOT: sprintf(buf, "!"); break;
        case MU_NEXT: sprintf(buf, "X "); break;
        case MU_FALSE: sprintf(buf, "false"); break;
        case MU_AND: sprintf(buf, " /\\ "); break;
        //case MU_EQUIV: sprintf(buf, " <-> "); break;
        //case MU_IMPLY: sprintf(buf, " -> "); break;
        case MU_EXIST: sprintf(buf, "E "); break;
        case MU_ALL: sprintf(buf, "A "); break;
        case MU_MU:
            sprintf(buf, "(mu @V%d", mu->idx); buf += strlen(buf);
            sprintf(buf, ". "); buf += strlen(buf);
            buf = ltsmin_expr_print_mu(mu->arg1, buf);
            sprintf(buf, ")");
            break;
        case MU_NU:
            sprintf(buf, "(nu @V%d", mu->idx); buf += strlen(buf);
            sprintf(buf, ". "); buf += strlen(buf);
            buf = ltsmin_expr_print_mu(mu->arg1, buf);
            sprintf(buf, ")");
            break;
        case MU_EDGE_EXIST:
            sprintf(buf, "<%s> ", "?"); break; // TODO
        case MU_EDGE_ALL:
            sprintf(buf, "[%s] ", "?"); break; // TODO
        default:
            Abort("unknown MU token");
    }
    buf += strlen(buf);
    // right eq
    switch(mu->node_type) {
        case UNARY_OP:
            buf = ltsmin_expr_print_mu(mu->arg1, buf);
            break;
        case BINARY_OP:
            buf = ltsmin_expr_print_mu(mu->arg2, buf);
            *buf++ = ')';
            *buf = 0;
            break;
        default:;
    }
    *buf='\0';
    return buf;
}


/* convert CTL tokens to MU tokens */
ltsmin_expr_t ctl_star_to_mu_1(ltsmin_expr_t in)
{
    ltsmin_expr_t res = RT_NEW(struct ltsmin_expr_s);
    memcpy(res, in, sizeof(struct ltsmin_expr_s));
    res->token = ctl2mu_token(in->token);
    // handle sub-expressions
    switch (in->node_type) {
        case UNARY_OP:
            res->arg1 = ctl_star_to_mu_1(res->arg1);
            break;
        case BINARY_OP:
            res->arg1 = ctl_star_to_mu_1(res->arg1);
            res->arg2 = ctl_star_to_mu_1(res->arg2);
            break;
        default:
            break;
    }
    LTSminExprRehash(res);
    return res;
}

/* decide whether an expression is a mu/nu path-expression */
int is_path_expr(tableaux_path_t t, ltsmin_expr_t e)
{
    // path is in the form L(phi_1, phi2) or XL(phi_1, phi2)
    // with L = U for mu paths and L=!U for nu-paths
    if (e->token == CTL_NEXT) e = e->arg1;

    switch(t) {
        case MU_PATH:
            switch(e->token) {
                case CTL_FUTURE:
                case CTL_UNTIL:
                    return 1;
                case CTL_NOT:
                    // !G
                    return (e->arg1->token == CTL_GLOBALLY);
            }
            break;
        case NU_PATH:
            switch(e->token) {
                case CTL_GLOBALLY:
                   return 1;
                case CTL_NOT:
                    // !U or !F
                    return (e->arg1->token == CTL_UNTIL ||
                            e->arg1->token == CTL_FUTURE);
            }
            break;
    }
    return 0;
}

/* find a path to the companion (via parent relation)
 * then, decide whether there exist a mu/nu path from the companion to
 * to node on the way back
 * this is done by setting the in_path flag in the expression_list
 */
int find_path(tableaux_path_t t, syntax_tree_t* s, syntax_tree_t* companion)
{
    // clear is_path flag in nodes
    tableaux_expr_list_t* clear = s->node->expr_list;
    while(clear) {
        clear->in_path = 0;
        clear = clear->next;
    }

    if (s == companion) {
        // find the path
        companion->S_phi->in_path = is_path_expr(t, companion->S_phi->expr);
        return companion->S_phi->in_path;
    } else {
        int res = 0;
        if ((res = find_path(t, s->parent, companion))) {
            res = 0;
            tableaux_expr_list_t *el = s->node->expr_list;
            while(el) {
                el->in_path = res =
                    ((el->generating_expr[0] && el->generating_expr[0]->in_path) ||
                     (el->generating_expr[1] && el->generating_expr[1]->in_path) );
                el = el->next;
            }
        }
        return res;
    }
}

/* translate the syntax tree into a mu formula */
ltsmin_expr_t tableaux_translate_syntax_tree(tableaux_t* t, syntax_tree_t *s)
{
    ltsmin_expr_t res = NULL;
    int connect_op = 0;
    int fixpoint_op1 = 0;
    int fixpoint_op2 = 0;

    switch(s->node->quantifier)
    {
        case NODE_EXIST:
            connect_op = MU_AND;
            fixpoint_op1 = MU_NU;
            fixpoint_op2 = MU_MU;
            break;
        case NODE_ALL:
            connect_op = MU_OR;
            fixpoint_op1 = MU_MU;
            fixpoint_op2 = MU_NU;
            break;

        case NODE_NONE:
            return ctl_star_to_mu_1(s->node->expr_list->expr);
    }
    // for NODE_ALL/NODE_EXIST
    ltsmin_expr_t mu_left = NULL;
    ltsmin_expr_t mu_right = NULL;


    // handle companion/non-terminal nodes
    if (s->is_companion) {
        ltsmin_expr_t mu_nu_list = NULL;

        tableaux_expr_list_t *el = s->node->expr_list;

        // fixed variables
        s->path_var[0] = t->used_var++;

        while(el) {
            ltsmin_expr_t el_expr = NULL;

            // new path variables
            s->path_var[1] = t->used_var++;
            s->S_phi = el;

            // translate everything below
            if (s->branch[ST_LEFT]) {
                mu_left = tableaux_translate_syntax_tree(t, s->branch[ST_LEFT]);
            }

            if (s->branch[ST_RIGHT]) {
                mu_right = tableaux_translate_syntax_tree(t, s->branch[ST_RIGHT]);
            }
            // end nu
            switch(s->label) {
                case TABLEAUX_TERMINAL:
                    // error:
                    // it isn't a terminal node if is_companion is set here
                    Abort("encountered terminal with is_companion set");
                    break;
                case TABLEAUX_PRETERMINAL:
                    // error:
                    // it isn't a terminal node if is_companion is set here
                    Abort("encountered preterminal with is_companion set");
                    break;
                case TABLEAUX_IDENTITY:
                    el_expr = mu_left;
                    break;
                case TABLEAUX_NOT: break; // UNUSED
                case TABLEAUX_AND:
                    el_expr = LTSminExpr(BINARY_OP, MU_AND, 0, mu_left, mu_right);
                    break;
                case TABLEAUX_OR:
                    el_expr = LTSminExpr(BINARY_OP, MU_OR, 0, mu_left, mu_right);
                    break;
                case TABLEAUX_ALL_NEXT:
                    el_expr = LTSminExpr(UNARY_OP, MU_ALL, 0, LTSminExpr(UNARY_OP, MU_NEXT, 0, mu_left, mu_right), NULL);
                    break;
                case TABLEAUX_EXIST_NEXT:
                    el_expr = LTSminExpr(UNARY_OP, MU_EXIST, 0, LTSminExpr(UNARY_OP, MU_NEXT, 0, mu_left, mu_right), NULL);
                    break;
            }

            // build nu
            ltsmin_expr_t nu = LTSminExpr(fixpoint_op2, fixpoint_op2, s->path_var[1], el_expr, NULL);

            // build nu list
            if (s->node->expr_list != el) {
                mu_nu_list = LTSminExpr(BINARY_OP, connect_op, 0, mu_nu_list, nu);
            } else {
                mu_nu_list = nu;
            }
            el = el->next;
        }

        res = LTSminExpr(fixpoint_op1, fixpoint_op1, s->path_var[0], mu_nu_list, NULL);
    } else {
        // translate everything below
        if (s->branch[ST_LEFT]) {
            mu_left = tableaux_translate_syntax_tree(t, s->branch[ST_LEFT]);
        }

        if (s->branch[ST_RIGHT]) {
            mu_right = tableaux_translate_syntax_tree(t, s->branch[ST_RIGHT]);
        }

        // end nu
        switch(s->label) {
            case TABLEAUX_TERMINAL:
                // is this correct?
                res = ctl_star_to_mu_1(s->node->expr_list->expr); // 1 expression max?
                break;
            case TABLEAUX_PRETERMINAL:
                {
                    int path_var_idx = 0;

                    char buf[4096];
                    char buf1[4096];
                    tableaux_node_print(s->node, buf);
                    tableaux_node_print(s->companion->node, buf1);
                    Warning(infoLong, "encountered preterminal %s with companion %s", buf, buf1);
                    ltsmin_expr_print_ctl(s->companion->S_phi->expr, buf);
                    if (s->node->quantifier == NODE_ALL) {
                        path_var_idx = find_path(NU_PATH, s, s->companion);
                        Warning(infoLong, " nu-path exist from %s to %s? %s", buf, buf, path_var_idx ? "yes" : "no");
                    } else {
                        path_var_idx = find_path(MU_PATH, s, s->companion);
                        Warning(infoLong, " mu-path exist from %s to %s? %s", buf, buf, path_var_idx ? "yes" : "no");
                    }

                    res = LTSminExpr((ltsmin_expr_case)MU_VAR, MU_VAR, s->companion->path_var[path_var_idx], mu_left, mu_right);
                }
                break;
            case TABLEAUX_IDENTITY:
                res = mu_left;
                break;
            case TABLEAUX_NOT: break; // UNUSED
            case TABLEAUX_AND:
                res = LTSminExpr(BINARY_OP, MU_AND, 0, mu_left, mu_right);
                break;
            case TABLEAUX_OR:
                res = LTSminExpr(BINARY_OP, MU_OR, 0, mu_left, mu_right);
                break;
            case TABLEAUX_ALL_NEXT:
                res = LTSminExpr(UNARY_OP, MU_ALL, 0, LTSminExpr(UNARY_OP, MU_NEXT, 0, mu_left, mu_right), NULL);
                break;
            case TABLEAUX_EXIST_NEXT:
                res = LTSminExpr(UNARY_OP, MU_EXIST, 0, LTSminExpr(UNARY_OP, MU_NEXT, 0, mu_left, mu_right), NULL);
                break;
        }
    }
    return res;
}

/* convert ctl* formula to mu calculus */
ltsmin_expr_t ctl_star_to_mu(ltsmin_expr_t in)
{
    char buf[4096];
    tableaux_t *t = tableaux_create();

    // step 1: convert ctl_star formula to tableaux start node
    Warning(infoLong, "-- conversion --");
    //tableaux_node_t *n = build_test_node(t);
    tableaux_node_t *n = build_initial_node(t,in);
    t->root.node = n;

    // step 2: calculate the tableaux
    tableaux_build_syntax_tree(t, &t->root);

    // debug print this
    if (log_active(infoLong)) tableaux_print(t);

    // step 3: translate the tableaux
    ltsmin_expr_t mu = tableaux_translate_syntax_tree(t, &t->root);
    if (log_active(infoLong)) ltsmin_expr_print_mu(mu, buf);
    Warning(infoLong, "** %s **", buf);

    tableaux_destroy(t);
    Warning(infoLong, "-- end conversion --");

    return mu;
}

/* for debugging */
ltsmin_expr_t print_expr_1(ltsmin_expr_t in, int indent)
{
    for(int i=0; i < indent; i++) {
        printf("\t");
    }
    printf("[%d/%d/%x]\n", in->token, in->idx, in->hash);
    if (in->arg1) print_expr_1(in->arg1, indent+1);
    if (in->arg2) print_expr_1(in->arg2, indent+1);
    printf("\n");
    return in;
}

ltsmin_expr_t print_expr(ltsmin_expr_t in)
{
    print_expr_1(in, 0);
    return in;
}

/* Comments:
 * De-Morgan for mu-/nu-formula:
 * ! (mu Y . p(Y) ) = (nu Y . !p(!Y) )
 * ! (nu Y . p(Y) ) = (mu Y . !p(!Y) )
 */
