#ifndef LTSMIN_TYPE_SYSTEM_H
#define LTSMIN_TYPE_SYSTEM_H

#include <ltsmin-lib/lts-type.h>
#include <ltsmin-lib/ltsmin-tl.h>

/**
\file ltsmin-type-system.h
*/

/**
\brief Format table entry.

An entry can be either an actual data_format_t,
or an error, the latter case means that an operator can not be applied.
*/
typedef union {
    data_format_t df;
    int error;
} format_table_t ;

/**
\brief DF(a) an entry in the format table for an actual data format.
*/
#define DF(a) {.df = a},

/**
\brief FT_ERROR an entry in the format table for an error.
*/
#define FT_ERROR {.error = -1},

/**
\brief Format table for math operators.

Atomic operators are:
 - *: S_MULT
 - /: S_DIV
 - %: S_REM
 - +: S_ADD
 - -: S_SUB
*/
static const format_table_t MATH_OPS[DATA_FORMAT_SIZE][DATA_FORMAT_SIZE] = {
/* v = LHS, > = RHS         LTStypeDirect   LTStypeRange    LTStypeChunk    LTStypeEnum     LTStypeBool     LTStypeTrilean  LTStypeSInt32 */
/* LTStypeDirect */     {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeRange */      {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeChunk */      {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeEnum */       {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeBool */       {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeTrilean */    {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeSInt32 */     {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        DF(LTStypeSInt32)   },
};

/**
\brief Format table for Boolean operators.

Operators are (non-atomic):
 - ||: PRED_OR, LTL_OR, CTL_OR, MU_OR
 - &&: PRED_AND, LTL_AND, CTL_AND, MU_AND
 - <=>: PRED_EQUIV, LTL_EQUIV, CTL_EQUIV
 - =>: PRED_IMPLIES, LTL_IMPLIES, CTL_IMPLIES
 - R: LTL_RELEASE
 - W: LTL_WEAK_UNTIL
 - U: LTL_UNTIL, CTL_UNTIL
*/
static const format_table_t BOOL_OPS[DATA_FORMAT_SIZE][DATA_FORMAT_SIZE] = {
/* v = LHS, > = RHS         LTStypeDirect   LTStypeRange    LTStypeChunk    LTStypeEnum     LTStypeBool     LTStypeTrilean  LTStypeSInt32 */
/* LTStypeDirect */     {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeRange */      {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeChunk */      {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeEnum */       {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeBool */       {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        DF(LTStypeBool) FT_ERROR        FT_ERROR            },
/* LTStypeTrilean */    {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeSInt32 */     {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
};

/**
\brief Format table for order operators.

Atomic operators are:
 - <: S_L
 - <=: S_LEQ
 - >: S_GT
 - >=: S_GEQ
*/
static const format_table_t ORDER_OPS[DATA_FORMAT_SIZE][DATA_FORMAT_SIZE] = {
/* v = LHS, > = RHS         LTStypeDirect   LTStypeRange    LTStypeChunk    LTStypeEnum     LTStypeBool     LTStypeTrilean  LTStypeSInt32 */
/* LTStypeDirect */     {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeRange */      {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeChunk */      {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeEnum */       {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeBool */       {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeTrilean */    {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeSInt32 */     {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        DF(LTStypeBool)     },
};

/**
\brief Format table for other relational operators.

Atomic operators are:
 - ==: S_EQ
 - !=: S_NEQ
 - ??: S_EN
*/
static const format_table_t REL_OPS[DATA_FORMAT_SIZE][DATA_FORMAT_SIZE] = {
/* v = LHS, > = RHS         LTStypeDirect   LTStypeRange    LTStypeChunk    LTStypeEnum     LTStypeBool     LTStypeTrilean  LTStypeSInt32 */
/* LTStypeDirect */     {   DF(LTStypeBool) DF(LTStypeBool) FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        DF(LTStypeBool)     },
/* LTStypeRange */      {   DF(LTStypeBool) DF(LTStypeBool) FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        DF(LTStypeBool)     },
/* LTStypeChunk */      {   FT_ERROR        FT_ERROR        DF(LTStypeBool) DF(LTStypeBool) FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeEnum */       {   FT_ERROR        FT_ERROR        DF(LTStypeBool) DF(LTStypeBool) FT_ERROR        FT_ERROR        FT_ERROR            },
/* LTStypeBool */       {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        DF(LTStypeBool) DF(LTStypeBool) FT_ERROR            },
/* LTStypeTrilean */    {   FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        DF(LTStypeBool) DF(LTStypeBool) FT_ERROR            },
/* LTStypeSInt32 */     {   DF(LTStypeBool) DF(LTStypeBool) FT_ERROR        FT_ERROR        FT_ERROR        FT_ERROR        DF(LTStypeBool)     },
};

/**
\brief Format table for all unary Boolean operators.
*/
static const format_table_t UNARY_BOOL_OPS[DATA_FORMAT_SIZE] = {
    FT_ERROR            // LTStypeDirect
    FT_ERROR            // LTStypeRange
    FT_ERROR            // LTStypeChunk
    FT_ERROR            // LTStypeEnum
    DF(LTStypeBool)     // LTStypeBool
    FT_ERROR            // LTStypeTrilean
    FT_ERROR            // LTStypeSInt32
};

/**
\brief Check and return the format of an atomic predicate.
*/
data_format_t check_type_format_atom(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type);

/**
\brief Check and return the format of a predicate formula.
*/
data_format_t check_type_format_pred(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type);

/**
\brief Check and return the format of an LTL formula.
*/
data_format_t check_type_format_LTL(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type);

/**
\brief Check and return the format of a CTL formula.
*/
data_format_t check_type_format_CTL(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type);

/**
\brief Check and return the format of a mu-calculus formula.
*/
data_format_t check_type_format_MU(const ltsmin_expr_t e, const ltsmin_parse_env_t env, const lts_type_t lts_type);

/**
\brief Look up the data format in \p f using \p l and \p r.

Aborts if the entry in f is FORMAT_ERROR.
 */
data_format_t get_data_format_binary(
        const format_table_t f[DATA_FORMAT_SIZE][DATA_FORMAT_SIZE], ltsmin_expr_t e,
        ltsmin_parse_env_t env, data_format_t l, data_format_t r);

/**
\brief Look up the data format in \p f using \p c.

Aborts if the entry in f is FORMAT_ERROR.
 */
data_format_t get_data_format_unary(const format_table_t f[DATA_FORMAT_SIZE],
        ltsmin_expr_t e, ltsmin_parse_env_t env, data_format_t c);

/**
\brief return the type number for \p e.

Assumed is that e->token \in {SVAR, EVAR}
*/
int get_typeno(ltsmin_expr_t e, lts_type_t lts_type);

#endif
