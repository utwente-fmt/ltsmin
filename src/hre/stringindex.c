// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <string.h>
#include <stdlib.h>

#include <hre/user.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/fast_hash.h>
#include <hre/stringindex.h>

#define DEBUG(...) {}
//define DEBUG Debug

#define DATA_BLOCK_SIZE 256
//define DATA_BLOCK_SIZE 4
#define TABLE_INITIAL 0xff
//define TABLE_INITIAL 0xf
#define FILL_MAX 7
#define FILL_OUTOF 8

#define END_OF_LIST (0x7fffffff)

#define USED(si,i) (((si)->next[i]>=0)&&((si)->next[i]!=END_OF_LIST))

struct stringindex {
    array_manager_t man;
	int free_list;
	int count;
	int *next; // next in bucket and in free list.
	int *len;  // length for bucket and previous in free list.
	char **data;
	int *table;
	int mask;
};

int SIgetRange(string_index_t si){
	return array_size(si->man);
}

int SIgetCount(string_index_t si){
	return si->count;
}

array_manager_t SImanager(string_index_t si){
    return si->man;
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

static void next_resize(void*arg,void*old_array,int old_size,void*new_array,int new_size){
    DEBUG("skip next resize from %d to %d",old_size,new_size);
    (void)arg;
    (void)old_array;
    (void)old_size;
    (void)new_array;
    (void)new_size;
}

static void len_resize(void*arg,void*old_array,int old_size,void*new_array,int new_size){
    DEBUG("extend during len resize from %d to %d",old_size,new_size);
    (void)old_array;
    (void)new_array;
    string_index_t si=(string_index_t)arg;
    expand_free_list(si,old_size,new_size);
	if ((si->mask*FILL_OUTOF)<(si->count*FILL_MAX)){
		int i,current,next,N;
	    uint32_t hash;
    	uint32_t len;
	    int bucket;

		N=si->mask+1;
		DEBUG("resizing table from %d to %d",N,N+N);
		si->mask=(si->mask<<1)+1;
		si->table=(int*)RTrealloc(si->table,(si->mask+1)*sizeof(int));
		for(i=0;i<N;i++){
			current=si->table[i];
			si->table[i]=END_OF_LIST;
			si->table[N+i]=END_OF_LIST;
			while(current!=END_OF_LIST){
				next=si->next[current];
				len=si->len[current];
				hash=SuperFastHash(si->data[current],len,0);
				bucket=hash&si->mask;
				HREassert(bucket==i||bucket==N+i,"error");
				si->next[current]=si->table[bucket];
				si->table[bucket]=current;
				DEBUG("moving %s from %d to %d",si->data[current],i,bucket);
				current=next;
			}
		}
	}   
}

string_index_t SIcreate(){
	int i;
	string_index_t si;
	si=(string_index_t)RTmallocZero(sizeof(struct stringindex));
	si->count=0;
	si->free_list=END_OF_LIST;
	si->man=create_manager(DATA_BLOCK_SIZE);
	//si->next=(int*)RTmalloc(DATA_BLOCK_SIZE*sizeof(int));
	ADD_ARRAY_CB(si->man,si->next,int,next_resize,NULL);
	//si->len=(int*)RTmalloc(DATA_BLOCK_SIZE*sizeof(int));
	ADD_ARRAY_CB(si->man,si->len,int,len_resize,si);
	//si->data=(char**)RTmalloc(DATA_BLOCK_SIZE*sizeof(char*));
	ADD_ARRAY(si->man,si->data,char*);
	//create_free_list(si);
	si->table=(int*)RTmalloc((TABLE_INITIAL+1)*sizeof(int));
	si->mask=TABLE_INITIAL;
	for(i=0;i<=TABLE_INITIAL;i++){
		si->table[i]=END_OF_LIST;
	}
	return si;
};

void SIdestroy(string_index_t *si){
	int i;
    int size=array_size((*si)->man);

	for(i=0;i<size;i++){
		if (USED(*si,i)) RTfree((*si)->data[i]);
	}
	RTfree((*si)->len);
	RTfree((*si)->data);
	RTfree((*si)->next);
	RTfree((*si)->table);
	RTfree(*si);
	*si=NULL;
}

char* SIget(string_index_t si,int i){
    int size=array_size(si->man);
	if(0<=i && i<size && (si->next[i]>=0)) {
		return si->data[i];
	} else {
		return NULL;
	}
}

char* SIgetC(string_index_t si,int i,int *len){
    int size=array_size(si->man);
	if(0<=i && i<size && (si->next[i]>=0)) {
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
		if (si->len[idx]!=len) continue;
		if (0==memcmp(str,si->data[idx],len)) return idx;
	}
	return SI_INDEX_FAILED;
}

int SIlookup(string_index_t si,const char*str){
	return SIlookupC(si,str,strlen(str));
}


static void PutEntry(string_index_t si,const char*str,int s_len,int index){
	uint32_t hash;
	int bucket;

    ensure_access(si->man,index);
	HREassert (si->next[index] < 0, "Cannot put %s at %d: position occupied by %s",
	                                str,index,si->data[index]);
	cut_from_free_list(si,index);
	si->len[index]=s_len;
	si->data[index]=RTmalloc(s_len+1);
	memcpy(si->data[index],str,s_len);
	(si->data[index])[s_len]=0;
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
		idx=si->count;
	} else {
		idx=si->free_list;
	}
	PutEntry(si,str,len,idx);
	return idx;
}

int SIput(string_index_t si,const char*str){
	return SIputC(si,str,strlen(str));
}


void SIputCAt(string_index_t si,const char*str,int len,int pos){
	int idx;

	idx=SIlookupC(si,str,len);
	if (idx==pos) return;
	HREassert (SIget(si,pos)==NULL, "Cannot put %s at %d: position filled",str,pos);
	PutEntry(si,str,len,pos);
}

void SIputAt(string_index_t si,const char*str,int pos){
	SIputCAt(si,str,strlen(str),pos);
}

void SIreset(string_index_t si){
	int i,N;
	N=array_size(si->man);
	for(i=0;i<N;i++) {
		if (USED(si,i)) RTfree(si->data[i]);
	}
	si->count=0;
	si->free_list=END_OF_LIST;
	expand_free_list(si,0,N);
	N=si->mask+1;
	for(i=0;i<N;i++) si->table[i]=END_OF_LIST;
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
			RTfree(si->data[idx]);
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
	SIdeleteC(si,str,strlen(str));
}


