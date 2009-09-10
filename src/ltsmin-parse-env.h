#ifndef LTSMIN_PARSE_ENV_H
#define LTSMIN_PARSE_ENV_H

/**
\file ltsmin-parse-env.h
\brief Internal datastructures for ast/lex/lemon.
 */

#include <ltsmin-syntax.h>
#include <dynamic-array.h>
#include <etf-util.h>

struct op_info{
    int token;
    int prio;
};

struct ltsmin_parse_env_s{
    stream_t input;
    void* parser;
    string_index_t values; // integer <-> constant mapping
    string_index_t state_vars; // integer <-> state variable name mapping
    string_index_t edge_vars; // integer <-> edge variable name mapping
    string_index_t keywords; // token <-> keywords mapping
    string_index_t idents; // other identifiers
    string_index_t unary_ops; // integer <-> operator mapping
    array_manager_t unary_man;
    struct op_info *unary_info;
    string_index_t binary_ops; // integer <-> operator mapping
    array_manager_t binary_man;
    struct op_info *binary_info;
    int lineno;    //line number, maintained by lexer.
    int linebased; /* if 0 then end of line is ignored as white space. 
                      Otherwise an EOL token is generated. */
    etf_model_t etf;
    string_index_t etf_current_idx;
    ltsmin_expr_t expr;
};



#endif
