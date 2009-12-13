#include <config.h>
#include "bitset.h"
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include "balloc.h"
#include <stdint.h>
#include <runtime.h>

typedef unsigned long long int word_t;

#define WORD_CLASS ((sizeof(word_t)==4)?5:\
		    ((sizeof(word_t)==8)?6:\
		     ((sizeof(word_t)==16)?7:\
                      (fprintf(stderr,"strange size for type word_t\n"),exit(1),0)\
		   )))

#define PTR_CLASS ((sizeof(void*)==4)?2:\
		    ((sizeof(void*)==8)?3:\
                      (fprintf(stderr,"strange size for type void*\n"),exit(1),0)\
		   ))


#define WORD_MASK ((1<<WORD_CLASS)-1)


struct bitset {
	element_t max; // the least element for which the default applies.
	void *set; // tree of specific values.
	void *default_value; // the default: either ALL_ONES or ALL_ZEROS.
	int node_class; // ilog2(node_size);
	int node_size; // number of children of an internal node. power of 2.
	int base_class; // the number of bits in a leaf is 2^base_class.
	int depth; // the depth of the specific value tree.
	allocater_t node_alloc;
	allocater_t base_alloc;	
};


#define ALL_ZERO ((void*)1)
#define ALL_ONES ((void*)2)

static int ilog2(int n){
    if (n<1) Fatal(1,error,"argument is not a power of 2");
    int i=0;
    while(n!=1){
        if (n&1) Fatal(1,error,"argument is not a power of 2");
        i++;
        n=n>>1;
    }
    return i;
}

bitset_t bitset_create(int node_size,int leaf_size){
    int node_class=ilog2(node_size)-PTR_CLASS;
    if (node_class==0) Fatal(1,error,"node too small for two pointers");
    int base_class=ilog2(leaf_size)+3; // one byte is 8 bits
    if (base_class < WORD_CLASS) Fatal(1,error,"leaf smaller than word size");
    bitset_t set=RT_NEW(struct bitset);
	set->max=(((element_t)1)<<base_class)-1;
	set->set=ALL_ZERO;
	set->default_value=ALL_ZERO;
	set->node_class=node_class;
	set->node_size=node_size>>PTR_CLASS;
	set->base_class=base_class;
	set->depth=0;
	set->node_alloc=BAcreate(node_size,1024*1024);
	if (set->node_alloc==NULL){
		free(set);
		return NULL;
	}
	set->base_alloc=BAcreate(leaf_size,1024*1024);
	if (set->base_alloc==NULL){
		free(set->node_alloc);
		free(set);
		return NULL;
	}
	return set;
}

bitset_t bitset_create_shared(bitset_t set){
    bitset_t newset=RT_NEW(struct bitset);
	newset->max=(((element_t)1)<<set->base_class)-1;
	newset->set=ALL_ZERO;
	newset->default_value=ALL_ZERO;
	newset->node_class=set->node_class;
	newset->node_size=set->node_size;
	newset->base_class=set->base_class;
	newset->depth=0;
	newset->node_alloc=set->node_alloc;
	BAaddref(newset->node_alloc);
	newset->base_alloc=set->base_alloc;
	BAaddref(newset->base_alloc);
	return newset;
}

static void free_set(bitset_t main,int depth,void *set){
	if (set==ALL_ZERO) return;
	if (set==ALL_ONES) return;
	if (depth){
		int i;
		depth--;
		for(i=0;i<main->node_size;i++){
			free_set(main,depth,((void**)set)[i]);
		}
		BAfree(main->node_alloc,set);
	} else {
		BAfree(main->base_alloc,set);
	}
}

void bitset_destroy(bitset_t set){
    free_set(set,set->depth,set->set);
    BAderef(set->node_alloc);
    BAderef(set->base_alloc);
    free(set);
}

int bitset_clear_all(bitset_t set){
	free_set(set,set->depth,set->set);
	set->max=(((element_t)1)<<set->base_class)-1;
	set->set=ALL_ZERO;
	set->default_value=ALL_ZERO;
	set->depth=0;
	return 1;
}

int bitset_set_all(bitset_t set){
	free_set(set,set->depth,set->set);
	set->max=(((element_t)1)<<set->base_class)-1;
	set->set=ALL_ONES;
	set->default_value=ALL_ONES;
	set->depth=0;
	return 1;
}

static int expand(bitset_t set){
	void **newset=(void**)BAget(set->node_alloc);
	int i;
	if(newset==NULL) return 0;
	newset[0]=set->set;
	for(i=1;i<(1<<set->node_class);i++){
		newset[i]=set->default_value;
	}
	set->set=(void*)newset;
	set->max++;
	set->max<<=set->node_class;
	set->max--;
	set->depth++;
	return 1;
}

static int modify(bitset_t set,void**ptr,int depth,void *v,element_t e){
	if ((*ptr)==v) return 1;
	if (depth) {
		int seg;
		int bits;
		if (((intptr_t)(*ptr))&3) {
			void **newset=(void**)BAget(set->node_alloc);
			int i;
			if(newset==NULL) return 0;
			for(i=0;i<(1<<set->node_class);i++){
				newset[i]=(*ptr);
			}
			(*ptr)=(void*)newset;
		}
		depth--;
		bits=depth*set->node_class+set->base_class;
		seg=e>>bits;
		e=e&((1<<bits)-1);
		if (modify(set,(void**)(((void**)(*ptr))+seg),depth,v,e)){
			for(seg=0;seg<(1<<set->node_class);seg++){
				if (((void**)(*ptr))[seg]!=v) return 1;
			}
			BAfree(set->node_alloc,*ptr);
			*ptr=v;
			return 1;
		} else {
			return 0;
		}
	} else {
		int seg;
		word_t w;
		if (((intptr_t)(*ptr))&3) {
			word_t *newset=(word_t*)BAget(set->base_alloc);
			int i;
			if(newset==NULL) return 0;
			w=(word_t)0;
			if ((*ptr)==ALL_ONES) w=~w;
			for(i=0;i<(1<<(set->base_class-WORD_CLASS));i++){
				newset[i]=w;
			}
			*ptr=(void*)newset;
		}
		seg=e>>WORD_CLASS;
		e=e&WORD_MASK;
		w=((word_t)1)<<e;
		if (v==ALL_ONES){
			((word_t*)(*ptr))[seg]|=w;
		} else {
			((word_t*)(*ptr))[seg]&=(~w);
		}
		if (v==ALL_ONES) {
			w=~0;
		} else {
			w=0;
		}
		for(seg=0;seg<(1<<(set->base_class-WORD_CLASS));seg++){
			if (((word_t*)(*ptr))[seg]!=w) return 1;
		}
		BAfree(set->base_alloc,*ptr);
		*ptr=v;
		return 1;
	}
}

int bitset_clear(bitset_t set,element_t e){
	if(e>set->max){
		if (set->default_value==ALL_ZERO) return 1;
		while(e>set->max){
			if (!expand(set)) return 0;
		}
	}
	return modify(set,&(set->set),set->depth,ALL_ZERO,e);
}

int bitset_set(bitset_t set,element_t e){
	if(e>set->max){
		if (set->default_value==ALL_ONES) return 1;
		while(e>set->max){
			if (!expand(set)) return 0;
		}
	}
	return modify(set,&(set->set),set->depth,ALL_ONES,e);
}

static int testbit(bitset_t set,void *ptr,int depth,element_t e){
	int seg;
	int bits;
	if (ptr==ALL_ONES) return 1;
	if (ptr==ALL_ZERO) return 0;
	if (depth) {
		depth--;
		bits=depth*set->node_class+set->base_class;
		seg=e>>bits;
		e=e&((1<<bits)-1);
		return testbit(set,((void**)ptr)[seg],depth,e);
	} else {
		seg=e>>WORD_CLASS;
		e=e&WORD_MASK;
		return ((((word_t*)ptr)[seg]) & (((word_t)1)<<e))?1:0;	
	}
}

int bitset_test(bitset_t set,element_t e){
	if (e>set->max) return (set->default_value==ALL_ONES);
	return testbit(set,set->set,set->depth,e);
}

int bitset_next_clear(bitset_t set,element_t *e);
int bitset_prev_clear(bitset_t set,element_t *e);

static int scan_right_set(bitset_t set,void *ptr,int depth,element_t *e){
	int bits;
	element_t w;
	int seg;
	int found;
	int le;
	word_t word;

	if (ptr==ALL_ONES) return 1;
	if (ptr==ALL_ZERO) {
		bits=depth*set->node_class+set->base_class;
		w=~0;
		w<<=bits;
		*e&=w;
		*e+=1<<bits;
		return 0;
	}
	if (depth) {
		bits=depth*set->node_class+set->base_class;
		w=(1<<bits)-1;
		depth--;
		bits-=set->node_class;
		seg=(*e&w)>>bits;
		for (;seg<(1<<set->node_class);seg++){
			if (scan_right_set(set,((void**)ptr)[seg],depth,e)) return 1;
		}
		return 0;
	} else {
		bits=set->base_class;
		w=(1<<bits)-1;
		le=*e&w;
		*e&=~w;
		found=0;
		for(;le<(1<<set->base_class);le++){
			seg=le>>WORD_CLASS;
			word=1<<(le&WORD_MASK);
			if ((((word_t*)ptr)[seg])&word) {
				found=1;
				break;
			}
		}
		*e+=le;
		return found;
	}
}

int bitset_next_set(bitset_t set,element_t *e){
	element_t old;
	if (*e>set->max) return (set->default_value==ALL_ONES);
	old=*e;
	if (scan_right_set(set,set->set,set->depth,e)) return 1;
	*e=old;
	return (set->default_value==ALL_ONES);
}

static int scan_left_set(bitset_t set,void *ptr,int depth,element_t *e){
	int bits;
	element_t w;
	int seg;
	int found;
	int le;
	word_t word;

	if (ptr==ALL_ONES) return 1;
	if (ptr==ALL_ZERO) {
		bits=depth*set->node_class+set->base_class;
		w=(1<<bits)-1;
		(*e)|=w;
		(*e)-=1<<bits;
		return 0;
	}
	if (depth) {
		bits=depth*set->node_class+set->base_class;
		w=(1<<bits)-1;
		depth--;
		bits-=set->node_class;
		seg=(*e&w)>>bits;
		for (;seg>=0;seg--){
			if (scan_left_set(set,((void**)ptr)[seg],depth,e)) return 1;
		}
		return 0;
	} else {
		bits=set->base_class;
		w=(1<<bits)-1;
		le=*e&w;
		*e&=~w;
		found=0;
		for(;le>=0;le--){
			seg=le>>WORD_CLASS;
			word=1<<(le&WORD_MASK);
			if ((((word_t*)ptr)[seg])&word) {
				found=1;
				break;
			}
		}
		*e+=le;
		return found;
	}
}

int bitset_prev_set(bitset_t set,element_t *e){
	element_t old;
	if (*e>set->max) {
		if (set->default_value==ALL_ONES) return 1;
		old=*e;
		*e=set->max;
	} else {
		old=*e;
	}
	if (scan_left_set(set,set->set,set->depth,e)) return 1;
	*e=old;
	return 0;
}

static void print(FILE*f,bitset_t set,void* ptr,int depth){
	int i,seg;
	for(i=set->depth;i>depth;i--) fprintf(f,"|");
	if (ptr==ALL_ONES) {
		fprintf(f,"+ALL_ONES\n");
	} else if (ptr==ALL_ZERO) {
		fprintf(f,"+ALL_ZERO\n");
	} else if (depth) {
		fprintf(f,"++\n");
		depth--;
		for(seg=0;seg<(1<<set->node_class);seg++) print(f,set,((void**)ptr)[seg],depth);
	} else {
		fprintf(f,"+");
		for(seg=0;seg<(1<<(set->base_class-WORD_CLASS));seg++){
			for(i=0;i<1<<WORD_CLASS;i++){
				fprintf(f,"%s",(((word_t*)ptr)[seg]&(1<<i))?"1":"0");
			}
		}
		fprintf(f,"\n");
	}
}

void bitset_fprint(FILE*f,bitset_t set) {
	print(f,set,set->set,set->depth);
	fprintf(f,"+%s\n",(set->default_value==ALL_ONES)?"ALL_ONES":"ALL_ZERO");
}

static void invert(bitset_t set,void *ptr,int depth){
	int i;
	int count;
	if (depth) {
		depth--;
		count=1<<set->node_class;
		for(i=0;i<count;i++){
			if (((void**)ptr)[i]==ALL_ZERO){
				((void**)ptr)[i]=ALL_ONES;
			} else if (((void**)ptr)[i]==ALL_ONES){
				((void**)ptr)[i]=ALL_ZERO;
			} else invert(set,((void**)ptr)[i],depth);
		}
	} else {
		count=1<<(set->base_class-WORD_CLASS);
		for(i=0;i<count;i++){
			((word_t*)ptr)[i]=~((word_t*)ptr)[i];
		}
	}
}

/* set := complement of set */
void bitset_invert(bitset_t set){
	if (set->default_value==ALL_ONES) {
		set->default_value=ALL_ZERO;
	} else {
		set->default_value=ALL_ONES;
	}
	if (set->set==ALL_ZERO){
		set->set=ALL_ONES;
	} else if (set->set==ALL_ONES){
		set->set=ALL_ZERO;
	} else invert(set,set->set,set->depth);
}

void bitset_intersect(bitset_t set1, bitset_t set2){
	element_t i,max;

	if (set1->default_value==ALL_ZERO){
		max=set1->max;
		for(i=0;bitset_next_set(set1,&i) && (i<=max);i++){
			if (!bitset_test(set2,i)) bitset_clear(set1,i);
		}
	} else if (set2->default_value==ALL_ONES){
		max=set2->max;
		for(i=0;bitset_next_set(set1,&i) && (i<=max);i++){
			if (!bitset_test(set2,i)) bitset_clear(set1,i);
		}
	} else {
		if (set1->max<set2->max) {
			bitset_clear(set1,set2->max);
			set1->default_value=ALL_ZERO;
			bitset_set(set1,set2->max);
		} else {
			set1->default_value=ALL_ZERO;
		}
		max=set1->max|set2->max;
		for(i=0;bitset_next_set(set1,&i) && (i<=max);i++){
			if (!bitset_test(set2,i)) bitset_clear(set1,i);
		}
	}
}

void bitset_union(bitset_t set1, bitset_t set2){
	element_t i,max;

	if (set1->default_value==ALL_ONES){
		max=set1->max;
		for(i=0;bitset_next_set(set2,&i) && (i<=max);i++){
			bitset_set(set1,i);
		}
	} else if (set2->default_value==ALL_ZERO){
		max=set2->max;
		for(i=0;bitset_next_set(set2,&i) && (i<=max);i++){
			bitset_set(set1,i);
		}
	} else {
		if (set1->max<set2->max) {
			bitset_set(set1,set2->max);
			set1->default_value=ALL_ONES;
			bitset_clear(set1,set2->max);
		} else {
			set1->default_value=ALL_ONES;
		}
		max=set1->max|set2->max;
		for(i=0;bitset_next_set(set2,&i) && (i<=max);i++){
			bitset_set(set1,i);
		}
	}
}

