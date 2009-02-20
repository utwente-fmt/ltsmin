#include "dynamic-array.h"
#include <stdlib.h>
#include "runtime.h"

#define MBLOCK 16


#define DEBUG(...) {}

struct array {
	int e_size;
	void **ar;
};

struct array_manager {
	int block;
	int size;
	int managed;
	int managed_size;
	struct array *arrays;
};

array_manager_t create_manager(int block_size){
	array_manager_t man=(array_manager_t)RTmalloc(sizeof(struct array_manager));
	man->block=block_size;
	man->size=block_size;
	man->managed=0;
	man->managed_size=0;
	man->arrays=NULL;
	return man;
}

int array_size(array_manager_t man){
	return man->size;
}

static void fix_array(void**ar,int oldsize,int size,int e_size){
	void*tmp;
	tmp=realloc(*ar,size*e_size);
	if (tmp) {
		DEBUG(info,"%x -> %x",*ar,tmp);
		*ar=tmp;
		bzero(tmp+(oldsize*e_size),(size-oldsize)*e_size);
	} else {
		// size is never 0, so tmp==NULL is an error.
		Fatal(1,error,"realloc from %d to %d * %d failed",oldsize,size,e_size);
	}
}

void add_array(array_manager_t man,void**ar,int e_size){
	if(man->managed>=man->managed_size){
		int old=man->managed_size;
		man->managed_size+=MBLOCK;
		struct array **ptr=&(man->arrays);
		fix_array((void**)ptr,old,man->managed_size,sizeof(struct array));
	}
	fix_array(ar,0,man->size,e_size);
	man->arrays[man->managed].e_size=e_size;
	man->arrays[man->managed].ar=ar;
	man->managed++;
	DEBUG("added array with e_size %d",e_size);
}

void ensure_access(array_manager_t man,int index){
	if (index < man->size) return;
	int old=man->size;
	man->size=((index+man->block)/man->block)*man->block;
	DEBUG("resize from %d to %d",old,man->size);
	for(int i=0;i<man->managed;i++){
		fix_array(man->arrays[i].ar,old,man->size,man->arrays[i].e_size);
	}
}


