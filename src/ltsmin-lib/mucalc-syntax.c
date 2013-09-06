/*
 * mucalc-syntax.c
 *
 *  Created on: 18 Oct 2012
 *      Author: kant
 */

#include <ltsmin-lib/mucalc-syntax.h>
#include <ltsmin-lib/mucalc-grammar.h>
#include <ltsmin-lib/mucalc-parse-env.h>

mucalc_parse_env_t mucalc_parse_env_create()
{
    mucalc_parse_env_t env = RT_NEW(struct mucalc_parse_env_s);
    //env->variables = SIcreate();
    env->variable_count = 0;
    env->ids = SIcreate();
    env->strings = SIcreate();

    env->propositions_man=create_manager(32);
    ADD_ARRAY(env->propositions_man,env->propositions,struct mucalc_proposition);
    env->proposition_count = 0;

    env->action_expressions_man=create_manager(32);
    ADD_ARRAY(env->action_expressions_man,env->action_expressions,struct mucalc_action_expression);
    env->action_expression_count = 0;

    env->values_man=create_manager(32);
    ADD_ARRAY(env->values_man,env->values,struct mucalc_value);
    env->value_count = 0;

    env->subformula_count = 0;
    env->formula_tree = NULL;
    env->true_expr = mucalc_expr_create(env, MUCALC_TRUE, 0, NULL, NULL);
    env->false_expr = mucalc_expr_create(env, MUCALC_FALSE, 0, NULL, NULL);
    env->error = false;
    return env;
}

void mucalc_parse_env_destroy(mucalc_parse_env_t env)
{
    //SIdestroy(&env->variables);
    SIdestroy(&env->ids);
    SIdestroy(&env->strings);
    destroy_manager(env->propositions_man);
    RTfree(env);
    return;
}

void mucalc_parse(void *yyp, int yymajor, int yyminor, mucalc_parse_env_t env)
{
    MucalcParse(yyp, yymajor, yyminor, env);
}

void *mucalc_parse_alloc(void *(*mallocProc)(size_t))
{
    return MucalcParseAlloc(mallocProc);
}

void mucalc_parse_free(void *p, void (*freeProc)(void*))
{
    MucalcParseFree(p, freeProc);
}

mucalc_expr_t mucalc_expr_create(mucalc_parse_env_t env, mucalc_type_enum_t type, int value, mucalc_expr_t arg1, mucalc_expr_t arg2)
{
    mucalc_expr_t e = RT_NEW(struct mucalc_expr_s);
    e->idx = (env->subformula_count++);
    e->type = type;
    e->value = value;
    e->arg1 = arg1;
    e->arg2 = arg2;
    //mucalc_expr_print(env, e);
    return e;
}

void mucalc_expr_destroy(mucalc_expr_t e)
{
    if (e != NULL)
    {
        mucalc_expr_destroy(e->arg1);
    }
    if (e != NULL)
    {
        mucalc_expr_destroy(e->arg2);
    }
    RTfree(e);
    return;
}

int mucalc_add_proposition(mucalc_parse_env_t env, int id, int value)
{
    mucalc_proposition_t proposition;
    proposition.id = id;
    proposition.value = value;
    proposition.state_idx = -1;
    proposition.state_typeno = -1;
    proposition.value_idx = -1;
    for(int i=0; i<env->proposition_count; i++)
    {
        if (env->propositions[i].id==proposition.id
                && env->propositions[i].value==proposition.value)
        {
            return i;
        }
    }
    int proposition_idx = env->proposition_count++;
    ensure_access(env->propositions_man, proposition_idx);
    env->propositions[proposition_idx] = proposition;
    return proposition_idx;
}

int mucalc_add_action_expression(mucalc_parse_env_t env, int value, bool negated)
{
    mucalc_action_expression_t action_expr;
    action_expr.value = value;
    action_expr.negated = negated;
    for(int i=0; i<env->action_expression_count; i++)
    {
        if (env->action_expressions[i].value==action_expr.value
                && env->action_expressions[i].negated==action_expr.negated)
        {
            return i;
        }
    }
    int action_expr_idx = env->action_expression_count++;
    ensure_access(env->action_expressions_man, action_expr_idx);
    env->action_expressions[action_expr_idx] = action_expr;
    return action_expr_idx;
}

int mucalc_add_value(mucalc_parse_env_t env, mucalc_value_type_enum_t type, int value)
{
    for(int i=0; i<env->value_count; i++)
    {
        if (env->values[i].value==value
                && env->values[i].type==type)
        {
            return i;
        }
    }
    int value_idx = env->value_count++;
    ensure_access(env->values_man, value_idx);
    mucalc_value_t value_obj;
    value_obj.type = type;
    value_obj.value = value;
    env->values[value_idx] = value_obj;
    return value_idx;
}


const char* mucalc_type_print(mucalc_type_enum_t type)
{
    switch(type)
    {
        case MUCALC_FORMULA:        return "formula";
        case MUCALC_MU:             return "mu";
        case MUCALC_NU:             return "nu";
        case MUCALC_MUST:           return "must";
        case MUCALC_MAY:            return "may";
        case MUCALC_AND:            return "and";
        case MUCALC_OR:             return "or";
        case MUCALC_NOT:            return "not";
        case MUCALC_TRUE:           return "true";
        case MUCALC_FALSE:          return "false";
        case MUCALC_PROPOSITION:    return "proposition";
        case MUCALC_VAR:            return "var";
        default: Abort("mucalc_type_print: unknown expression type: %d", type);
    }
}

/**
 * FIXME: allocates strings
 */
const char* mucalc_fetch_value(mucalc_parse_env_t env, mucalc_type_enum_t type, int value)
{
    switch(type)
    {
        case MUCALC_VAR:
        case MUCALC_MU:
        case MUCALC_NU:
            return SIget(env->ids, value);
        case MUCALC_PROPOSITION:
        {
            mucalc_proposition_t proposition = env->propositions[value];
            char* id = SIget(env->ids, proposition.id);
            mucalc_value_t value_obj = env->values[proposition.value];
            if (value_obj.type==MUCALC_VALUE_STRING) {
                char* value = SIget(env->strings, value_obj.value);
                int len = strlen(id)+strlen(value)+2;
                char *result = RTmalloc(sizeof(char[len]));
                snprintf(result, len, "%s=%s", id, value);
                return result;
            } else {
                int len = strlen(id)+12+2;
                char *result = RTmalloc(sizeof(char[len]));
                snprintf(result, len, "%s=%d", id, value_obj.value);
                return result;
            }
        }
        case MUCALC_MAY:
        case MUCALC_MUST:
        {
            mucalc_action_expression_t action_expr = env->action_expressions[value];
            // FIXME: incorporate 'negated' field
            if (action_expr.value==-1)
                return "";
            return SIget(env->strings, action_expr.value);
        }
        default:
            return "";
    }
}

/**
 * FIXME: allocates strings
 */
void mucalc_expr_print(mucalc_parse_env_t env, mucalc_expr_t e)
{
    Print(infoLong, "mucalc_expr %d: %s %d %s %d %d",
          e->idx, mucalc_type_print(e->type),
          e->value, mucalc_fetch_value(env, e->type, e->value),
          e->arg1==NULL ? 0 : e->arg1->idx,
          e->arg2==NULL ? 0 : e->arg2->idx);
}
