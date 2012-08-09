#include <hre/config.h>

#include <stdio.h>
#include <stdlib.h>

#include <hre/user.h>
#include <util-lib/balloc.h>

struct block;
struct block {
  struct block *next;
};

struct block_allocator {
  size_t element_size;
  size_t block_size;
  struct block *block_list;
  void* free_list;
  int ref_count;
};

allocater_t BAcreate(size_t element_size,size_t block_size){
	if (element_size<sizeof(void*)) {
		Abort("element size less than pointer size");
	}
	allocater_t a = RT_NEW(struct block_allocator);
	a->element_size=element_size;
	a->block_size=block_size;
	a->block_list=NULL;
	a->free_list=NULL;
	a->ref_count=1;
	return a;
}

static void BAdestroy(allocater_t a){
	void *p1,*p2;
	p1=a->block_list;
	while(p1!=NULL){
		p2=*((void**)p1);
		RTfree(p1);
		p1=p2;
	}
	RTfree(a);
}

void BAaddref(allocater_t a){
    a->ref_count++;
    Warning(debug,"addref: reference count is %d",a->ref_count);
}

void BAderef(allocater_t a){
    a->ref_count--;
    Warning(debug,"deref: reference count is %d",a->ref_count);
    if(a->ref_count==0){
        Warning(debug,"destroying");
        BAdestroy(a);
    }
}


void* BAget(allocater_t a){
	void *e;
	if(a->free_list == NULL){
		struct block *blk;
		int i;
		blk=(struct block*)RTmalloc(a->block_size);
		if (blk==NULL) {
			return NULL;
		}
		for(i=1;(i+1)*a->element_size <= a->block_size;i++){
			if (i*a->element_size < sizeof(struct block)) continue;
			e=((void*)blk)+(i*a->element_size);
			*((void**)e)=a->free_list;
			a->free_list=e;
		}
		blk->next=a->block_list;
		a->block_list=blk;
	}
	e=a->free_list;
	a->free_list=*((void**)e);
	//fprintf(stderr,"BAget(%d): alloc %8x\n",a->element_size,e);
	return e;
}


void BAfree(allocater_t a,void* e){
#ifdef FREE_CHECK
	void *p;
	struct block *blk;
	int i;
	fprintf(stderr,"BAfree(%d): free %8x\n",a->element_size,e);
	for(p=a->free_list;p!=NULL;p=*((void**)p)){
		if (e==p) {
			fprintf(stderr,"BAfree: freeing element already on the free list\n");
			exit(HRE_EXIT_FAILURE);
		}
	}
	for(blk=a->block_list;blk!=NULL;blk=blk->next){
		for(i=1;(i+1)*a->element_size <= a->block_size;i++){
			if (i*a->element_size < sizeof(struct block)) continue;
			p=((void*)blk)+(i*a->element_size);
			if(p==e){
#endif
				*((void**)e)=a->free_list;
				a->free_list=e;

#ifdef FREE_CHECK
				return;
			}
		}
	}
	//fprintf(stderr,"BAfree(%d): attempt to free an unowned block\n",a->element_size);
	exit(HRE_EXIT_FAILURE);
#endif
}
