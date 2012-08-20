// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#include <hre/config.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <hre/user.h>
#include <lts-io/provider.h>
#include <lts-lib/lts.h>
#include <lts-lib/set.h>
#include <util-lib/bitset.h>
#include <hre/stringindex.h>
#include <util-lib/tables.h>

#define UNDEF ((uint32_t)-1)
#define MKDET_BLOCK_SIZE 4096
#define MKDET_MONITOR_INTERVAL 10000

static int fully_expored;
static bitset_t master;

static int mkdet_next_lab(lts_t orig,uint32_t l,int S){
	int T,s;
	if (S==EMPTY_SET) return EMPTY_SET;
	T=mkdet_next_lab(orig,l,SetGetParent(S));
	s=SetGetDest(S);
	for(uint32_t i=orig->begin[s];i<orig->begin[s+1];i++){
		if(orig->label[i]==l){
			T=SetInsert(T,0,orig->dest[i]);
		}
	}
	return T;
}

static void mkdet_dfs_lab(lts_t orig,int S,lts_t lts,int*scount,int*tcount,int*tmax){
	int T,U;

	if(SetGetTag(S)==-1){
		SetSetTag(S,*scount);
		(*scount)++;
		bitset_t done=bitset_create_shared(master);
		Debug("exploring set %d (%d)",S,SetGetTag(S));
		for(T=S;T!=EMPTY_SET;T=SetGetParent(T)){
		    int state=SetGetDest(T);
		    Debug("scanning state %u",state);
		    for(uint32_t i=orig->begin[state];i<orig->begin[state+1];i++){
		        if (bitset_set(done,orig->label[i])){
		            Debug("found new edge");
		            U=mkdet_next_lab(orig,orig->label[i],T);
		            mkdet_dfs_lab(orig,U,lts,scount,tcount,tmax);
				    if ((*tcount)==(*tmax)) {
					    (*tmax)+=MKDET_BLOCK_SIZE;
					    lts_set_size(lts,lts->root_count,1,(*tmax));
				    }
    				lts->src[*tcount]=SetGetTag(S);
    				lts->label[*tcount]=orig->label[i];
    				lts->dest[*tcount]=SetGetTag(U);		            
				    (*tcount)++;
				    if (((*tcount)%MKDET_MONITOR_INTERVAL)==0){
					    Print(infoShort,"visited %d states and %d transitions, fully expored %d states",
						    *scount,*tcount,fully_expored);
				    }
		        }
		    }
		}
		fully_expored++;
		bitset_destroy(done);
	}
}

static int mkdet_next_prop(lts_t orig,uint32_t p,int S){
	int T,s;
	if (S==EMPTY_SET) return EMPTY_SET;
	T=mkdet_next_prop(orig,p,SetGetParent(S));
	s=SetGetDest(S);
	for(uint32_t i=orig->begin[s];i<orig->begin[s+1];i++){
		if(orig->properties[orig->dest[i]]==p){
			T=SetInsert(T,0,orig->dest[i]);
		}
	}
	return T;
}

static void mkdet_dfs_prop(lts_t orig,int S,lts_t lts,int*scount,int*tcount,int*smax,int*tmax){
	int T,U;

	if(SetGetTag(S)==-1){
		SetSetTag(S,*scount);
	    if ((*scount)==(*smax)) {
		    (*smax)+=MKDET_BLOCK_SIZE;
		    lts_set_size(lts,lts->root_count,(*smax),(*tmax));
	    }
		lts->properties[*scount]=orig->properties[SetGetDest(S)];
		(*scount)++;
		bitset_t done=bitset_create_shared(master);
		Debug("exploring set %d (%d)",S,SetGetTag(S));
		for(T=S;T!=EMPTY_SET;T=SetGetParent(T)){
		    int state=SetGetDest(T);
		    Debug("scanning state %u",state);
		    for(uint32_t i=orig->begin[state];i<orig->begin[state+1];i++){
		        uint32_t p=orig->properties[orig->dest[i]];
		        if (bitset_set(done,p)){
		            Debug("found new edge");
		            U=mkdet_next_prop(orig,p,T);
		            mkdet_dfs_prop(orig,U,lts,scount,tcount,smax,tmax);
				    if ((*tcount)==(*tmax)) {
					    (*tmax)+=MKDET_BLOCK_SIZE;
					    lts_set_size(lts,lts->root_count,(*smax),(*tmax));
				    }
    				lts->src[*tcount]=SetGetTag(S);
    				lts->dest[*tcount]=SetGetTag(U);		            
				    (*tcount)++;
				    if (((*tcount)%MKDET_MONITOR_INTERVAL)==0){
					    Print(infoShort,"visited %d states and %d transitions, fully expored %d states",
						    *scount,*tcount,fully_expored);
				    }
		        }
		    }
		}
		fully_expored++;
		bitset_destroy(done);
	}
}

void lts_mkdet(lts_t lts){
    master=bitset_create(256,256);
    int has_labels=lts->label!=NULL;
    int has_props=lts->properties!=NULL;
    uint32_t i;
	int tcount,smax,tmax,scount;
	lts_t orig;

    Debug("copying LTS");
	orig=lts_create();
	lts_set_sig(orig,lts->ltstype);
	lts_set_type(orig,LTS_BLOCK);
	lts_set_type(lts,LTS_BLOCK);
	lts_set_size(orig,lts->root_count,lts->states,lts->transitions);
    Debug("initial states");
	for(i=0;i<lts->root_count;i++){
	    orig->root_list[i]=lts->root_list[i];
	}
    Debug("properties and begin");
	for(i=0;i<=lts->states;i++) {
		orig->begin[i]=lts->begin[i];
		if(has_props) orig->properties[i]=lts->properties[i];
	}
    Debug("edges");
	for(i=0;i<lts->transitions;i++){
		if(has_labels) orig->label[i]=lts->label[i];
		orig->dest[i]=lts->dest[i];
	}
    Debug("computing initial state");
	SetClear(-1);
	tmax=MKDET_BLOCK_SIZE;
	tcount=0;
	scount=0;
	lts_set_type(lts,LTS_LIST);
	lts_set_size(lts,lts->root_count,1,tmax);
	fully_expored=0;
	int S=EMPTY_SET;
	for(i=0;i<orig->root_count;i++){
	    S=SetInsert(S,0,orig->root_list[i]);
	}
	Debug("performing reachability");
	if (has_labels && has_props) Abort("cannot deal with both labels and properties");
	if(has_labels){
	    for(i=0;i<orig->root_count;i++){
	        int S=SetInsert(EMPTY_SET,0,orig->root_list[i]);
	        mkdet_dfs_lab(orig,S,lts,&scount,&tcount,&tmax);
	        lts->root_list[i]=SetGetTag(S);
	    }
    }
	if(has_props) {
	    smax=tmax;
	    lts_set_size(lts,lts->root_count,smax,tmax);
	    for(i=0;i<orig->root_count;i++){
	        int S=SetInsert(EMPTY_SET,0,orig->root_list[i]);
	        mkdet_dfs_prop(orig,S,lts,&scount,&tcount,&smax,&tmax);
	        lts->root_list[i]=SetGetTag(S);
	    }
    }
    Debug("found %u states and %u transitions",scount,tcount);
	lts_set_size(lts,lts->root_count,scount,tcount);
	lts_free(orig);
	SetFree();
	bitset_destroy(master);
	Debug("cleanup complete");
}

