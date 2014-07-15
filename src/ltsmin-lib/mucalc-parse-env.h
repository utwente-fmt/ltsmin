#ifndef MUCALC_PARSE_ENV_H
#define MUCALC_PARSE_ENV_H
#include <hre/config.h>

#include <hre-io/user.h>
#include <ltsmin-lib/mucalc-syntax.h>

/**
\file mucalc-parse-env.h
\brief Internal datastructures for ast/lex/lemon.
 */

#define ENV_BUFFER_SIZE 4096

struct mucalc_parse_env_s
{
    stream_t    input;
    void*       parser; // parser object
    int         lineno;    // line number, maintained by lexer.
    int         linebased; /* if 0 then end of line is ignored as white space.
                              Otherwise an EOL token is generated. */
    int         linepos;
    char        buffer[ENV_BUFFER_SIZE];

    string_index_t  variables; // (secondary) storage for variable identifiers (for context check)
    int             variable_count; // number of fixpoint variables

    string_index_t  ids; // storage for identifiers
    string_index_t  strings; // storage for string values

    array_manager_t propositions_man;
    struct mucalc_proposition* propositions; // storage for propositions
    int             proposition_count; // number of propositions

    array_manager_t action_expressions_man;
    struct mucalc_action_expression* action_expressions; // storage for action expressions
    int             action_expression_count; // number of action expressions

    array_manager_t values_man;
    struct mucalc_value* values; // storage for values
    int             value_count; // number of values

    int             subformula_count; // number of subformulae
    mucalc_expr_t   formula_tree; // root node of the formula
    mucalc_expr_t   true_expr; // unique node for "true"
    mucalc_expr_t   false_expr; // unique node for "false"
    bool error;
};

#endif
