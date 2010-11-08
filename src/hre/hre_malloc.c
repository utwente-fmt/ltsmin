// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hre/provider.h>
#include <hre/internal.h>

void* RTmalloc(size_t size){
	if(size==0) return NULL;
	void *tmp=malloc(size);
	if (tmp==NULL) Abort("out of memory trying to get %d",size);
	return tmp;
}

void* RTmallocZero(size_t size){
	void *p=RTmalloc(size);
	memset(p, 0, size);
	return p;
}

void* RTrealloc(void *rt_ptr, size_t size){
    if (size==0) { // macosx realloc(??.0) does not return NULL!
        free(rt_ptr);
        return NULL;
	}
    void *tmp=realloc(rt_ptr,size);
    if (tmp==NULL) Abort("out of memory trying to resize to %d",size);
    return tmp;
}

void RTfree(void *rt_ptr){
	if(rt_ptr != NULL) free (rt_ptr);
}

struct hre_region_s {
    void*area;
    hre_malloc_t malloc;
    hre_realloc_t realloc;
    hre_free_t free;
};

hre_region_t hre_heap=NULL;

void* HREmalloc(hre_region_t region,size_t size){
    if(region==NULL) {
        return RTmalloc(size);
    } else {
        return region->malloc(region->area,size);
    }
}

void* HREmallocZero(hre_region_t region,size_t size){
    if(region==NULL) {
        return RTmallocZero(size);
    } else {
        void*res=region->malloc(region->area,size);
        if (size) memset(res, 0, size);
        return res;
    }
}

void* HRErealloc(hre_region_t region,void* mem,size_t size){
    if(region==NULL) {
        return RTrealloc(mem,size);
    } else {
        return region->realloc(region->area,mem,size);
    }
}

void HREfree(hre_region_t region,void* mem){
    if(region==NULL) {
        RTfree(mem);
    } else {
        region->free(region->area,mem);
    }
}

hre_region_t HREcreateRegion(void* area,hre_malloc_t malloc,hre_realloc_t realloc,hre_free_t free){
    hre_region_t res=HRE_NEW(hre_heap,struct hre_region_s);
    res->area=area;
    res->malloc=malloc;
    res->realloc=realloc;
    res->free=free;
    return res;
}

