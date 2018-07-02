// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>
#undef _XOPEN_SOURCE

#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <hre/provider.h>
#include <hre/internal.h>

static hre_region_t region = NULL;

hre_region_t
RTgetMallocRegion()
{
    return region;
}

void
RTsetMallocRegion(hre_region_t r)
{
    region = r;
    Debug ("Switched allocator to %p", r);
}

void* RTmalloc(size_t size){
    if (region)
        return HREmalloc(region, size);
    if(size==0) return NULL;
    void *tmp=malloc(size);
    if (tmp==NULL) Abort("out of memory trying to get %zu",size);
    Debug("allocated %zu from system", size);
    return tmp;
}

void* RTmallocZero(size_t size){
    if (region)
        return HREmallocZero(region, size);
    if(size==0) return NULL;
    void *tmp=calloc((size + CACHE_LINE_SIZE - 1) >> CACHE_LINE, CACHE_LINE_SIZE);
    if (tmp==NULL) Abort("out of memory trying to get %zu",size);
    Debug("allocated %zu in anonymous pages from system", size);
    return tmp;
}

void* RTalign(size_t align, size_t size) {
    if (region)
        return HREalign(region, align, size);
#ifdef HAVE_POSIX_MEMALIGN
    void *ret = NULL;
    errno = posix_memalign(&ret, align, size);
#elif HAVE__ALIGNED_MALLOC
    void *ret = _aligned_malloc(size, align);
#endif
    if (errno) {
    switch (errno) {
        case ENOMEM:
            Fatal(0,error,"out of memory on allocating %zu bytes aligned at %zu",
                  size, align);
        case EINVAL:
            Fatal(0,error,"invalid alignment %zu", align);
        default:
            Fatal(0,error,"unknown error allocating %zu bytes aligned at %zu",
                  size, align);
    }}
    HREassert (NULL != ret, "Alloc failed");
    Debug("allocated %zu aligned at %zu from system", size, align);
    return ret;
}

#define MAX_ALIGN_MEMSET (1024*1024)
#define MAX_ALIGN_ZEROS (1024)
static size_t next_calloc = 0;
static void *calloc_table[MAX_ALIGN_ZEROS][3];

void* RTalignZero(size_t align, size_t size) {
    if (region)
        return HREalign(region, align, size);
    if (0 == align) Abort("Zero alignment in RTalignZero");
    if (0 == size) return NULL;
    if (size < MAX_ALIGN_MEMSET) {
        // for small sizes do memset
        void *mem = RTalign(align, size);
        memset (mem, 0 , size);
        Debug("allocated %zu in anonymous pages aligned at %zu from system", size, align);
        return mem;
    }
    // for large sizes use calloc and do manual alignment
    if ((size / align)*align != size) // make size multiple of align
        size = ((size + align) / align)*align;
//    void *p = calloc((size / align + 1), align);
    void *p = mmap (NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    // MAP_PRIVATE && WRITE, because we still want the attached NUMA policy to be followed
    if (p == MAP_FAILED)
        Abort ("mmap failed for size %zu MB", size>>20);
    size_t pp = (size_t)p;
    void *old = p;
    if ((pp / align) * align != pp) // manual alignment only if needed
        p = (void*)((pp / align + 1) * align);
    // store self-aligned allocs in calloc_table
    size_t next = __sync_fetch_and_add (&next_calloc, 1);
    calloc_table[next][0] = p;
    calloc_table[next][1] = old;
    calloc_table[next][2] = (void*)size;
    Debug("allocated %zu in (LARGE) anonymous pages aligned at %zu from system", size, align);
    return p;
}

void* RTrealloc(void *rt_ptr, size_t size){
    if (region)
        return HRErealloc(region, rt_ptr, size);
    if (size==0) { // macosx realloc(??.0) does not return NULL!
        free(rt_ptr);
        return NULL;
    }
    Debug("reallocating %p to size %zu from system", rt_ptr, size);
    void *tmp=realloc(rt_ptr,size);
    if (tmp==NULL) Abort("out of memory trying to resize to %zu",size);
    return tmp;
}

void RTfree(void *rt_ptr){
    if (region) return HREfree(region, rt_ptr);
    if (rt_ptr != NULL) {
        Debug("freeing %p from system", rt_ptr);
        free (rt_ptr);
    }
}

void RTalignedFree(void *rt_ptr){
    if (region) return HREalignedFree(region, rt_ptr);
    for (size_t i = 0; i < next_calloc; i++) {
        if (rt_ptr == calloc_table[i][0]) {
            Debug("freeing %p (LARGE) from system", rt_ptr);
            munmap (calloc_table[i][1], (size_t)calloc_table[i][2]);
            return;
        }
    }
    if (rt_ptr != NULL) {
        Debug("freeing %p from system", rt_ptr);
#ifdef HAVE_POSIX_MEMALIGN
        free (rt_ptr);
#elif HAVE__ALIGNED_MALLOC
        _aligned_free(rt_ptr);
#endif
    }
}

void
RTswitchAlloc (bool shared)
{
    if (shared) {
        HREassert (RTgetMallocRegion() == NULL);
        //HREassert (HREdefaultRegion(HREglobal()) != NULL);
        RTsetMallocRegion (HREdefaultRegion(HREglobal()));
    } else {
        //HREassert (RTgetMallocRegion() != NULL);
        RTsetMallocRegion (NULL);
    }
}

struct hre_region_s {
    void*area;
    hre_malloc_t malloc;
    hre_align_t align;
    hre_realloc_t realloc;
    hre_free_t free;
    hre_free_t aligned_free;
};

hre_region_t hre_heap=NULL;

void* HREmalloc(hre_region_t region,size_t size){
    if(region==NULL) {
        return RTmalloc(size);
    } else {
        return region->malloc(region->area,size);
    }
}

void* HREalign(hre_region_t region,size_t align,size_t size){
    if(region==NULL) {
        return RTalign(align,size);
    } else {
        return region->align(region->area,align,size);
    }
}

void* HREalignZero(hre_region_t region,size_t align,size_t size){
    if(region==NULL) {
        return RTalignZero(align,size);
    } else {
        return region->align(region->area,align,size);
        // Assumes region is zero'd
    }
}

void* HREmallocZero(hre_region_t region,size_t size){
    if(region==NULL) {
        return RTmallocZero(size);
    } else {
        void*res=region->malloc(region->area,size);
        //if (size) memset(res, 0, size); //region was allocated using MAP_ANON
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

void HREalignedFree(hre_region_t region,void*mem){
    if(region==NULL) {
        RTalignedFree(mem);
    } else {
        region->aligned_free(region->area,mem);
    }
}

void *HREgetArea(hre_region_t region) {
    return region->area;
}

hre_region_t HREcreateRegion(void* area,hre_malloc_t malloc,hre_align_t align,hre_realloc_t realloc,hre_free_t free,hre_free_t aligned_free){
    hre_region_t res=HRE_NEW(hre_heap,struct hre_region_s);
    res->area=area;
    res->malloc=malloc;
    res->align=align;
    res->realloc=realloc;
    res->free=free;
    res->aligned_free=aligned_free;
    return res;
}

