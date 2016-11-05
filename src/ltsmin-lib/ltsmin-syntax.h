#ifndef LTSMIN_SYNTAX_H
#define LTSMIN_SYNTAX_H

#include <hre-io/stream.h>
#include <util-lib/fast_hash.h>

/**
\brief Operator types.

We have the following operator types:
 - unary prefix
 - unary postfix
 - binary infix
 - ternary infix, using a left and right symbol 
*/
typedef enum {PREFIX,POSTFIX,INFIX,TRI_LEFT,TRI_RIGHT} op_type;

/**
\brief The parser environment with all global variables.
*/
typedef struct ltsmin_parse_env_s *ltsmin_parse_env_t;

extern void ltsmin_parse_stream(int select,ltsmin_parse_env_t env,stream_t stream);

extern ltsmin_parse_env_t LTSminParseEnvCreate();
extern void LTSminParseEnvDestroy(ltsmin_parse_env_t);
extern void LTSminParseEnvReset(ltsmin_parse_env_t);

extern void LTSminKeyword(ltsmin_parse_env_t env, int token,const char* keyword);

extern int LTSminEdgeVarIndex(ltsmin_parse_env_t env,const char* name);
extern const char* LTSminEdgeVarName(ltsmin_parse_env_t env,int idx);

extern int LTSminStateVarIndex(ltsmin_parse_env_t env,const char* name);
extern const char* LTSminStateVarName(ltsmin_parse_env_t env,int idx);

extern int LTSminValueIndex(ltsmin_parse_env_t env,const char* name);

extern int LTSminConstant (ltsmin_parse_env_t env, int token, const char* name);
extern const char* LTSminConstantName (ltsmin_parse_env_t env, int idx);
extern int LTSminConstantIdx(ltsmin_parse_env_t env, const char* name);
extern int LTSminConstantToken(ltsmin_parse_env_t env, int idx);

extern int LTSminPrefixOperator (ltsmin_parse_env_t env, int token, const char* name, int prio);
extern int LTSminPostfixOperator(ltsmin_parse_env_t env, int token, const char* name, int prio);
extern const char* LTSminUnaryName(ltsmin_parse_env_t env, int idx);
extern int LTSminUnaryIdx(ltsmin_parse_env_t env, const char* name);
extern int LTSminUnaryToken(ltsmin_parse_env_t env, int idx);
extern int LTSminUnaryIsPrefix(ltsmin_parse_env_t env, int idx);

extern int LTSminBinaryOperator(ltsmin_parse_env_t env,int token, const char* name,int prio);
extern const char* LTSminBinaryName(ltsmin_parse_env_t env,int idx);
extern int LTSminBinaryIdx(ltsmin_parse_env_t env, const char* name);
extern int LTSminBinaryToken(ltsmin_parse_env_t env,int idx);

extern void Parse(void*,int,int,ltsmin_parse_env_t);
extern void *ParseAlloc(void *(*mallocProc)(size_t));
extern void ParseFree(void *p,void (*freeProc)(void*));

typedef struct ltsmin_expr_s *ltsmin_expr_t;

typedef enum {
    /* common symbols */
    SVAR,
    EVAR,
    INT,
    CHUNK,
    VAR,

    /* predicate language constructs, to avoid enum collisions */
    S_LT,
    S_LEQ,
    S_GT,
    S_GEQ,
    S_EQ,
    S_NEQ,
    S_TRUE,
    S_FALSE,
    S_MAYBE,
    S_NOT,
    S_OR,
    S_AND,
    S_EQUIV,
    S_IMPLY,
    S_MULT,
    S_DIV,
    S_REM,
    S_ADD,
    S_SUB,
    S_EN,

    /* special symbols */
    MU_FIX,
    NU_FIX,
    EXISTS_STEP,
    FORALL_STEP,
    EDGE_EXIST,
    EDGE_ALL,

    /* unary and binary operations */
    BINARY_OP,
    UNARY_OP,
    CONSTANT,
} ltsmin_expr_case;

struct ltsmin_expr_s {
    ltsmin_expr_case    node_type;
    int                 idx;
    int                 token;
    ltsmin_expr_t       arg1;
    ltsmin_expr_t       arg2;
    uint32_t            hash;
    ltsmin_expr_t       parent;
    int                 chunk_cache;
    void*               context;
    void                (*destroy_context)(void* c);
};

extern void   LTSminLogExpr(log_t log,char*msg,ltsmin_expr_t expr,ltsmin_parse_env_t env);
extern size_t LTSminSPrintExpr(char *buf, size_t max_buf,ltsmin_expr_t expr,ltsmin_parse_env_t env);
extern char  *LTSminPrintExpr(ltsmin_expr_t expr,ltsmin_parse_env_t env);

ltsmin_expr_t LTSminExpr(ltsmin_expr_case node_type, int token, int idx, ltsmin_expr_t arg1, ltsmin_expr_t arg2);
int           LTSminExprEq(ltsmin_expr_t expr1, ltsmin_expr_t expr2);
ltsmin_expr_t LTSminExprRehash(ltsmin_expr_t expr);
ltsmin_expr_t LTSminExprClone(ltsmin_expr_t expr);
void          LTSminExprDestroy(ltsmin_expr_t expr, int recurisve);

extern ltsmin_expr_t LTSminExprSibling(ltsmin_expr_t e);

/**
\brief Print the given string as a legal ETF identifier.
 
Illegal characters are escaped with '\'. Moreover if the identifier happens to be a keyword
then the entire keyword is escaped. The matching decoding is currently performed in the
ltsmin lexer.

 */
extern void fprint_ltsmin_ident(FILE* f,char*ident);

#endif
