#include <config.h>
#include <stdio.h>
#include <runtime.h>
#include <ltsmin-syntax.h>
#include <ltsmin-grammar.h>
#include <ltsmin-parse-env.h>
#include <ltsmin-lexer.h>
#include <chunk_support.h>
#include <ctype.h>

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

ltsmin_parse_env_t LTSminParseEnvCreate(){
    ltsmin_parse_env_t env=RT_NEW(struct ltsmin_parse_env_s);
    env->values=SIcreate();
    env->state_vars=SIcreate();
    env->edge_vars=SIcreate();
    env->keywords=SIcreate();
    env->idents=SIcreate();
    env->unary_ops=SIcreate();
    env->unary_man=create_manager(32);
    ADD_ARRAY(env->unary_man,env->unary_info,struct op_info);    
    env->binary_ops=SIcreate();
    env->binary_man=create_manager(32);
    ADD_ARRAY(env->binary_man,env->binary_info,struct op_info);
    return env;
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

/*
extern int LTSminPrefixOperator(ltsmin_parse_env_t env,const char* name,int prio);
extern int LTSminPostfixOperator(ltsmin_parse_env_t env,const char* name,int prio);
extern const char* LTSminUnaryName(ltsmin_parse_env_t env,int idx);
*/

int LTSminBinaryOperator(ltsmin_parse_env_t env,const char* name,int prio){
    if (SIlookup(env->binary_ops,name)!=SI_INDEX_FAILED ||
        SIlookup(env->unary_ops,name)!=SI_INDEX_FAILED)
    {
        Fatal(1,error,"operator %s already exists",name);
    }
    int res=SIput(env->binary_ops,name);
    ensure_access(env->binary_man,res);
    switch(prio){
        case 1: env->binary_info[res].token=TOKEN_BIN1; break;
        case 2: env->binary_info[res].token=TOKEN_BIN2; break;
        case 3: env->binary_info[res].token=TOKEN_BIN3; break;
        default: Fatal(1,error,"priority %d is not supported",prio);
    }
    env->binary_info[res].prio=prio;
    return res;
}
const char* LTSminBinaryName(ltsmin_parse_env_t env,int idx){
    return SIget(env->binary_ops,idx);
}

void LTSminPrintExpr(log_t log,ltsmin_parse_env_t env,ltsmin_expr_t expr){
    switch(expr->node_type){
        case VAR:
            log_printf(log,"%s",SIget(env->idents,expr->idx));
            break;
        case SVAR:
            log_printf(log,"%s",SIget(env->state_vars,expr->idx));
            break;
        case EVAR:
            log_printf(log,"%s",SIget(env->edge_vars,expr->idx));
            break;
        case INT:
            log_printf(log,"%d",expr->idx);
            break;
        case CHUNK: {
            chunk c;
            c.data=SIgetC(env->values,expr->idx,(int*)&c.len);
            int len=c.len*2+3;
            char print[len];
            chunk2string(c,len,print);
            log_printf(info,"%s",print);
            break;
        }
        case BINARY_OP: {
            log_printf(log,"(");
            LTSminPrintExpr(log,env,expr->arg1);
            log_printf(log,"%s",SIget(env->binary_ops,expr->idx));
            LTSminPrintExpr(log,env,expr->arg2);
            log_printf(log,")");
            break;
        }
        case UNARY_OP:
            log_printf(log,"(<unary is todo>)");
            break;
        case MU_FIX:
            log_printf(log,"(mu %s.",SIget(env->idents,expr->idx));
            LTSminPrintExpr(log,env,expr->arg1);
            log_printf(log,")");
            break;
        case NU_FIX:
            log_printf(log,"(nu %s.",SIget(env->idents,expr->idx));
            LTSminPrintExpr(log,env,expr->arg1);
            log_printf(log,")");
            break;
        case EXISTS_STEP:
            log_printf(log,"(E ");
            LTSminPrintExpr(log,env,expr->arg1);
            log_printf(log,".");
            LTSminPrintExpr(log,env,expr->arg2);
            log_printf(log,")");
            break;
        case FORALL_STEP:
            log_printf(log,"(A ");
            LTSminPrintExpr(log,env,expr->arg1);
            log_printf(log,".");
            LTSminPrintExpr(log,env,expr->arg2);
            log_printf(log,")");
            break;
    }
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



