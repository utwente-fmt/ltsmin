// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#include <hre/config.h>

#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include <hre/feedback.h>
#include <hre/runtime.h>
#include <hre/user.h>

typedef void(*plugin_init_t)();

void* RTdlopen(const char *name){
    void *dlHandle;
    char abs_filename[PATH_MAX];
    char *ret_filename = realpath(name, abs_filename);
    if (ret_filename) {
        dlHandle = dlopen(abs_filename, RTLD_LAZY);
        if (dlHandle == NULL)
        {
            Abort("%s, Library \"%s\" is not reachable", dlerror(), name);
        }
    } else {
        Abort("Library \"%s\" is not found", name);
    }
    plugin_init_t init=RTtrydlsym(dlHandle,"hre_init");
    if (init!=NULL){
        init();
    } else {
        Warning(info,"library has no initializer");
    }
    return dlHandle;
}

void *
RTdlsym (const char *libname, void *handle, const char *symbol)
{
    void *ret = dlsym (handle, symbol);
    if (ret == NULL) {
        const char *dlerr = dlerror ();
        Abort("dynamically loading from `%s': %s", libname,
               dlerr != NULL ? dlerr : "unknown error");
    }
    return ret;
}

void *
RTtrydlsym(void *handle, const char *symbol)
{
    return dlsym (handle, symbol);
}

int linear_search(si_map_entry map[],const char*key){
    while(map[0].key){
        if(!strcmp(map[0].key,key)) return map[0].val;
        map++;
    }
    return -1;
}

char *key_search(si_map_entry map[],const int val){
    while(map[0].key){
        if(map[0].val == val) return map[0].key;
        map++;
    }
    return "not found";
}

#if defined(__APPLE__)

size_t RTmemSize(){
    int mib[4];
    int64_t physical_memory;
    size_t len = sizeof(int64_t);
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    len = sizeof(int64_t);
    sysctl(mib, 2, &physical_memory, &len, NULL, 0);
    return physical_memory;
}

int RTcacheLineSize(){
    int mib[4];
    int line_size;
    size_t len = sizeof(int);
    mib[0] = CTL_HW;
    mib[1] = HW_CACHELINE;
    len = sizeof(int);
    sysctl(mib, 2, &line_size, &len, NULL, 0);
    return line_size;
}

#else

size_t RTmemSize(){
    long res=sysconf(_SC_PHYS_PAGES);
    size_t pagesz=RTpageSize();
    return pagesz*((size_t)res);
}

#if defined(__linux__)

int RTcacheLineSize(){
    long res=sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return (int)res;
}

#else

int RTcacheLineSize(){
    Abort("generic implementation for RTcacheLineSize needed");
}

#endif

#endif

int RTnumCPUs(){
    long res=sysconf(_SC_NPROCESSORS_ONLN);
    return (size_t)res;
}

size_t RTpageSize(){
    long res=sysconf(_SC_PAGESIZE);
    return (size_t)res;
}

