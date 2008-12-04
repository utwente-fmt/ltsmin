
#include "at-map.h"
#include "runtime.h"
#include "greybox.h"
#include "aterm2.h"
#include "chunk_support.h"

struct at_map_s {
	model_t model;
	int type_no;
	ATermTable int2aterm;
	ATermTable aterm2int;
	pretty_print_t print;
	parse_t parse;
};

at_map_t ATmapCreate(model_t model,int type_no,pretty_print_t print,parse_t parse){
	at_map_t map=(at_map_t)RTmalloc(sizeof(struct at_map_s));
	map->model=model;
	map->type_no=type_no;
	map->int2aterm=ATtableCreate(1024,75);
	map->aterm2int=ATtableCreate(1024,75);
	map->print=print?print:ATwriteToString;
	map->parse=parse?parse:((parse_t)ATreadFromString);
	return map;
}

/// Translate a term to in integer.
int ATfindIndex(at_map_t map,ATerm t){
	ATermInt i=(ATermInt)ATtableGet(map->aterm2int,t);
	if (!i){
		char *tmp=map->print(t);
		int idx=GBchunkPut(map->model,map->type_no,chunk_str(tmp));
		i=ATmakeInt(idx);
		//Warning(info,"putting %s as %d",tmp,idx);
		ATtablePut(map->aterm2int,t,(ATerm)i);
		ATtablePut(map->int2aterm,(ATerm)i,t);
	}
	return ATgetInt(i);
}


/// Translate an integer to a term.
ATerm ATfindTerm(at_map_t map,int idx){
	ATermInt i=ATmakeInt(idx);
	ATerm t=ATtableGet(map->int2aterm,(ATerm)i);
	if (!t) {
		//Warning(info,"missing index %d",idx);
		chunk c=GBchunkGet(map->model,map->type_no,idx);
		if (c.len==0) {
			Fatal(1,error,"lookup of %d failed",idx);
		}
		char s[c.len+1];
		for(unsigned int i=0;i<c.len;i++) s[i]=c.data[i];
		s[c.len]=0;
		t=map->parse(s);
		ATtablePut(map->aterm2int,t,(ATerm)i);
		ATtablePut(map->int2aterm,(ATerm)i,t);
	}
	return t;
}




