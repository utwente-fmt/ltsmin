// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <hre/user.h>
#include <util-lib/fast_hash.h>
#include <util-lib/treedbs.h>

#define BLOCKSIZE 256
#define INIT_HASH_SIZE 256
#define INIT_HASH_MASK 0x0ff


static inline uint32_t hashvalue(int left,int right) {
    return SuperFastHash(&left, sizeof left, SuperFastHash(&right, sizeof right, 0));
}

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
	
	/** Contains the path to a leaf for every leaf index. Every step is:
	    0 : right here.
	    <0 : go left using -node.
	    >0 : go right using node.
     */
	int **db_tree_path;

	int **db_hash;
	int *db_mask;
	int *db_hash_size;
};


static void resize_hash(treedbs_t dbs,int node){
	int i,hash;

	dbs->db_hash_size[node]*=2;
	dbs->db_mask[node]=(dbs->db_mask[node]<<1)|1;
	if (!(dbs->db_hash[node]=RTrealloc(dbs->db_hash[node],dbs->db_hash_size[node]*sizeof(int))))
		 Abort("realloc hash failed");
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
	if (!(dbs->db_left[node]=RTrealloc(dbs->db_left[node],dbs->db_size[node]*sizeof(int)))
	|| !(dbs->db_right[node]=RTrealloc(dbs->db_right[node],dbs->db_size[node]*sizeof(int)))
	|| !(dbs->db_bucket[node]=RTrealloc(dbs->db_bucket[node],dbs->db_size[node]*sizeof(int)))
	) Abort("realloc data failed");

	//ATwarning("resize of %d to %d succesful",node,db_size[node]);
}

static int db_insert(treedbs_t dbs,int node,int left, int right, int * seen){
	int result,hash;
    *seen=1;

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
        *seen = 0;
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

int TreeDBSlookup_ret(treedbs_t dbs, int *vector, int *ret) {
    return TreeFold_ret(dbs, vector, ret);
}

int TreeDBSlookup(treedbs_t dbs, int *vector) {
    return TreeFold(dbs, vector);
}

int TreeFold(treedbs_t dbs,int *vector) {
    int idx = 0;
    TreeFold_ret(dbs, vector, &idx);
    return idx;
}

int
TreeFold_ret(treedbs_t dbs,int *vector, int *idx)
{
    int nPars=dbs->nPars;
    int seen = -1;
    if (nPars==1) {
        seen = 1;
        //Warning(info,"insert %d",vector[0]);
        if(vector[0]>=dbs->range){
            int old=dbs->range;
            while(vector[0]>=dbs->range) {
                int blk_count=dbs->range/BLOCKSIZE;
                dbs->range+=BLOCKSIZE*((blk_count/2)+1);
            }
            //Warning(info,"resize from %d to %d",old,dbs->range);
            dbs->map=RTrealloc(dbs->map,dbs->range*sizeof(int));
            dbs->rev=RTrealloc(dbs->rev,dbs->range*sizeof(int));
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
            seen = 0;
        }
        //Warning(info,"insert %d as %d",vector[0],dbs->map[vector[0]]);
        *idx = dbs->map[vector[0]];
        return seen;
    } else {
        seen = 0;
        int tmp[nPars];
        for(int i=nPars-1;i>0;i--) {
            int left=dbs->db_tree_left[i];
            int right=dbs->db_tree_right[i];
            tmp[i]=db_insert(dbs,i,
                             (left<nPars)?tmp[left]:vector[left-nPars],
                             (right<nPars)?tmp[right]:vector[right-nPars],
                             &seen);
        }
        *idx = tmp[1];
        return seen;
    }
}

int TreeDBSGet(treedbs_t dbs,int index,int pos){
	int nPars=dbs->nPars;
 	if (nPars==1) {
		return dbs->rev[index];
	} else {
	    Debug("getting pos %d from state %d",pos,index);
	    int tmp=index;
	    for(int i=0;dbs->db_tree_path[pos][i]!=0;i++){
	        Debug("path value %d is %d",i,dbs->db_tree_path[pos][i]);
	        if (dbs->db_tree_path[pos][i]<0){
	            tmp=dbs->db_left[-dbs->db_tree_path[pos][i]][tmp];
	        } else {
	            tmp=dbs->db_right[dbs->db_tree_path[pos][i]][tmp];
	        }
	    }
	    return tmp;
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
	for (int i=begin;i<=middle_end;i++){
	    int j=0;
	    while(dbs->db_tree_path[i-dbs->nPars][j]!=0) j++;
	    dbs->db_tree_path[i-dbs->nPars][j]=-this_node;
	    dbs->db_tree_path[i-dbs->nPars][j+1]=0;
	}
	for (int i=middle_begin;i<=end;i++){
	    int j=0;
	    while(dbs->db_tree_path[i-dbs->nPars][j]!=0) j++;
	    dbs->db_tree_path[i-dbs->nPars][j]=this_node;
	    dbs->db_tree_path[i-dbs->nPars][j+1]=0;
	}
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
	/*
	for(int i=0;i<dbs->nPars;i++){
	    printf("path to %4d:",i);
	    for(int j=0;dbs->db_tree_path[i][j]!=0;j++){
	        printf(" %4d",dbs->db_tree_path[i][j]);
	    }
	    printf("\n");
	}
	*/
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
		dbs->db_tree_path=(int**)RTmalloc(nPars*sizeof(int*));
		
		dbs->db_hash_size=(int*)RTmalloc(nPars*sizeof(int));
		for(int i=1;i<dbs->nPars;i++){
			dbs->db_left[i]=(int*)RTmalloc(BLOCKSIZE*sizeof(int));
			dbs->db_right[i]=(int*)RTmalloc(BLOCKSIZE*sizeof(int));
			dbs->db_bucket[i]=(int*)RTmalloc(BLOCKSIZE*sizeof(int));
			dbs->db_hash[i]=(int*)RTmalloc(INIT_HASH_SIZE*sizeof(int));
		}
		for(int i=0;i<dbs->nPars;i++){
		    dbs->db_tree_path[i]=(int*)RTmalloc(nPars*sizeof(int));
		    dbs->db_tree_path[i][0]=0;
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

void
TreeDBSclear(treedbs_t dbs)
{
    int nPars=dbs->nPars;
    if (nPars==1) {
        dbs->count=0;
        for(int i=0;i<BLOCKSIZE;i++) {
            memset (dbs->map, 0, dbs->range*sizeof(int));
        }
    } else {
        for (int i=1;i<dbs->nPars;i++){
            for(int j=0;j<dbs->db_hash_size[i];j++) {
                dbs->db_hash[i][j] = -1;
            }
            dbs->db_next[i]=0;
        }
    }
}


void TreeDBSfree(treedbs_t dbs){
	if(dbs->nPars==1){
		RTfree(dbs->map);
		RTfree(dbs->rev);
		RTfree(dbs);
	} else {
		for(int i=1;i<dbs->nPars;i++){
			RTfree(dbs->db_left[i]);
			RTfree(dbs->db_right[i]);
			RTfree(dbs->db_bucket[i]);
			RTfree(dbs->db_hash[i]);
	    }
	    for(int i=0;i<dbs->nPars;i++){
			RTfree(dbs->db_tree_path[i]);
		}
		RTfree(dbs->db_left);
		RTfree(dbs->db_right);
		RTfree(dbs->db_bucket);
		RTfree(dbs->db_size);
		RTfree(dbs->db_next);
		RTfree(dbs->db_hash);
		RTfree(dbs->db_mask);
		RTfree(dbs->db_tree_left);
		RTfree(dbs->db_tree_right);
		RTfree(dbs->db_tree_path);
		RTfree(dbs->db_hash_size);
		RTfree(dbs);
	}
}

void TreeDBSstats(treedbs_t dbs) {
    TreeInfo(dbs);
}

static void TreeInfoPrint(int depth,int node,treedbs_t dbs){
	if (node>=dbs->nPars) return;
	char prefix[2*depth+1];
	for(int i=0; i<(2*depth); i++) prefix[i]=' ';
	prefix[2*depth]=0;
	Warning(info,"%s%d: node has %d elements and %d hash slots",prefix,node,dbs->db_next[node],dbs->db_hash_size[node]);
	TreeInfoPrint(depth+1,dbs->db_tree_left[node],dbs);
	TreeInfoPrint(depth+1,dbs->db_tree_right[node],dbs);
}


void TreeInfo(treedbs_t dbs){
	if (dbs->nPars==1){
		Warning(info,"map with %d entries and range of %d elements.",dbs->count,dbs->range);
	} else {
		TreeInfoPrint(0,1,dbs);
	}
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

