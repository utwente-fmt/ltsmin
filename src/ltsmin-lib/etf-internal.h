#ifndef ETF_INTERNAL_H
#define ETF_INTERNAL_H

#include <ltsmin-lib/etf-util.h>
#include <util-lib/dynamic-array.h>
#include <hre/stringindex.h>


struct etf_parse_env_s{
	void *parser;
	string_index_t strings;
	etf_model_t model;
	etf_map_t current_map;
	etf_rel_t current_rel;
	int*elements;
	int state_length;
	int edge_labels;
	int current_sort;
	int current_idx;
	string_index_t  map_idx;
	array_manager_t map_manager;
	etf_map_t* maps;
	int *map_sorts;
	string_index_t  set_idx;
	array_manager_t set_manager;
	etf_set_t* sets;
	string_index_t  rel_idx;
	array_manager_t rel_manager;
	etf_rel_t* rels;	
};

struct etf_model_s {
	lts_type_t ltstype; // signature of the model.
	int* initial_state;
	int map_count;
	etf_map_t* map;
	char** map_names;
	char** map_types;
	array_manager_t map_manager;
	int trans_count;
	etf_rel_t *trans;
	array_manager_t trans_manager;
	array_manager_t type_manager;
	char** type_names;
	string_index_t* type_values;
};


typedef struct etf_list_s *etf_list_t;

struct etf_list_s{
	etf_list_t prev;
	int fst;
	int snd;
};

#define INLINE_VALUE 0
#define REFERENCE_VALUE 1

extern etf_list_t ETFlistAppend(etf_list_t list,int fst,int snd);

extern void ETFlistFree(etf_list_t list);

extern int ETFlistLength(etf_list_t list);

/**
\brief Create an uninitialized ETF model.
*/
extern etf_model_t ETFmodelCreate();

extern int ETFgetType(etf_model_t model,const char*sort);

#endif

