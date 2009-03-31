#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include "runtime.h"
#include "fast_hash.h"
#include <stdint.h>
#include "treedbs.h"

#define BLOCKSIZE 256
#define INIT_HASH_SIZE 256
#define INIT_HASH_MASK 0x0ff

//static inline int hashvalue(int left,int right) { return (236487217*left+677435677*right) ; }
/*
static inline uint32_t hashvalue(int left,int right) {
	return SuperFastHash(&left,4,SuperFastHash(&right,4,0));
}
*/

#define hashvalue(left,right) SuperFastHash((void*)&(left),4,SuperFastHash((void*)&(right),4,0))


struct treedbs_s {
	int nPars;

	int count;
	int range;
	int *map;
	int *rev;

	int **db_left;
	int **db_right;
	int **db_bucket;
	int *db_size;
	int *db_next;
	int *db_tree_left;
	int *db_tree_right;

	int **db_hash;
	int *db_mask;
	int *db_hash_size;
};


static void resize_hash(treedbs_t dbs,int node){
	int i,hash;

	dbs->db_hash_size[node]*=2;
	dbs->db_mask[node]=(dbs->db_mask[node]<<1)|1;
	if (!(dbs->db_hash[node]=realloc(dbs->db_hash[node],dbs->db_hash_size[node]*sizeof(int))))
		 Fatal(0,error,"realloc hash failed");
	for (i=0;i<dbs->db_hash_size[node];i++) dbs->db_hash[node][i]=-1;
	for (i=0;i<dbs->db_next[node];i++) {
		hash=hashvalue(dbs->db_left[node][i],dbs->db_right[node][i])&dbs->db_mask[node];
		dbs->db_bucket[node][i]=dbs->db_hash[node][hash];
		dbs->db_hash[node][hash]=i;
	}
	//Warning(info,"rehash of %d to %d succesful",node,dbs->db_hash_size[node]);
}

static void resize_data(treedbs_t dbs,int node){
	int blk_count=dbs->db_size[node]/BLOCKSIZE;
	dbs->db_size[node]+=BLOCKSIZE*((blk_count/2)+1);
	if (!(dbs->db_left[node]=realloc(dbs->db_left[node],dbs->db_size[node]*sizeof(int)))
	|| !(dbs->db_right[node]=realloc(dbs->db_right[node],dbs->db_size[node]*sizeof(int)))
	|| !(dbs->db_bucket[node]=realloc(dbs->db_bucket[node],dbs->db_size[node]*sizeof(int)))
	) Fatal(0,error,"realloc data failed");

	//ATwarning("resize of %d to %d succesful",node,db_size[node]);
}

static int db_insert(treedbs_t dbs,int node,int left, int right){
	int result,hash;

	hash=hashvalue(left,right)&dbs->db_mask[node];
	result=dbs->db_hash[node][hash];
	while(result >= 0){
		if ((dbs->db_left[node][result]==left)&&(dbs->db_right[node][result]==right)) break;
		result=dbs->db_bucket[node][result];
	}
	if (result <0) {
		while (!(dbs->db_next[node]<dbs->db_size[node])) {
			if (!(dbs->db_next[node]<dbs->db_size[node])) resize_data(dbs,node);
			if (dbs->db_hash_size[node]<dbs->db_size[node]) resize_hash(dbs,node);
			hash=hashvalue(left,right)&dbs->db_mask[node];
		}
		result=dbs->db_next[node];
		dbs->db_next[node]++;
		dbs->db_left[node][result]=left;
		dbs->db_right[node][result]=right;
		dbs->db_bucket[node][result]=dbs->db_hash[node][hash];
		dbs->db_hash[node][hash]=result;
	}
	return result;
}

int TreeCount(treedbs_t dbs){
	int nPars=dbs->nPars;
	if (nPars==1) {
		return dbs->count;
	} else {
		return dbs->db_next[1];
	}
}

int TreeFold(treedbs_t dbs,int *vector){
	int nPars=dbs->nPars;
	if (nPars==1) {
		//Warning(info,"insert %d",vector[0]);
		if(vector[0]>=dbs->range){
			int old=dbs->range;
			while(vector[0]>=dbs->range) {
				int blk_count=dbs->range/BLOCKSIZE;
				dbs->range+=BLOCKSIZE*((blk_count/2)+1);
			}
			//Warning(info,"resize from %d to %d",old,dbs->range);
			dbs->map=realloc(dbs->map,dbs->range*sizeof(int));
			dbs->rev=realloc(dbs->rev,dbs->range*sizeof(int));
			while(old<dbs->range){
				dbs->map[old]=-1;
				dbs->rev[old]=-1;
				old++;
			}
		}
		if (dbs->map[vector[0]]==-1) {
			dbs->map[vector[0]]=dbs->count;
			dbs->rev[dbs->count]=vector[0];
			dbs->count++;
		}
		//Warning(info,"insert %d as %d",vector[0],dbs->map[vector[0]]);
		return dbs->map[vector[0]];
	} else {
		int tmp[nPars];
		for(int i=nPars-1;i>0;i--) {
			int left=dbs->db_tree_left[i];
			int right=dbs->db_tree_right[i];
			tmp[i]=db_insert(dbs,i,
				(left<nPars)?tmp[left]:vector[left-nPars],
				(right<nPars)?tmp[right]:vector[right-nPars]);
		}
		return tmp[1];
	}
}

void TreeUnfold(treedbs_t dbs,int index,int*vector){
	int nPars=dbs->nPars;
	if (nPars==1) {
		vector[0]=dbs->rev[index];
	} else {
		int tmp[nPars];
		tmp[1]=index;
		for(int i=1;i<nPars;i++){
			int left=dbs->db_tree_left[i];
			if (left<nPars){
				tmp[left]=dbs->db_left[i][tmp[i]];
			} else {
				vector[left-nPars]=dbs->db_left[i][tmp[i]];
			}
			int right=dbs->db_tree_right[i];
			if (right<nPars){
				tmp[right]=dbs->db_right[i][tmp[i]];
			} else {
				vector[right-nPars]=dbs->db_right[i][tmp[i]];
			}
		}
	}
}

static int mktree(treedbs_t dbs,int next_node,int begin,int end){
	int middle_end=begin+((end-begin)/2);
	int middle_begin=middle_end+1;
	int this_node=next_node;
	next_node++;
	if (begin==middle_end) {
		dbs->db_tree_left[this_node]=begin;
	} else {
		dbs->db_tree_left[this_node]=next_node;
		next_node=mktree(dbs,next_node,begin,middle_end);
	}
	if (middle_begin==end) {
		dbs->db_tree_right[this_node]=end;
	} else {
		dbs->db_tree_right[this_node]=next_node;
		next_node=mktree(dbs,next_node,middle_begin,end);
	}
	return next_node;
}

static void maketree(treedbs_t dbs){
	mktree(dbs,1,dbs->nPars,dbs->nPars+dbs->nPars-1);
}

treedbs_t TreeDBScreate(int nPars){
	treedbs_t dbs=(treedbs_t)RTmalloc(sizeof(struct treedbs_s));
	dbs->nPars=nPars;
	if (nPars==1) {
		dbs->count=0;
		dbs->range=BLOCKSIZE;
		dbs->map=(int*)RTmalloc(BLOCKSIZE*sizeof(int));
		dbs->rev=(int*)RTmalloc(BLOCKSIZE*sizeof(int));
		for(int i=0;i<BLOCKSIZE;i++) {
			dbs->map[i]=-1;
			dbs->rev[i]=-1;
		}
	} else {
		dbs->db_left=(int**)RTmalloc(nPars*sizeof(int*));
		dbs->db_right=(int**)RTmalloc(nPars*sizeof(int*));
		dbs->db_bucket=(int**)RTmalloc(nPars*sizeof(int*));
		dbs->db_size=(int*)RTmalloc(nPars*sizeof(int));
		dbs->db_next=(int*)RTmalloc(nPars*sizeof(int));
		dbs->db_hash=(int**)RTmalloc(nPars*sizeof(int*));
		dbs->db_mask=(int*)RTmalloc(nPars*sizeof(int));
		dbs->db_tree_left=(int*)RTmalloc(nPars*sizeof(int));
		dbs->db_tree_right=(int*)RTmalloc(nPars*sizeof(int));
		dbs->db_hash_size=(int*)RTmalloc(nPars*sizeof(int));
		for(int i=1;i<dbs->nPars;i++){
			dbs->db_left[i]=(int*)RTmalloc(BLOCKSIZE*sizeof(int));
			dbs->db_right[i]=(int*)RTmalloc(BLOCKSIZE*sizeof(int));
			dbs->db_bucket[i]=(int*)RTmalloc(BLOCKSIZE*sizeof(int));
			dbs->db_hash[i]=(int*)RTmalloc(INIT_HASH_SIZE*sizeof(int));
		}
		maketree(dbs);
		for(int i=1;i<nPars;i++){
			dbs->db_size[i]=BLOCKSIZE;
			dbs->db_next[i]=0;
			dbs->db_hash_size[i]=INIT_HASH_SIZE;
			dbs->db_mask[i]=INIT_HASH_MASK;
			for(int j=0;j<INIT_HASH_SIZE;j++) {
				dbs->db_hash[i][j]=-1;
			}
		}
	}
	return dbs;
}

/*
void WriteDBS(char *pattern){
	char name[1024];
	int i,j;
	FILE *out;

	sprintf(name,pattern,0);
	out=fopen(name,"wb");
	for(i=topcount;i<nPars;i++){
		fwrite32(out,db_tree_left[i]);
		fwrite32(out,db_tree_right[i]);
	}
	for(i=topcount;i<nPars;i++){
		sprintf(name,pattern,i);
		out=fopen(name,"wb");
		for(j=0;j<db_next[i];j++){
			fwrite32(out,db_left[i][j]);
			fwrite32(out,db_right[i][j]);
		}
	}
}
*/

