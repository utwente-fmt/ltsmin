#ifndef LTSMIN_PARSE_ENV_H
#define LTSMIN_PARSE_ENV_H

/**
\file ltsmin-parse-env.h
\brief Internal datastructures for ast/lex/lemon.
 */

#include <hre/stringindex.h>
#include <ltsmin-lib/etf-util.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <util-lib/dynamic-array.h>

struct op_info{
    int pattern;
    int token;
    int prio;
};

#define ENV_BUFFER_SIZE 4096

struct ltsmin_parse_env_s{
    stream_t input;
    void* parser;
    string_index_t values; // integer <-> constant mapping
    string_index_t state_vars; // integer <-> state variable name mapping
    string_index_t edge_vars; // integer <-> edge variable name mapping
    string_index_t keywords; // token <-> keywords mapping
    string_index_t idents; // other identifiers
    string_index_t constant_ops; // integer <-> operator mapping
    array_manager_t constant_man;
    struct op_info *constant_info;
    string_index_t unary_ops; // integer <-> operator mapping
    array_manager_t unary_man;
    struct op_info *unary_info;
    string_index_t binary_ops; // integer <-> operator mapping
    array_manager_t binary_man;
    struct op_info *binary_info;
    int lineno;    //line number, maintained by lexer.
    int linebased; /* if 0 then end of line is ignored as white space. 
                      Otherwise an EOL token is generated. */
    int                 linepos;
    etf_model_t         etf;
    string_index_t      etf_current_idx;
    ltsmin_expr_t       expr;
    char                buffer[ENV_BUFFER_SIZE];
};


/* Parser Priorities:
 * HIGH
 *     postfix priority 1
 *     prefix priority 1
 *     binary priority 1
 *     postfix priority 2
 *     prefix priority 2
 *     binary priority 2
 *     postfix priority 3
 *     prefix priority 3
 *     binary priority 3
 *     postfix priority 4
 *     prefix priority 4
 *     binary priority 4
 *     postfix priority 5
 *     prefix priority 5
 *     binary priority 5
 *     postfix priority 6
 *     prefix priority 6
 *     binary priority 6
 *     postfix priority 7
 *     prefix priority 7
 *     binary priority 7
 *     postfix priority 8
 *     prefix priority 8
 *     binary priority 8
 *     postfix priority 9
 *     prefix priority 9
 *     Quantifier
 * LOW
 */

#endif
