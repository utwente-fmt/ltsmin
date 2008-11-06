#include "dynamic-array.h"
#include <malloc.h>
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

static void fix_array(void**ar,int size,int e_size){
	void*tmp;
	tmp=realloc(*ar,size*e_size);
	if (tmp) {
		DEBUG(info,"%x -> %x",*ar,tmp);
		*ar=tmp;
	} else {
		// size is never 0, so tmp==NULL is an error.
		Fatal(1,error,"realloc to %d * %d failed",size,e_size);
	}
}

void add_array(array_manager_t man,void**ar,int e_size){
	if(man->managed>=man->managed_size){
		man->managed_size+=MBLOCK;
		fix_array((void**)&(man->arrays),man->managed_size,sizeof(struct array));
	}
	fix_array(ar,man->size,e_size);
	man->arrays[man->managed].e_size=e_size;
	man->arrays[man->managed].ar=ar;
	man->managed++;
	DEBUG("added array with e_size %d",e_size);
}

void ensure_access(array_manager_t man,int index){
	int i;
	if (index < man->size) return;
	i=((index+man->block)/man->block)*man->block;
	DEBUG("resize from %d to %d",man->size,i);
	man->size=i;
	for(i=0;i<man->managed;i++){
		fix_array(man->arrays[i].ar,man->size,man->arrays[i].e_size);
	}
}

