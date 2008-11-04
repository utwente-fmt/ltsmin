
#include "at-map.h"
#include "runtime.h"
#include "chunk-table.h"
#include "aterm2.h"

struct at_map_s {
	chunk_table_t table;
	ATermIndexedSet set;
	int size;
};

at_map_t ATmapCreate(char* name){
	at_map_t map=(at_map_t)RTmalloc(sizeof(struct at_map_s));
	map->table=CTcreate(name);
	map->set=ATindexedSetCreate(1024,75);
	map->size=0;
	return map;
}


static void new_term(void*context,size_t len,void* chunk){
	((char*)chunk)[len]=0;
	ATerm t=ATreadFromString((char*)chunk);
	ATindexedSetPut(((at_map_t)context)->set,t,NULL);
	((at_map_t)context)->size++;
}


/// Translate a term to in integer.
int ATfindIndex(at_map_t map,ATerm t){
	long idx=ATindexedSetGetIndex(map->set,t);
	if (idx>=0) return idx;
	char *tmp=ATwriteToString(t);
	int len=strlen(tmp);
	char chunk[len+1];
	for(int i=0;i<len;i++) chunk[i]=tmp[i];
	chunk[len]=0;
	CTsubmitChunk(map->table,len,chunk,new_term,map);
	return ATindexedSetGetIndex(map->set,t);
}


/// Translate an integer to a term.
ATerm ATfindTerm(at_map_t map,int idx){
	if(idx<map->size) return ATindexedSetGetElem(map->set,idx);
	CTupdateTable(map->table,idx,new_term,map);
	return ATindexedSetGetElem(map->set,idx);
}


