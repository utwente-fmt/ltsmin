
#include "at-map.h"
#include "runtime.h"
#include "greybox.h"
#include "aterm2.h"
#include "chunk_support.h"

struct at_map_s {
	model_t model;
	int type_no;
	int size;
	ATerm *int2aterm;
	ATermTable aterm2int;
	void* context;
	pretty_print_t print;
	parse_t parse;
};

at_map_t ATmapCreate(model_t model,int type_no,void* context,pretty_print_t print,parse_t parse){
	at_map_t map=(at_map_t)RTmalloc(sizeof(struct at_map_s));
	map->model=model;
	map->type_no=type_no;
	map->size=1024;
	map->int2aterm=(ATerm*)RTmalloc(map->size*sizeof(ATerm));
	for(int i=0;i<map->size;i++) map->int2aterm[i]=NULL;
	//protect is not needed because the map below keeps the terms locked in memory.
	map->aterm2int=ATtableCreate(1024,50);
	map->context=context;
	map->print=print;
	map->parse=parse?parse:((parse_t)ATreadFromString);
	return map;
}

/// Translate a term to in integer.
int ATfindIndex(at_map_t map,ATerm t){
	ATermInt i=(ATermInt)ATtableGet(map->aterm2int,t);
	if (!i){
		char *tmp;
		if (map->print) {
			tmp=map->print(map->context,t);
		} else {
			tmp=ATwriteToString(t);
		}
		int idx=GBchunkPut(map->model,map->type_no,chunk_str(tmp));
		i=ATmakeInt(idx);
		//Warning(info,"putting %s as %d",tmp,idx);
		ATtablePut(map->aterm2int,t,(ATerm)i);
		while(idx>=map->size){
			int N=map->size;
			map->size=N*2;
			map->int2aterm=(ATerm*)realloc(map->int2aterm,map->size*sizeof(ATerm));
			for(int i=0;i<map->size;i++) map->int2aterm[i]=NULL;
		}
		map->int2aterm[idx]=t;
	}
	return ATgetInt(i);
}


/// Translate an integer to a term.
ATerm ATfindTerm(at_map_t map,int idx){
	if(idx<map->size && map->int2aterm[idx]) return map->int2aterm[idx];
	ATermInt i=ATmakeInt(idx);
	//Warning(info,"missing index %d",idx);
	chunk c=GBchunkGet(map->model,map->type_no,idx);
	if (c.len==0) {
		Fatal(1,error,"lookup of %d failed",idx);
	}
	char s[c.len+1];
	for(unsigned int i=0;i<c.len;i++) s[i]=c.data[i];
	s[c.len]=0;
	ATerm t;
	if (map->parse){
		t=map->parse(map->context,s);
	} else {
		t=ATreadFromString(s);
	}
	ATtablePut(map->aterm2int,t,(ATerm)i);
	while(idx>=map->size){
		int N=map->size;
		map->size=N*2;
		map->int2aterm=(ATerm*)realloc(map->int2aterm,map->size*sizeof(ATerm));
		for(int i=0;i<map->size;i++) map->int2aterm[i]=NULL;
	}
	map->int2aterm[idx]=t;
	return t;
}




