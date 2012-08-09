#include <hre/config.h>

#include <stdio.h>

#include <hre/user.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-grammar.h>
#include <ltsmin-lib/ltsmin-parse-env.h>
#include <ltsmin-lib/ltsmin-lexer.h>
#include <ltsmin-lib/etf-internal.h>
#include <ltsmin-lib/etf-util.h>

/*
static etf_parse_env_t ETFparseEnvCreate(){
	etf_parse_env_t env=RT_NEW(struct etf_parse_env_s);
	env->parser=ParseAlloc (RTmalloc);
	env->strings=SIcreate();
	env->model=ETFmodelCreate();
	env->map_idx=SIcreate();
	env->map_manager=create_manager(8);
	ADD_ARRAY(env->map_manager,env->maps,etf_map_t);
	ADD_ARRAY(env->map_manager,env->map_sorts,int);
	env->set_idx=SIcreate();
	env->set_manager=create_manager(8);
	ADD_ARRAY(env->set_manager,env->sets,etf_set_t);
	env->rel_idx=SIcreate();
	env->rel_manager=create_manager(8);
	ADD_ARRAY(env->rel_manager,env->rels,etf_rel_t);
	return env;
}
*/

etf_model_t etf_parse_file(const char *file){
    FILE *in=fopen( file, "r" );
    if (in == NULL)
        AbortCall ("Unable to open file ``%s''", file);
    ltsmin_parse_env_t env=LTSminParseEnvCreate();
    LTSminKeyword(env,TOKEN_BEGIN,"begin");
    LTSminKeyword(env,TOKEN_END,"end");
    LTSminKeyword(env,TOKEN_STATE,"state");
    LTSminKeyword(env,TOKEN_EDGE,"edge");
    LTSminKeyword(env,TOKEN_INIT,"init");
    LTSminKeyword(env,TOKEN_TRANS,"trans");
    LTSminKeyword(env,TOKEN_SORT,"sort");
    LTSminKeyword(env,TOKEN_MAP,"map");
    LTSminKeyword(env,TOKEN_STAR,"*");
    LTSminKeyword(env,TOKEN_SLASH,"/");
    env->etf=ETFmodelCreate();
    ltsmin_parse_stream(TOKEN_ETF,env,stream_input(in));
    etf_model_t model=env->etf;
    env->etf=NULL;
    //TODO LTSminParseEnvDestroy(env);
    return model;	
}

etf_list_t ETFlistAppend(etf_list_t prev,int fst,int snd){
	etf_list_t list=RT_NEW(struct etf_list_s);
	list->prev=prev;
	list->fst=fst;
	list->snd=snd;
	return list;
}

void ETFlistFree(etf_list_t list){
    if (list==NULL) return;
	if (list->prev) ETFlistFree(list->prev);
	RTfree(list);
}

int ETFlistLength(etf_list_t list){
	int count=0;
	while(list){
		count++;
		list=list->prev;
	}
	return count;
}

