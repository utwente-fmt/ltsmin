#ifndef LTSMIN_SYNTAX_H
#define LTSMIN_SYNTAX_H

#include <runtime.h>
#include <stringindex.h>
#include <stream.h>

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

extern void LTSminKeyword(ltsmin_parse_env_t env,int token,const char* keyword);

extern int LTSminEdgeVarIndex(ltsmin_parse_env_t env,const char* name);
extern const char* LTSminEdgeVarName(ltsmin_parse_env_t env,int idx);

extern int LTSminStateVarIndex(ltsmin_parse_env_t env,const char* name);
extern const char* LTSminStateVarName(ltsmin_parse_env_t env,int idx);

extern int LTSminPrefixOperator(ltsmin_parse_env_t env,const char* name,int prio);
extern int LTSminPostfixOperator(ltsmin_parse_env_t env,const char* name,int prio);
extern const char* LTSminUnaryName(ltsmin_parse_env_t env,int idx);

extern int LTSminBinaryOperator(ltsmin_parse_env_t env,const char* name,int prio);
extern const char* LTSminBinaryName(ltsmin_parse_env_t env,int idx);

extern void Parse(void*,int,int,ltsmin_parse_env_t);
extern void *ParseAlloc(void *(*mallocProc)(size_t));
extern void ParseFree(void *p,void (*freeProc)(void*));

typedef struct ltsmin_expr_s *ltsmin_expr_t;
typedef enum {SVAR,EVAR,INT,CHUNK,BINARY_OP,UNARY_OP,VAR,MU_FIX,NU_FIX,
EXISTS_STEP,FORALL_STEP} ltsmin_expr_case;
struct ltsmin_expr_s {
    ltsmin_expr_case node_type;
    int idx;
    ltsmin_expr_t arg1;
    ltsmin_expr_t arg2;
};
extern void LTSminPrintExpr(log_t log,ltsmin_parse_env_t env,ltsmin_expr_t expr);

#endif
