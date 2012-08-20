// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#include <hre/config.h>

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdlib.h>

#include <hre/user.h>
#include <lts-lib/lts.h>
#include <lts-io/provider.h>
#include <lts-lib/set.h>
#include <util-lib/bitset.h>
#include <hre/stringindex.h>
#include <util-lib/tables.h>

#define UNDEF ((uint32_t)-1)

/**
Recursively insert a transition in signatures.
 */
static void dfs_insert(lts_t silent,int *newmap,int label,int dest,int state){
	int set;
	set=SetInsert(newmap[state],label,dest);
	if (set != newmap[state]) {
		newmap[state]=set;
		for(uint32_t j=silent->begin[state];j<silent->begin[state+1];j++){
			dfs_insert(silent,newmap,label,dest,silent->src[j]);
		}
	}
}

/**
 */
static lts_t filter_silent(lts_t lts,silent_predicate is_silent,void*silent_context){
    int has_labels=lts->label!=NULL;
	lts_t silent;
	lts_set_type(lts,LTS_LIST);
	silent=lts_create();
	lts_set_sig(silent,lts->ltstype);
	lts_set_type(silent,LTS_LIST);
	lts_set_size(silent,0,lts->states,lts->transitions);
	uint32_t silent_count=0;
	uint32_t visible_count=0;
    for(uint32_t i=0;i<lts->transitions;i++){
        if (is_silent(silent_context,lts,lts->src[i],i,lts->dest[i])){
            silent->src[silent_count]=lts->src[i];
            if(has_labels) silent->label[silent_count]=lts->label[i];
            silent->dest[silent_count]=lts->dest[i];
            silent_count++;
        } else {
            lts->src[visible_count]=lts->src[i];
            if(has_labels) lts->label[visible_count]=lts->label[i];
            lts->dest[visible_count]=lts->dest[i];
            visible_count++;
        }
    }
    lts_set_size(silent,0,lts->states,silent_count);
    lts_set_size(lts,lts->root_count,lts->states,visible_count);
    Print(info,"Split LTS into %u silent steps and %u visible steps",silent_count,visible_count);
    return silent;
}

void lts_silent_compress(lts_t lts,silent_predicate is_silent,void*silent_context){
	lts_t silent;
    int has_labels=lts->label!=NULL;
    int has_props=lts->properties!=NULL;

    /** we could, but are not required to, reduce cycles. */
 
	Debug("Move silent steps to second LTS.");
	silent=filter_silent(lts,is_silent,silent_context);
	
	Debug("Create initial partition");
	lts_set_type(lts,LTS_LIST);
	lts_set_type(silent,LTS_BLOCK_INV);
	uint32_t *map=(uint32_t*)RTmalloc(sizeof(int)*lts->states);
	if(has_props){
	    // TODO: make initial partition an argument.
	    for(uint32_t i=0;i<lts->states;i++){
		    map[i]=lts->properties[i];
	    }
	} else {
	    for(uint32_t i=0;i<lts->states;i++){
		    map[i]=0;
	    }
	}
	
	Debug("perform partition refinement");
	uint32_t *newmap=(uint32_t*)malloc(sizeof(int)*lts->states);
	uint32_t map_count=1;
	int iter=0;
	bitset_t valid=bitset_create(256,256);
	for(;;){
	    iter++;
	    Debug("starting round %d",iter);
	    SetClear(-1);
	    for(uint32_t i=0;i<lts->states;i++){
	        newmap[i]=SetInsert(EMPTY_SET,-1,map[i]);
	    }
	    Debug("propagation fase");
	    for(uint32_t i=0;i<lts->transitions;i++){
	        dfs_insert(silent,(int*)newmap,has_labels?lts->label[i]:0,map[lts->dest[i]],lts->src[i]);
	    }
	    Debug("counting number of blocks");
	    uint32_t old_count=map_count;
	    map_count=0;
	    for(uint32_t i=0;i<lts->states;i++){
	        if (bitset_set(valid,newmap[i])){
	            map_count++;
	        }
	    }
	    bitset_clear_all(valid);
	    Debug("partition %d contains %u blocks",iter,map_count);
	    if (map_count==old_count) break;
	    for(uint32_t i=0;i<lts->states;i++){
	        map[i]=newmap[i];
	    }
	}
	bitset_destroy(valid);
	
	Debug("building reduced LTS");
	string_index_t id_index=SIcreate();
	uint32_t *repr=NULL;
	ADD_ARRAY(SImanager(id_index),repr,uint32_t);
	Debug("transfering initial states");
	uint32_t r_count=0;
	uint32_t t_count=0;
	for(uint32_t i=0;i<lts->root_count;i++){
	    uint32_t root=SIputC(id_index,(const char*)&(map[lts->root_list[i]]),4);
	    if(root==r_count){
	        repr[root]=lts->root_list[i];
	        r_count++;
	        t_count+=SetGetSize(newmap[lts->root_list[i]])-1;
	    }
	}
	Debug("compressed %u initial states to %u",lts->root_count,r_count);
	Debug("processing reachable states");
	uint32_t s_count=r_count;
	for(uint32_t i=0;i<lts->transitions;i++){
	    uint32_t state=SIputC(id_index,(const char*)&(map[lts->dest[i]]),4);
	    if(state==s_count){
	        repr[state]=lts->dest[i];
	        s_count++;
	        t_count+=SetGetSize(newmap[lts->dest[i]])-1;
        }
	}
	Debug("reduced LTS has %u initial states, %u states and %u transitions",r_count,s_count,t_count);
	uint32_t *temp_props=NULL;
	if(has_props){
	    temp_props=(uint32_t*)RTmalloc(4*s_count);
	    for(uint32_t i=0;i<s_count;i++){
	        temp_props[i]=lts->properties[repr[i]];
	    }
	}
	lts_set_size(lts,r_count,s_count,t_count);
	for(uint32_t i=0;i<r_count;i++){
	    lts->root_list[i]=i;
	}
	t_count=0;
	for(uint32_t i=0;i<s_count;i++){
	    int set=newmap[repr[i]];
	    if (has_props) lts->properties[i]=temp_props[i];
		while(set!=EMPTY_SET){
		    int lbl=SetGetLabel(set);
		    int dst=SetGetDest(set);
			set=SetGetParent(set);
			if (lbl==-1) continue;
			lts->src[t_count]=i;
			if (has_labels) lts->label[t_count]=lbl;
			lts->dest[t_count]=SIlookupC(id_index,(const char*)&dst,4);
			t_count++;
		}
	}
	SetFree();
	RTfree(newmap);
	RTfree(map);
	RTfree(repr);
	if(has_props) RTfree(temp_props);
	Debug("reduction completed");
}

