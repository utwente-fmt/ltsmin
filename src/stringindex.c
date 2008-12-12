
#include "config.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "stringindex.h"
#include "runtime.h"
#include "fast_hash.h"

#define DATA_BLOCK_SIZE 4096
//define DATA_BLOCK_SIZE 4
#define TABLE_INITIAL 0xfff
//define TABLE_INITIAL 0xf
#define FILL_MAX 7
#define FILL_OUTOF 8

#define END_OF_LIST (0x7fffffff)

#define USED(si,i) (((si)->next[i]>=0)&&((si)->next[i]!=END_OF_LIST))

struct stringindex {
	int free_list;
	int count;
	int size;
	int *next; // next in bucket and in free list.
	int *len;  // length for bucket and previous in free list.
	char **data;
	int *table;
	int mask;
};

int SIgetRange(string_index_t si){
	return si->size;
}

int SIgetCount(string_index_t si){
	return si->count;
}

static void create_free_list(string_index_t si){
	int i;

	si->free_list=0;
	for(i=0;i<si->size;i++) {
		si->next[i]=~(i+1);
		si->len[i]=(i-1);
	}
	si->next[si->size-1]=~0;
	si->len[0]=(si->size-1);	
}

static void cut_from_free_list(string_index_t si,int index){
	if (si->free_list==index) {
		if (~si->next[index]==index) {
			si->free_list=END_OF_LIST;
			return;
		}
		si->free_list=~si->next[index];
	}
	si->next[si->len[index]]=si->next[index];
	si->len[~si->next[index]]=si->len[index];
}

static void add_to_free_list(string_index_t si,int idx){
	if (si->free_list==END_OF_LIST) {
		si->free_list=idx;
		si->next[idx]=~idx;
		si->len[idx]=idx;
	} else {
		si->next[idx]=~si->free_list;
		si->len[idx]=si->len[si->free_list];
		si->next[si->len[si->free_list]]=~idx;
		si->len[si->free_list]=idx;
		si->free_list=idx;
	}
}


static void expand_free_list(string_index_t si,int old_size,int new_size){
	int i;

	for(i=old_size;i<new_size;i++) {
		si->next[i]=~(i+1);
		si->len[i]=(i-1);
	}
	if (si->free_list==END_OF_LIST) {
		si->free_list=old_size;
		si->next[new_size-1]=~(old_size);
		si->len[old_size]=(new_size-1);
	} else {
		si->next[si->len[si->free_list]]=~old_size;
		si->len[old_size]=si->len[si->free_list];
		si->next[new_size-1]=~(si->free_list);
		si->len[si->free_list]=(new_size-1);
	}
}

string_index_t SIcreate(){
	int i;
	string_index_t si;
	si=(string_index_t)RTmalloc(sizeof(struct stringindex));
	si->count=0;
	si->size=DATA_BLOCK_SIZE;
	si->next=(int*)RTmalloc(DATA_BLOCK_SIZE*sizeof(int));
	si->len=(int*)RTmalloc(DATA_BLOCK_SIZE*sizeof(int));
	si->data=(char**)RTmalloc(DATA_BLOCK_SIZE*sizeof(char*));
	create_free_list(si);
	si->table=(int*)RTmalloc((TABLE_INITIAL+1)*sizeof(int));
	si->mask=TABLE_INITIAL;
	for(i=0;i<=TABLE_INITIAL;i++){
		si->table[i]=END_OF_LIST;
	}
	return si;
};

void SIdestroy(string_index_t *si){
	int i;

	for(i=0;i<(*si)->size;i++){
		if (USED(*si,i)) free((*si)->data[i]);
	}
	free((*si)->len);
	free((*si)->data);
	free((*si)->next);
	free((*si)->table);
	free(*si);
	*si=NULL;
}

char* SIget(string_index_t si,int i){
	if(0<=i && i<si->size && (si->next[i]>=0)) {
		return si->data[i];
	} else {
		return NULL;
	}
}

char* SIgetC(string_index_t si,int i,int *len){
	if(0<=i && i<si->size && (si->next[i]>=0)) {
		if (len) *len=si->len[i];
		return si->data[i];
	} else {
		return NULL;
	}
}

int SIlookupC(string_index_t si,const char*str,int len){
	uint32_t hash;
	int bucket;
	int idx;

	hash=SuperFastHash(str,len,0);
	bucket=hash&si->mask;
	for(idx=si->table[bucket];idx!=END_OF_LIST;idx=si->next[idx]){
		if (0==memcmp(str,si->data[idx],len)) return idx;
	}
	return SI_INDEX_FAILED;
}

int SIlookup(string_index_t si,const char*str){
	return SIlookupC(si,str,strlen(str)+1);
}


static void PutEntry(string_index_t si,const char*str,int s_len,int index){
	int i,current,next,N;
	uint32_t hash;
	uint32_t len;
	int bucket;

	if(index>=si->size){
		int extra1,extra2,old_size,new_size;

		old_size=si->size;
		extra1=1+(index-si->size)/DATA_BLOCK_SIZE;
		extra2=old_size/DATA_BLOCK_SIZE/4;
		new_size=old_size+DATA_BLOCK_SIZE*((extra1>=extra2)?extra1:extra2);
		//fprintf(stderr,"resizing data from %d to %d\n",old_size,new_size);
		si->len=(int*)realloc(si->len,new_size*sizeof(int));
		si->data=(char**)realloc(si->data,new_size*sizeof(char*));
		si->next=(int*)realloc(si->next,new_size*sizeof(int));
		expand_free_list(si,old_size,new_size);
		si->size=new_size;
		if ((si->mask*FILL_OUTOF)<(si->count*FILL_MAX)){
			N=si->mask+1;
			//fprintf(stderr,"resizing table from %d to %d",N,N+N);
			si->mask=(si->mask<<1)+1;
			si->table=(int*)realloc(si->table,(si->mask+1)*sizeof(int));
			for(i=0;i<N;i++){
				current=si->table[i];
				si->table[i]=END_OF_LIST;
				si->table[N+i]=END_OF_LIST;
				while(current!=END_OF_LIST){
					next=si->next[current];
					len=si->len[current];
					hash=SuperFastHash(si->data[current],len,0);
					bucket=hash&si->mask;
					assert(bucket==i||bucket==N+i);
					si->next[current]=si->table[bucket];
					si->table[bucket]=current;
					//fprintf(stderr,"moving %s from %d to %d",si->data[current],i,bucket);
					current=next;
				}
			}
		}
	}
	if (si->next[index]>=0) {
		//fprintf(stderr,"Cannot put %s at %d: position occupied by %s\n",str,index,si->data[index]);
		Fatal(1,error,"Cannot put %s at %d: position occupied by %s",str,index,si->data[index]);
		return;
	}
	cut_from_free_list(si,index);
	si->len[index]=s_len;
	si->data[index]=RTmalloc(s_len);
	memcpy(si->data[index],str,s_len);
	hash=SuperFastHash(str,s_len,0);
	bucket=hash&si->mask;
	si->next[index]=si->table[bucket];
	si->table[bucket]=index;
	si->count++;
}


int SIputC(string_index_t si,const char*str,int len){
	int idx;

	idx=SIlookupC(si,str,len);
	if (idx!=SI_INDEX_FAILED) {
		return idx;
	}
	if (si->free_list==END_OF_LIST){
		idx=si->size;
	} else {
		idx=si->free_list;
	}
	PutEntry(si,str,len,idx);
	return idx;
}

int SIput(string_index_t si,const char*str){
	return SIputC(si,str,strlen(str)+1);
}


void SIputCAt(string_index_t si,const char*str,int len,int pos){
	int idx;

	idx=SIlookupC(si,str,len);
	if (idx==pos) return;
	if (idx!=SI_INDEX_FAILED){
		Fatal(1,error,"Cannot put %s at %d: already at %d",str,pos,idx);
		return;
	}
	PutEntry(si,str,len,pos);
}

void SIputAt(string_index_t si,const char*str,int pos){
	SIputCAt(si,str,strlen(str)+1,pos);
}

void SIreset(string_index_t si){
	int i,N;
	N=si->size;
	for(i=0;i<N;i++) {
		if (USED(si,i)) free(si->data[i]);
	}
	N=si->mask+1;
	for(i=0;i<N;i++) si->table[i]=END_OF_LIST;
	si->count=0;
	create_free_list(si);
}

void SIdeleteC(string_index_t si,const char*str,int len){
	uint32_t hash;
	int bucket;
	int idx,next,deleted;

	hash=SuperFastHash(str,len,0);
	bucket=hash&si->mask;
	idx=si->table[bucket];
	si->table[bucket]=END_OF_LIST;
	while(idx!=END_OF_LIST){
		if (0==strcmp(str,si->data[idx])) {
			deleted=idx;
			free(si->data[idx]);
			si->count--;
			idx=si->next[idx];
			while(idx!=END_OF_LIST){
				next=si->next[idx];
				si->next[idx]=si->table[bucket];
				si->table[bucket]=idx;
				idx=next;
			}
			add_to_free_list(si,deleted);
			return;
		} else {
			next=si->next[idx];
			si->next[idx]=si->table[bucket];
			si->table[bucket]=idx;
			idx=next;
		}
	}
}

void SIdelete(string_index_t si,const char*str){
	SIdeleteC(si,str,strlen(str)+1);
}


