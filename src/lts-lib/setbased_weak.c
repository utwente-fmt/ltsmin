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

static void dfs_weak_a(lts_t lts,uint32_t tau,int*map,int*newmap,int s,int a,int id){
	uint32_t i,t;
	int sig;
	for(i=lts->begin[s];i<lts->begin[s+1];i++){
		if(lts->label[i]==tau) {
			t=lts->src[i];
			sig=SetInsert(newmap[t],a,id);
			if(sig!=newmap[t]){
				newmap[t]=sig;
				dfs_weak_a(lts,tau,map,newmap,t,a,id);
			}
		}
	}
}

static void dfs_weak_tau(lts_t lts,int tau,int*map,int*newmap,int s,int id){
    uint32_t i,a,t;
	int sig;
	for(i=lts->begin[s];i<lts->begin[s+1];i++){
		a=lts->label[i];
		t=lts->src[i];
		if(a==(uint32_t)tau){
			if (map[t]!=id) {
				sig=SetInsert(newmap[t],tau,id);
				if(sig!=newmap[t]){
					newmap[t]=sig;
					dfs_weak_tau(lts,tau,map,newmap,t,id);
				}
			}
		} else {
			sig=SetInsert(newmap[t],a,id);
			if(sig!=newmap[t]){
				newmap[t]=sig;
				dfs_weak_a(lts,tau,map,newmap,t,a,id);
			}
		}
	}
}

static int weak_essential(int*sig,int*repr,int src,int label,int dest,int tau){
	int set,set2;
	for(set=sig[repr[src]];set!=EMPTY_SET;set=SetGetParent(set)){
		if (SetGetLabel(set)==label){
			for(set2=sig[repr[SetGetDest(set)]];set2!=EMPTY_SET;set2=SetGetParent(set2)){
				if (SetGetLabel(set2)==tau && SetGetDest(set2)==dest) return 0;
			}
		}
	}
	if (label==tau) return 1;
	for(set=sig[repr[src]];set!=EMPTY_SET;set=SetGetParent(set)){
		if (SetGetLabel(set)==tau){
			for(set2=sig[repr[SetGetDest(set)]];set2!=EMPTY_SET;set2=SetGetParent(set2)){
				if (SetGetLabel(set2)==label && SetGetDest(set2)==dest) return 0;
			}
		}
	}
	return 1;
}

void setbased_weak_reduce(lts_t lts){
	int tau,count,i,iter,setcount,set,*repr,t_count,num_states;
	int *map,*newmap;

	tau=lts->tau;
	lts_tau_cycle_elim(lts);
    Print(info,"size after tau cycle elimination is %d states and %d transitions",lts->states,lts->transitions);
	num_states=lts->states;
	map=(int*)malloc(sizeof(int)*lts->states);
	newmap=(int*)malloc(sizeof(int)*lts->states);
	lts_sort(lts);
	lts_set_type(lts,LTS_BLOCK_INV);
	for(i=0;i<num_states;i++){
		map[i]=0;
		newmap[i]=EMPTY_SET;
	}
	count=1;
	iter=0;
	for(;;){
		SetClear(-1);
		iter++;
		for(i=0;i<num_states;i++){
			dfs_weak_tau(lts,tau,map,newmap,i,map[i]);
		}
		SetSetTag(newmap[lts->root_list[0]],0);
		setcount=1;
		for(i=0;i<num_states;i++){
			set=newmap[i];
			if (SetGetTag(set)<0) {
				//fprintf(stderr,"new set:");
				//PrintSet(stderr,set);
				//fprintf(stderr,"\n");
				SetSetTag(set,setcount);
				setcount++;
			}
		}
		Print(info,"count is %d",setcount);
		//for(i=0;i<num_states;i++){
		//	fprintf(stderr,"%d: old %d new %d sig ",i,map[i],SetGetTag(newmap[i]));
		//	SetPrintIndex(stderr,newmap[i],lts->label_string);
		//	fprintf(stderr,"\n");
		//}
		if(count==setcount) break;
		count=setcount;
		for(i=0;i<num_states;i++){
			map[i]=SetGetTag(newmap[i]);
			newmap[i]=EMPTY_SET;
		}
	}
	repr=(int*)malloc(sizeof(int)*count);
	for(i=0;i<count;i++) {
		repr[i]=-1;
	}
	t_count=0;
	for(i=0;i<(int)lts->states;i++){
		if(repr[map[i]]==-1){
			repr[map[i]]=i;
			t_count+=SetGetSize(newmap[i]);
		}
	}
	lts_set_type(lts,LTS_BLOCK);
	lts_set_size(lts,lts->root_count,count,t_count);
	uint32_t r_count=0;
 	for(i=0;i<(int)lts->root_count;i++){
	  uint32_t tmp=map[lts->root_list[i]];
	  uint32_t j=0;
	  for(;j<r_count;j++){
	    if (tmp==lts->root_list[j]){
	      break;
	    }
	  }
	  if (j==r_count){
    	  lts->root_list[r_count]=tmp;
	      r_count++;
      }
	}
	lts->begin[0]=0;
	for(i=0;i<(int)lts->states;i++){
		count=lts->begin[i];
		set=newmap[repr[i]];
		while(set!=EMPTY_SET){
			if(weak_essential(newmap,repr,i,SetGetLabel(set),SetGetDest(set),tau)){
				lts->label[count]=SetGetLabel(set);
				lts->dest[count]=SetGetDest(set);
				count++;
			}
			set=SetGetParent(set);
		}
		lts->begin[i+1]=count;
	}
	lts_set_size(lts,r_count,lts->states,count);
	SetFree();
	free(newmap);
	free(map);
	free(repr);
    Print(info,"set2 reduction took %d iterations",iter);
}


