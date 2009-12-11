#include <config.h>
#include <ltsmin-mu.h>
#include <runtime.h>
#include <ltsmin-grammar.h>
#include <ltsmin-parse-env.h>
#include <ltsmin-lexer.h>

ltsmin_expr_t mu_parse_file(lts_type_t ltstype,const char *file){
    FILE *in=fopen( file, "r" );
    ltsmin_parse_env_t env=LTSminParseEnvCreate();
    int N;
    N=lts_type_get_state_length(ltstype);
    for(int i=0;i<N;i++){
        char*name=lts_type_get_state_name(ltstype,i);
        if(name) LTSminStateVarIndex(env,name);
    }
    N=lts_type_get_state_label_count(ltstype);
    for(int i=0;i<N;i++){
        char*name=lts_type_get_state_label_name(ltstype,i);
        LTSminStateVarIndex(env,name);
    }
    N=lts_type_get_edge_label_count(ltstype);
    for(int i=0;i<N;i++){
        char*name=lts_type_get_edge_label_name(ltstype,i);
        LTSminStateVarIndex(env,name);
    }
    SIputAt(env->keywords,"mu",TOKEN_MU_SYM);
    SIputAt(env->keywords,"nu",TOKEN_NU_SYM);
    SIputAt(env->keywords,"A",TOKEN_ALL_SYM);
    SIputAt(env->keywords,"E",TOKEN_EXISTS_SYM);
    LTSminBinaryOperator(env,"/\\",2);
    LTSminBinaryOperator(env,"\\/",3);
    LTSminBinaryOperator(env,"=",1);

    ltsmin_parse_stream(TOKEN_EXPR,env,stream_input(in));
    ltsmin_expr_t expr=env->expr;
    env->expr=NULL;
    //TODO LTSminParseEnvDestroy(env);
    return expr;	
}
