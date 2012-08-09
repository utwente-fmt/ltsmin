/*
 * property-semantics.c
 *
 *  Created on: Aug 8, 2012
 *      Author: laarman
 */

#include <ltsmin-lib/ltsmin-parse-env.h>
#include <pins-lib/property-semantics.h>

static void
lookup_type_value (ltsmin_expr_t e, int type,ltsmin_parse_env_t env,model_t m)
{
    chunk c;
    c.data = SIgetC(env->idents,e->idx,(int*)&c.len);
    HREassert (NULL != c.data, "Empty chunk");
    int count = GBchunkCount(m,type);
    e->num = GBchunkPut(m,type,(const chunk)c);
    if (count != GBchunkCount(m,type)) // value was added
        Abort ("Value for identifier '%s' cannot be found in table for type %s.",
               c.data, lts_type_get_state_type(GBgetLTStype(m),type));
    e->lts_type = type;
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
        case PRED_AND:
        case PRED_OR:
        case PRED_EQ:
        case PRED_IMPLY:
        case PRED_EQUIV:
            if (left >= 0) { // type(SVAR)
                if (VAR == ltl->arg2->node_type)
                    lookup_type_value(ltl->arg2, left, env, model);
            } else if (right >= 0) {
                if (VAR == ltl->arg1->node_type)
                    lookup_type_value(ltl->arg1, right, env, model);
            }
        default:
            return -1;
        }
    case UNARY_OP:
        switch(ltl->token) {
        case PRED_NOT:   return ltsmin_expr_lookup_values(ltl->arg1, env, model);
        default:        ltsmin_expr_lookup_values(ltl->arg1, env, model);
                        return -1;
        }
    default:
        switch(ltl->token) {
        case SVAR:
            return lts_type_get_state_typeno(GBgetLTStype(model),ltl->idx);
        default:
            return -1;
        }
    }
}

ltsmin_expr_t
parse_file(const char *file, parse_f parser, model_t model)
{
    lts_type_t ltstype = GBgetLTStype(model);
    ltsmin_parse_env_t env = LTSminParseEnvCreate();
    ltsmin_expr_t expr = parser(file, env, ltstype);
    ltsmin_expr_lookup_values(expr, env, model);
    env->expr = NULL;
    LTSminParseEnvDestroy(env);
    return expr;
}

void
mark_predicate(ltsmin_expr_t e, matrix_t *m)
{
    if (!e) return;
    switch(e->node_type) {
    case BINARY_OP:
        mark_predicate(e->arg1,m);
        mark_predicate(e->arg2,m);
        break;
    case UNARY_OP:
        mark_predicate(e->arg1,m);
        break;
    default:
        switch(e->token) {
        case PRED_TRUE:
        case PRED_FALSE:
        case PRED_NUM:
        case PRED_VAR:
            break;
        case PRED_EQ:
            mark_predicate(e->arg1, m);
            mark_predicate(e->arg2, m);
            break;
        case PRED_SVAR: {
            for(int i=0; i < dm_nrows(m); i++)
                dm_set(m, i, e->idx);
            } break;
        default:
            Abort("unhandled predicate expression in mark_predicate");
        }
        break;
    }
}

void
mark_visible(ltsmin_expr_t e, matrix_t *m, int* group_visibility)
{
    if (!e) return;
    switch(e->node_type) {
    case BINARY_OP:
        mark_visible(e->arg1,m,group_visibility);
        mark_visible(e->arg2,m,group_visibility);
        break;
    case UNARY_OP:
        mark_visible(e->arg1,m,group_visibility);
        break;
    default:
        switch(e->token) {
        case PRED_TRUE:
        case PRED_FALSE:
        case PRED_NUM:
        case PRED_VAR:
            break;
        case PRED_EQ:
            mark_visible(e->arg1, m, group_visibility);
            mark_visible(e->arg2, m, group_visibility);
            break;
        case PRED_SVAR: {
            for(int i=0; i < dm_nrows(m); i++) {
                if (dm_is_set(m, i, e->idx)) {
                    group_visibility[i] = 1;
                }
            }
            } break;
        default:
            Abort("unhandled predicate expression in mark_visible");
        }
        break;
    }
}
