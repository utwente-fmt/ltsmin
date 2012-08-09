#include <hre/config.h>

#include <hre/user.h>
#include <ltsmin-lib/etf-objects.h>
#include <util-lib/treedbs.h>
#include <util-lib/dynamic-array.h>

struct etf_rel_s {
	treedbs_t rel_db;
	int length;
	int labels;
	int iter;
};


etf_rel_t ETFrelCreate(int length,int labels){
	etf_rel_t rel =RT_NEW(struct etf_rel_s);
	rel->length=length;
	rel->labels=labels;
	rel->rel_db=TreeDBScreate(2*length + labels);
	return rel;
}

void ETFrelDestroy(etf_rel_t *rel_p){
	etf_rel_t rel=*rel_p;
	*rel_p=NULL;
	TreeDBSfree(rel->rel_db);
	RTfree(rel);
}

/**
\brief Add a new triple to an ETF relation.
*/
void ETFrelAdd(etf_rel_t rel,int *src,int*dst,int*labels){
	int entry[2*rel->length+rel->labels];
	for(int i=0;i<rel->length;i++){
		entry[2*i]=src[i];
		entry[2*i+1]=dst[i];
	}
	for(int i=0;i<rel->labels;i++){
		entry[2*rel->length+i]=labels[i];
	}
	TreeFold(rel->rel_db,entry);
}

/**
\brief Reset the iterator of an ETF relation.
*/
void ETFrelIterate(etf_rel_t rel){
	rel->iter=0;
}

/**
\brief Get the next triple for the ETF relation.
*/
int ETFrelNext(etf_rel_t rel,int *src,int*dst,int*labels){
	if (rel->iter<TreeCount(rel->rel_db)){
		int entry[2*rel->length+rel->labels];
		TreeUnfold(rel->rel_db,rel->iter,entry);
		for(int i=0;i<rel->length;i++){
			src[i]=entry[2*i];
			dst[i]=entry[2*i+1];
		}
		for(int i=0;i<rel->labels;i++){
			labels[i]=entry[2*rel->length+i];
		}
		rel->iter++;
		return 1;
	} else {
		return 0;
	}
}

int ETFrelCount(etf_rel_t rel){
	return TreeCount(rel->rel_db);
}

void ETFrelInfo(etf_rel_t rel){
	TreeInfo(rel->rel_db);
}

struct etf_set_s {
	treedbs_t state_db;
	int length;
	int iter;
};

etf_set_t ETFsetCreate(int length){
	etf_set_t set=RT_NEW(struct etf_set_s);
	set->state_db=TreeDBScreate(length);
	set->length=length;
	return set;
}

void ETFsetDestroy(etf_set_t *set_p){
	etf_set_t set=*set_p;
	*set_p=NULL;
	RTfree(set);
}

void ETFsetAdd(etf_set_t set,int* state){
	TreeFold(set->state_db,state);
}

void ETFsetIterate(etf_set_t set){
	set->iter=0;
}

int ETFsetNext(etf_set_t set,int *state){
	if (set->iter<TreeCount(set->state_db)){
		TreeUnfold(set->state_db,set->iter,state);
		set->iter++;
		return 1;
	} else {
		return 0;
	}
}

int ETFsetCount(etf_set_t set){
	return TreeCount(set->state_db);
}


struct etf_map_s {
	treedbs_t state_db;
	array_manager_t manager;
	int *data;
	int length;
	int iter;
};

etf_map_t ETFmapCreate(int length){
	etf_map_t map=RT_NEW(struct etf_map_s);
	map->state_db=TreeDBScreate(length);
	map->length=length;
	map->manager=create_manager(sizeof(int));
	ADD_ARRAY(map->manager,map->data,int);
	return map;
}

void ETFmapDestroy(etf_map_t *map_p){
	etf_map_t map=*map_p;
	map_p=NULL;
	RTfree(map->data);
	RTfree(map);
}

void ETFmapAdd(etf_map_t map,int* state,int value){
	// TODO: check if the state/value pair is new.
	int state_no=TreeFold(map->state_db,state);
	ensure_access(map->manager,state_no);
	map->data[state_no]=value;
}

void ETFmapIterate(etf_map_t map){
	map->iter=0;
}

int ETFmapNext(etf_map_t map,int *state,int*val){
	if (map->iter<TreeCount(map->state_db)){
		TreeUnfold(map->state_db,map->iter,state);
		*val=map->data[map->iter];
		map->iter++;
		return 1;
	} else {
		return 0;
	}
}

int ETFmapCount(etf_map_t map){
	return TreeCount(map->state_db) ;
}


