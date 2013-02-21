#include <hre/config.h>

#include <stdio.h>
#include <ctype.h>

#include <hre/user.h>
#include <ltsmin-lib/ltsmin-grammar.h>
#include <ltsmin-lib/ltsmin-parse-env.h> // required for ltsmin-lexer.h!
#include <ltsmin-lib/ltsmin-lexer.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <util-lib/chunk_support.h>

void ltsmin_parse_stream(int select,ltsmin_parse_env_t env,stream_t stream){
    yyscan_t scanner;
    env->input=stream;
    env->parser=ParseAlloc (RTmalloc);
    Parse(env->parser,select,0,env);
    ltsmin_lex_init_extra( env , &scanner );
    ltsmin_lex( scanner );
    ltsmin_lex_destroy(scanner);
    stream_close(&env->input);
    ParseFree( env->parser , RTfree ); 
}

void LTSminKeyword(ltsmin_parse_env_t env,int token,const char* keyword){
    SIputAt(env->keywords,keyword,token);
}

/* set up for mu calculus
    SIputAt(env->keywords,"mu",TOKEN_MU_SYM);
    SIputAt(env->keywords,"nu",TOKEN_NU_SYM);
    SIputAt(env->keywords,"A",TOKEN_ALL_SYM);
    SIputAt(env->keywords,"E",TOKEN_EXISTS_SYM);
    LTSminBinaryOperator(env,"/\\",2);
    LTSminBinaryOperator(env,"\\/",3);
    LTSminBinaryOperator(env,"=",1);
*/

ltsmin_parse_env_t LTSminParseEnvCreate() {
    ltsmin_parse_env_t env=RT_NEW(struct ltsmin_parse_env_s);
    env->values=SIcreate();
    env->state_vars=SIcreate();
    env->edge_vars=SIcreate();
    env->keywords=SIcreate();
    env->idents=SIcreate();
    env->constant_ops=SIcreate();
    env->constant_man=create_manager(32);
    ADD_ARRAY(env->constant_man,env->constant_info,struct op_info);
    env->unary_ops=SIcreate();
    env->unary_man=create_manager(32);
    ADD_ARRAY(env->unary_man,env->unary_info,struct op_info);
    env->binary_ops=SIcreate();
    env->binary_man=create_manager(32);
    ADD_ARRAY(env->binary_man,env->binary_info,struct op_info);
    return env;
}

void LTSminParseEnvDestroy(ltsmin_parse_env_t env) {
    SIdestroy(&env->values);
    SIdestroy(&env->state_vars);
    SIdestroy(&env->edge_vars);
    SIdestroy(&env->keywords);
    SIdestroy(&env->idents);
    SIdestroy(&env->constant_ops);
    destroy_manager(env->constant_man);
    SIdestroy(&env->unary_ops);
    destroy_manager(env->unary_man);
    SIdestroy(&env->binary_ops);
    destroy_manager(env->binary_man);

    RTfree(env);
    return;
}

int LTSminValueIndex(ltsmin_parse_env_t env,const char* name){
    return SIput(env->values,name);
}

int LTSminEdgeVarIndex(ltsmin_parse_env_t env,const char* name){
    return SIput(env->edge_vars,name);
}

const char* LTSminEdgeVarName(ltsmin_parse_env_t env,int idx){
    return SIget(env->edge_vars,idx);
}

int LTSminStateVarIndex(ltsmin_parse_env_t env,const char* name){
    return SIput(env->state_vars,name);
}

const char* LTSminStateVarName(ltsmin_parse_env_t env,int idx){
    return SIget(env->state_vars,idx);
}

int LTSminConstant(ltsmin_parse_env_t env, int token, const char* name){
    if (SIlookup(env->binary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->unary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->constant_ops,name)!=SI_INDEX_FAILED)
    {
        Abort("operator %s already exists",name);
    }
    int res=SIput(env->constant_ops,name);
    ensure_access(env->constant_man,res);
    env->constant_info[res].prio=0;
    env->constant_info[res].token=token;
    return res;
}

const char* LTSminConstantName(ltsmin_parse_env_t env,int idx){
    return SIget(env->constant_ops,idx);
}

int LTSminConstantToken(ltsmin_parse_env_t env, int idx)
{
    ensure_access(env->constant_man,idx);
    return env->constant_info[idx].token;
}

int LTSminPrefixOperator(ltsmin_parse_env_t env, int token, const char* name,int prio){
    if (SIlookup(env->binary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->unary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->constant_ops,name)!=SI_INDEX_FAILED)
    {
        Abort("operator %s already exists",name);
    }
    int res=SIput(env->unary_ops,name);
    ensure_access(env->unary_man,res);
    switch(prio){
        case 1: env->unary_info[res].pattern=TOKEN_PREFIX1; break;
        case 2: env->unary_info[res].pattern=TOKEN_PREFIX2; break;
        case 3: env->unary_info[res].pattern=TOKEN_PREFIX3; break;
        case 4: env->unary_info[res].pattern=TOKEN_PREFIX4; break;
        case 5: env->unary_info[res].pattern=TOKEN_PREFIX5; break;
        case 6: env->unary_info[res].pattern=TOKEN_PREFIX6; break;
        case 7: env->unary_info[res].pattern=TOKEN_PREFIX7; break;
        case 8: env->unary_info[res].pattern=TOKEN_PREFIX8; break;
        case 9: env->unary_info[res].pattern=TOKEN_PREFIX9; break;
        default: Abort("priority %d is not supported",prio);
    }
    env->unary_info[res].prio=prio;
    env->unary_info[res].token=token;
    return res;
}

int LTSminPostfixOperator(ltsmin_parse_env_t env, int token, const char* name,int prio){
    if (SIlookup(env->binary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->unary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->constant_ops,name)!=SI_INDEX_FAILED)
    {
        Abort("operator %s already exists",name);
    }
    int res=SIput(env->unary_ops,name);
    ensure_access(env->unary_man,res);
    switch(prio){
        case 1: env->unary_info[res].pattern=TOKEN_POSTFIX1; break;
        case 2: env->unary_info[res].pattern=TOKEN_POSTFIX2; break;
        case 3: env->unary_info[res].pattern=TOKEN_POSTFIX3; break;
        case 4: env->unary_info[res].pattern=TOKEN_POSTFIX4; break;
        case 5: env->unary_info[res].pattern=TOKEN_POSTFIX5; break;
        case 6: env->unary_info[res].pattern=TOKEN_POSTFIX6; break;
        case 7: env->unary_info[res].pattern=TOKEN_POSTFIX7; break;
        case 8: env->unary_info[res].pattern=TOKEN_POSTFIX8; break;
        case 9: env->unary_info[res].pattern=TOKEN_POSTFIX9; break;
        default: Abort("priority %d is not supported",prio);
    }
    env->unary_info[res].prio=prio;
    env->unary_info[res].token=token;
    return res;
}

const char* LTSminUnaryName(ltsmin_parse_env_t env,int idx){
    return SIget(env->unary_ops,idx);
}

int LTSminUnaryToken(ltsmin_parse_env_t env, int idx)
{
    ensure_access(env->unary_man,idx);
    return env->unary_info[idx].token;
}

int LTSminUnaryIsPrefix(ltsmin_parse_env_t env, int idx) {
    ensure_access(env->unary_man,idx);
    return (env->unary_info[idx].pattern==TOKEN_PREFIX1 ||
            env->unary_info[idx].pattern==TOKEN_PREFIX2 ||
            env->unary_info[idx].pattern==TOKEN_PREFIX3 ||
            env->unary_info[idx].pattern==TOKEN_PREFIX4 ||
            env->unary_info[idx].pattern==TOKEN_PREFIX5 ||
            env->unary_info[idx].pattern==TOKEN_PREFIX6 ||
            env->unary_info[idx].pattern==TOKEN_PREFIX7 ||
            env->unary_info[idx].pattern==TOKEN_PREFIX8 ||
            env->unary_info[idx].pattern==TOKEN_PREFIX9);
}

int LTSminBinaryOperator(ltsmin_parse_env_t env, int token, const char* name,int prio){
    if (SIlookup(env->binary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->unary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->constant_ops,name)!=SI_INDEX_FAILED)
    {
        Abort("operator %s already exists",name);
    }
    int res=SIput(env->binary_ops,name);
    ensure_access(env->binary_man,res);
    switch(prio){
        case 1: env->binary_info[res].pattern=TOKEN_BIN1; break;
        case 2: env->binary_info[res].pattern=TOKEN_BIN2; break;
        case 3: env->binary_info[res].pattern=TOKEN_BIN3; break;
        case 4: env->binary_info[res].pattern=TOKEN_BIN4; break;
        case 5: env->binary_info[res].pattern=TOKEN_BIN5; break;
        case 6: env->binary_info[res].pattern=TOKEN_BIN6; break;
        case 7: env->binary_info[res].pattern=TOKEN_BIN7; break;
        case 8: env->binary_info[res].pattern=TOKEN_BIN8; break;
        default: Abort("priority %d is not supported",prio);
    }
    env->binary_info[res].prio=prio;
    env->binary_info[res].token=token;
    return res;
}
const char* LTSminBinaryName(ltsmin_parse_env_t env,int idx){
    return SIget(env->binary_ops,idx);
}

int LTSminBinaryToken(ltsmin_parse_env_t env, int idx)
{
    ensure_access(env->binary_man,idx);
    return env->binary_info[idx].token;
}

size_t
LTSminSPrintExpr(char *buf, ltsmin_expr_t expr, ltsmin_parse_env_t env)
{
    char *begin = buf;
    switch(expr->node_type){
        case VAR:
            if (expr->num == -1)
                buf += sprintf(buf, "%s",SIget(env->idents,expr->idx));
            else
                buf += sprintf(buf, "%s",SIget(env->idents,expr->idx));
            break;
        case SVAR:
            buf += sprintf(buf, "%s",LTSminStateVarName(env, expr->idx));
            break;
        case EVAR:
            buf += sprintf(buf, "%s",LTSminEdgeVarName(env, expr->idx));
            break;
        case INT:
            buf += sprintf(buf, "%d",expr->idx);
            break;
        case CHUNK: {
            chunk c;
            c.data=SIgetC(env->values,expr->idx,(int*)&c.len);
            char print[c.len*2+6];
            chunk2string(c,sizeof print,print);
            buf += sprintf(buf,"%s",print);
            break;
        }
        case CONSTANT: {
            buf += sprintf(buf, " %s ",LTSminConstantName(env, expr->idx));
            break;
        }
        case BINARY_OP: {
            buf += sprintf(buf, "(");
            buf += LTSminSPrintExpr(buf, expr->arg1, env);
            buf += sprintf(buf, " %s ",LTSminBinaryName(env, expr->idx));
            buf += LTSminSPrintExpr(buf, expr->arg2, env);
            buf += sprintf(buf, ")");
            break;
        }
        case UNARY_OP:
            if (LTSminUnaryIsPrefix(env, expr->idx)) {
                buf += sprintf(buf, "(%s ", LTSminUnaryName(env, expr->idx));
                buf += LTSminSPrintExpr(buf, expr->arg1, env);
                buf += sprintf(buf, ")");
            } else {
                buf += sprintf(buf, "(");
                buf += LTSminSPrintExpr(buf, expr->arg1, env);
                buf += sprintf(buf, "%s )", LTSminUnaryName(env, expr->idx));
            }
            break;
        case MU_FIX:
            buf += sprintf(buf, "(%s %s.",SIget(env->keywords,TOKEN_MU_SYM), SIget(env->idents,expr->idx));
            buf += LTSminSPrintExpr(buf, expr->arg1, env);
            buf += sprintf(buf, ")");
            break;
        case NU_FIX:
            buf += sprintf(buf, "(%s %s.",SIget(env->keywords,TOKEN_NU_SYM), SIget(env->idents,expr->idx));
            buf += LTSminSPrintExpr(buf, expr->arg1, env);
            buf += sprintf(buf, ")");
            break;
        case EXISTS_STEP:
            buf += sprintf(buf, "(%s ", SIget(env->keywords,TOKEN_EXISTS_SYM));
            buf += LTSminSPrintExpr(buf, expr->arg1, env);
            buf += sprintf(buf, ".");
            buf += LTSminSPrintExpr(buf, expr->arg2, env);
            buf += sprintf(buf, ")");
            break;
        case FORALL_STEP:
            buf += sprintf(buf, "(%s ", SIget(env->keywords,TOKEN_ALL_SYM));
            buf += LTSminSPrintExpr(buf, expr->arg1, env);
            buf += sprintf(buf, ".");
            buf += LTSminSPrintExpr(buf, expr->arg2, env);
            buf += sprintf(buf, ")");
            break;
        case EDGE_EXIST:
            buf += sprintf(buf, "(%s%s%s",
                SIget(env->keywords,TOKEN_EDGE_EXIST_LEFT),
                SIget(env->edge_vars,expr->idx),
                SIget(env->keywords,TOKEN_EDGE_EXIST_RIGHT));
            buf += LTSminSPrintExpr(buf, expr->arg1, env);
            buf += sprintf(buf, ")");
            break;
        case EDGE_ALL:
            buf += sprintf(buf, "(%s%s%s",
                SIget(env->keywords,TOKEN_EDGE_ALL_LEFT),
                SIget(env->edge_vars,expr->idx),
                SIget(env->keywords,TOKEN_EDGE_ALL_RIGHT));
            buf += LTSminSPrintExpr(buf, expr->arg1, env);
            buf += sprintf(buf, ")");
            break;
        default: Abort("Unknown expression node");
    }
    return buf - begin;
}

char *
LTSminPrintExpr(ltsmin_expr_t expr, ltsmin_parse_env_t env)
{
    size_t len = LTSminSPrintExpr(env->buffer, expr, env);
    HREassert (len < ENV_BUFFER_SIZE, "Buffer overflow in print expression");
    return env->buffer;
}

void
LTSminLogExpr(log_t log,char*msg,ltsmin_expr_t expr,ltsmin_parse_env_t env)
{
    size_t len = LTSminSPrintExpr(env->buffer, expr, env);
    HREassert (len < ENV_BUFFER_SIZE, "Buffer overflow in print expression");
    Warning(log, "%s%s", msg, env->buffer);
}

ltsmin_expr_t LTSminExpr(ltsmin_expr_case node_type, int token, int idx,
                         ltsmin_expr_t arg1, ltsmin_expr_t arg2)
{
    uint32_t hash[5];
    ltsmin_expr_t E = RT_NEW(struct ltsmin_expr_s);
    E->num = -1;
    hash[0] = E->node_type = node_type;
    hash[1] = E->token = token;
    hash[2] = E->idx = idx;
    E->arg1 = arg1;
    E->arg2 = arg2;
    hash[3] = arg1?arg1->hash:0;
    hash[4] = arg2?arg2->hash:0;
    E->hash = SuperFastHash((const char*)hash, sizeof(hash), 0x0739c2d6);
    return E;
}

ltsmin_expr_t LTSminExprRehash(ltsmin_expr_t expr)
{
    uint32_t hash[5];
    hash[0] = expr->node_type;
    hash[1] = expr->token;
    hash[2] = expr->idx;
    hash[3] = expr->arg1?expr->arg1->hash:0;
    hash[4] = expr->arg2?expr->arg2->hash:0;
    expr->hash = SuperFastHash((const char*)hash, sizeof(hash), 0x0739c2d6);
    return expr;
}

int LTSminExprEq(ltsmin_expr_t expr1, ltsmin_expr_t expr2)
{
    // both NULL
    if (!expr1 && !expr2) return 1;
    // one NULL
    if (!expr1 || !expr2) return 0;
    // compare hash
    if (expr1->hash != expr2->hash) return 0;
    // compare node
    return (expr1->node_type == expr2->node_type) &&
           (expr1->idx == expr2->idx) &&
           (expr1->token == expr2->token) &&
           (LTSminExprEq(expr1->arg1, expr2->arg1)) &&
           (LTSminExprEq(expr1->arg2, expr2->arg2));
}

ltsmin_expr_t LTSminExprClone(ltsmin_expr_t expr)
{
    ltsmin_expr_t e = RT_NEW(struct ltsmin_expr_s);
    memcpy(e, expr, sizeof(struct ltsmin_expr_s));
    if (e->arg1) e->arg1 = LTSminExprClone(e->arg1);
    if (e->arg2) e->arg2 = LTSminExprClone(e->arg2);
    return e;
}

/* assume none of the nodes used in the expression is shared! */
void LTSminExprDestroy(ltsmin_expr_t expr)
{
    if (expr->arg1) LTSminExprDestroy(expr->arg1);
    if (expr->arg2) LTSminExprDestroy(expr->arg2);
    RTfree(expr);
}

static char*keyword[]={"begin","end","state","edge","init","trans","sort","map",NULL};

static int legal_first_char(char c){
    switch(c){
        case 'a'...'z': return 1;
        case 'A'...'Z': return 1;
        default: return 0;
    }
}

static int legal_other_char(char c){
    switch(c){
        case 'a'...'z': return 1;
        case 'A'...'Z': return 1;
        case '0'...'9': return 1;
        case '_' : return 1;
        case '\'' : return 1;
        default: return 0;
    }
}

void fprint_ltsmin_ident(FILE* f,char*ident){
    for(int i=0;keyword[i];i++){
        if (strcmp(keyword[i],ident)==0){
            fprintf(f,"\\%s",ident);
            return;
        }
    }
    int N=strlen(ident);
    fprintf(f,"%s%c",legal_first_char(ident[0])?"":"\\",ident[0]);
    for(int i=1;i<N;i++){
        fprintf(f,"%s%c",legal_other_char(ident[i])?"":"\\",ident[i]);
    }
}



