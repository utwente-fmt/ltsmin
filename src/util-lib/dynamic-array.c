// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>
#include <stdlib.h>
#include <strings.h>

#include <hre/user.h>
#include <util-lib/dynamic-array.h>

#define MBLOCK 16

struct array {
	int e_size;
	array_resize_cb callback;
	void *cbarg;
	void **ar;
};

struct array_manager {
	size_t block;
	size_t size;
	int managed;
	int managed_size;
	struct array *arrays;
};

array_manager_t create_manager(size_t block_size){
	array_manager_t man=(array_manager_t)RTmalloc(sizeof(struct array_manager));
	man->block=block_size;
	man->size=block_size;
	man->managed=0;
	man->managed_size=0;
	man->arrays=NULL;
	return man;
}

void destroy_manager(array_manager_t man)
{
    // destroy managed arrays
    for(int i=0; i < man->managed; i++)
    {
        RTfree(*man->arrays[i].ar);
    }
    RTfree(man->arrays);
    RTfree(man);
}


size_t array_size(array_manager_t man){
	return man->size;
}

static void fix_array(struct array ref,size_t old_size,size_t new_size){
	void*tmp;
	void*old=*ref.ar;
	tmp=RTrealloc(*ref.ar,new_size*ref.e_size);
	HREassert (tmp, "realloc from %zu to %zu * %d failed",old_size,new_size,ref.e_size);
    Debug("%p -> %p",*ref.ar,tmp);
    *ref.ar=tmp;
    if (ref.callback) {
        ref.callback(ref.cbarg,old,old_size,tmp,new_size);
    } else if (new_size>old_size) {
        memset(tmp+(old_size*ref.e_size), 0, (new_size-old_size)*ref.e_size);
    }
}

void add_array(array_manager_t man,void**ar,int e_size,array_resize_cb callback,void*cbarg){
	if(man->managed>=man->managed_size){
		int old=man->managed_size;
		man->managed_size+=MBLOCK;
		struct array self_ar;
		self_ar.e_size=sizeof(struct array);
		self_ar.ar=(void**)&(man->arrays);
		self_ar.callback=NULL;
		self_ar.cbarg=NULL;
		fix_array(self_ar,old,man->managed_size);
	}
	man->arrays[man->managed].e_size=e_size;
	man->arrays[man->managed].ar=ar;
	man->arrays[man->managed].callback=callback;
	man->arrays[man->managed].cbarg=cbarg;
	fix_array(man->arrays[man->managed],0,man->size);
	man->managed++;
	Debug("added array with e_size %d",e_size);
}

void ensure_access(array_manager_t man,size_t index){
	if (index < man->size) return;
	if (index/man->block > 10) man->block=man->block*2;
	size_t old=man->size;
	man->size=((index+man->block)/man->block)*man->block;
	Debug("resize from %zu to %zu",old,man->size);
	for(int i=0;i<man->managed;i++){
		fix_array(man->arrays[i],old,man->size);
	}
}


