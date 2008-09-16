#include "config.h"
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include "runtime.h"
#include "fast_hash.h"
#include <stdint.h>

#define BLOCKSIZE 65536
#define INIT_HASH_SIZE 65536
#define INIT_HASH_MASK 0x0ffff

//static inline int hashvalue(int left,int right) { return (236487217*left+677435677*right) ; }
/*
static inline uint32_t hashvalue(int left,int right) {
	return SuperFastHash(&left,4,SuperFastHash(&right,4,0));
}
*/

#define hashvalue(left,right) SuperFastHash((void*)&(left),4,SuperFastHash((void*)&(right),4,0))

static int nPars;
static int topcount;

static int **db_left;
static int **db_right;
static int **db_bucket;
static int *db_size;
static int *db_next;
static int *db_tree_left;
static int *db_tree_right;

static int **db_hash;
static int *db_mask;
static int *db_hash_size;


static void resize_hash(int node){
	int i,hash;

	db_hash_size[node]*=2;
	db_mask[node]=(db_mask[node]<<1)|1;
	if (!(db_hash[node]=realloc(db_hash[node],db_hash_size[node]*sizeof(int))))
		 Fatal(0,error,"realloc hash failed");
	for (i=0;i<db_hash_size[node];i++) db_hash[node][i]=-1;
	for (i=0;i<db_next[node];i++) {
		hash=hashvalue(db_left[node][i],db_right[node][i])&db_mask[node];
		db_bucket[node][i]=db_hash[node][hash];
		db_hash[node][hash]=i;
	}
	Warning(info,"rehash of %d to %d succesful",node,db_hash_size[node]);
}

static void resize_data(int node){
	db_size[node]+=BLOCKSIZE;
	if (!(db_left[node]=realloc(db_left[node],db_size[node]*sizeof(int)))
	|| !(db_right[node]=realloc(db_right[node],db_size[node]*sizeof(int)))
	|| !(db_bucket[node]=realloc(db_bucket[node],db_size[node]*sizeof(int)))
	) Fatal(0,error,"realloc data failed");

	//ATwarning("resize of %d to %d succesful",node,db_size[node]);
}

static int db_insert(int node,int left, int right){
	int result,hash;

	hash=hashvalue(left,right)&db_mask[node];
	result=db_hash[node][hash];
	while(result >= 0){
		if ((db_left[node][result]==left)&&(db_right[node][result]==right)) break;
		result=db_bucket[node][result];
	}
	if (result <0) {
		while (!(db_next[node]<db_size[node])) {
			if (!(db_next[node]<db_size[node])) resize_data(node);
			if (db_hash_size[node]<db_size[node]) resize_hash(node);
			hash=hashvalue(left,right)&db_mask[node];
		}
		result=db_next[node];
		db_next[node]++;
		db_left[node][result]=left;
		db_right[node][result]=right;
		db_bucket[node][result]=db_hash[node][hash];
		db_hash[node][hash]=result;
	}
	return result;
}

void Fold(int *dest) {
	int i;
	for (i=nPars-1;i>=topcount;i--) {
		dest[i]=db_insert(i, dest[db_tree_left[i]], dest[db_tree_right[i]]);
	}
}

/*
void FoldHint(int *src,int *dest) {
	int i;
	for (i=nPars-1;i>=1;i--) {
		if(src[i+i]== dest[i+i] && src[i+i+1]==dest[i+i+1]) {
			dest[i]=src[i];
		} else {
			dest[i]=db_insert(i, dest[i+i], dest[i+i+1]);
		}
	}
}
*/

void Unfold(int *src) {
	int i;
	for(i=topcount;i<nPars;i++){
		src[db_tree_left[i]]=db_left[i][src[i]];
		src[db_tree_right[i]]=db_right[i][src[i]];
	}
}

void printStats(void){
	int i,total;

	total=0;
	for(i=topcount;i<nPars;i++){
		Warning(info,"size of database for node %d is %d",i,db_next[i]);
		total+=db_next[i];
	}
	Warning(info,"total number of nodes in database is %d",total);
	//ATwarning("average size of vector %d bits",(total*64)/db_next[1]);
}


static int mktree(int next_node,int begin,int end){
	int middle_end=begin+((end-begin)/2);
	int middle_begin=middle_end+1;
	int this_node=next_node;
	next_node++;
	if (begin==middle_end) {
		db_tree_left[this_node]=begin;
	} else {
		db_tree_left[this_node]=next_node;
		next_node=mktree(next_node,begin,middle_end);
	}
	if (middle_begin==end) {
		db_tree_right[this_node]=end;
	} else {
		db_tree_right[this_node]=next_node;
		next_node=mktree(next_node,middle_begin,end);
	}
	return next_node;
}

static void maketree(void){
	int i;

	if (topcount!=1) {
		for(i=1;i<nPars;i++){
			db_tree_left[i]=i+i;
			db_tree_right[i]=i+i+1;
		}
	} else {
		/* mktree is beter maar kan alleen bij 1 beginnen ! */
		mktree(1,nPars,nPars+nPars-1);
	}
}

void TreeDBSinit(int params,int tc){
	int i,j;
	nPars=params;
	topcount=tc;

	if (!(db_left=(int**)malloc(nPars*sizeof(int*)))
	|| !(db_right=(int**)malloc(nPars*sizeof(int*)))
	|| !(db_bucket=(int**)malloc(nPars*sizeof(int*)))
	|| !(db_size=(int*)malloc(nPars*sizeof(int)))
	|| !(db_next=(int*)malloc(nPars*sizeof(int)))
	|| !(db_hash=(int**)malloc(nPars*sizeof(int*)))
	|| !(db_mask=(int*)malloc(nPars*sizeof(int)))
	|| !(db_tree_left=(int*)malloc(nPars*sizeof(int)))
	|| !(db_tree_right=(int*)malloc(nPars*sizeof(int)))
	|| !(db_hash_size=(int*)malloc(nPars*sizeof(int)))
	) Fatal(0,error,"could not allocate memory");
	for(i=topcount;i<nPars;i++){
		if (!(db_left[i]=(int*)malloc(BLOCKSIZE*sizeof(int)))
		|| !(db_right[i]=(int*)malloc(BLOCKSIZE*sizeof(int)))
		|| !(db_bucket[i]=(int*)malloc(BLOCKSIZE*sizeof(int)))
		|| !(db_hash[i]=(int*)malloc(INIT_HASH_SIZE*sizeof(int)))
		) Fatal(0,error,"could not allocate memory");
	}
	
	maketree();
	for(i=topcount;i<nPars;i++){
		db_size[i]=BLOCKSIZE;
		db_next[i]=0;
		db_hash_size[i]=INIT_HASH_SIZE;
		db_mask[i]=INIT_HASH_MASK;
		for(j=0;j<INIT_HASH_SIZE;j++) {
			db_hash[i][j]=-1;
		}
	}

	//atexit(printStats);
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

