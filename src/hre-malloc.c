#include <hre-main.h>
#include <hre-internal.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct hre_region_s {
    uint64_t allocated;
    uint64_t freed;
};

static struct hre_region_s heap={.allocated=0,.freed=0};

hre_region_t hre_heap=&heap;

void* HREmalloc(hre_region_t region,size_t size){
    (void)region;
    void* res=malloc(size);
    if (size && !res) {
        Abort("could not allocate %llu bytes",(unsigned long long)size);
    } else {
        return res;
    }
}

void* HREmallocZero(hre_region_t region,size_t size){
    (void)region;
    void* res=HREmalloc(region,size);
    if (size) memset(res, 0, size);
    return res;
}

void HREfreeGuess(hre_region_t region,void* mem){
    (void)region;
    if (mem) free(mem);
}

void HREfree(hre_region_t region,void* mem,size_t size){
    (void)region;(void)size;
    if (mem) free(mem);
}
