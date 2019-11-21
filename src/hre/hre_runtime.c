// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#include <hre/config.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <ltdl.h>
#ifdef __linux__
#   include <sched.h> // for sched_getaffinity
#endif
#ifdef _WIN32
#   include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        lt_dlinit();
        dlHandle = lt_dlopen(abs_filename);
        if (dlHandle == NULL)
        {
            Abort("%s, Library \"%s\" is not reachable", lt_dlerror(), name);
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
    void *ret = lt_dlsym (handle, symbol);
    if (ret == NULL) {
        const char *dlerr = lt_dlerror ();
        Abort("dynamically loading from `%s': %s", libname,
               dlerr != NULL ? dlerr : "unknown error");
    }
    return ret;
}

void *
RTtrydlsym(void *handle, const char *symbol)
{
    return lt_dlsym (handle, symbol);
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

#ifdef __linux__
static int mem_size_warned = 0;
#endif

size_t RTmemSize() {
    const char *memsize = getenv("LTSMIN_MEM_SIZE");
    if (memsize) {
        return strtoimax(memsize, NULL, 10);
    } else {
#ifdef __APPLE__
        int mib[4];
        int64_t physical_memory;
        size_t len = sizeof(int64_t);
        mib[0] = CTL_HW;
        mib[1] = HW_MEMSIZE;
        len = sizeof(int64_t);
        sysctl(mib, 2, &physical_memory, &len, NULL, 0);
        return physical_memory;
#elif defined(__linux__)
        const long res=sysconf(_SC_PHYS_PAGES);
        const long pagesz=sysconf(_SC_PAGESIZE);
        size_t limit = pagesz*((size_t)res);

        /* Now try to determine whether this program runs in a cgroup.
         * If this is the case, we will pick the minimum of the previously
         * computed limit.
         */
        const char *file = "/sys/fs/cgroup/memory/memory.limit_in_bytes";
        FILE *fp = fopen(file, "r");
        if (fp != NULL) {
            size_t cgroup_limit = 0;
            /* If there is no limit then the value scanned for will be larger
             * than SIZE_T_MAX, and thus fscanf will not return 1.
             */
            int ret = fscanf(fp, "%zu", &cgroup_limit);
            if (ret == 1 && cgroup_limit > 0) {
                if (cgroup_limit < limit) {
                    limit = cgroup_limit;
                    if (!mem_size_warned) {
                        Warning(infoLong,
                                "Using cgroup limit of %zu bytes", limit);
                    }
                }
            } else if (!mem_size_warned) {
                Warning(lerror, "Unable to get cgroup memory limit "
                        "in file %s: %s",
                        file, errno != 0 ? strerror(errno) : "unknown error");
            }
            fclose(fp);
        } else if (!mem_size_warned) {
            Warning(lerror, "Unable to open cgroup memory limit file %s: %s",
                    file, strerror(errno));
        }

        mem_size_warned = 1;

        return limit;
#elif defined(_WIN32)
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);

    return (size_t) statex.ullTotalPhys;
#else
#   error Can not detect available memory on this system
#endif
    }
}

#ifdef _WIN32
static int line_size = -1;
#endif

int RTcacheLineSize(){
#ifdef __APPLE__
    int mib[4];
    int line_size;
    size_t len = sizeof(int);
    mib[0] = CTL_HW;
    mib[1] = HW_CACHELINE;
    len = sizeof(int);
    sysctl(mib, 2, &line_size, &len, NULL, 0);
    return line_size;
#elif defined(__linux__)
    long res=sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return (int)res;
#elif defined(_WIN32)
    if (line_size == -1) {
        DWORD buffer_size = 0;
        DWORD i = 0;
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION * buffer = 0;

        GetLogicalProcessorInformation(0, &buffer_size);
        buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)RTmalloc(buffer_size);
        GetLogicalProcessorInformation(&buffer[0], &buffer_size);

        for (i = 0; i != buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i) {
            if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1) {
                line_size = buffer[i].Cache.LineSize;
                break;
            }
        }

        RTfree(buffer);
    }

    return line_size;
#else
#    error can not detect cache line size on this system
#endif
}

int RTnumCPUs(){
    const char *numCPUs = getenv("LTSMIN_NUM_CPUS");
    if (numCPUs) {
        return strtoimax(numCPUs, NULL, 10);
    } else {
#if defined(__linux__) || defined(__APPLE__)
        int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#ifdef __linux__
        cpu_set_t cpus;
        cpu_set_t* cpus_p = &cpus;
        size_t cpus_size = sizeof(cpu_set_t);
        int configured_cpus = sysconf(_SC_NPROCESSORS_CONF);
        if (configured_cpus >= CPU_SETSIZE) {
            cpus_p = CPU_ALLOC(configured_cpus);
            if (cpus_p) {
                cpus_size = CPU_ALLOC_SIZE(configured_cpus);
                CPU_ZERO_S(cpus_size, cpus_p);
            } else {
                Warning(lerror, "CPU_ALLOC failed: %s",
                        errno != 0 ? strerror(errno) : "unknown error");
                return cpu_count;
            }
        }
        if (!sched_getaffinity(0, cpus_size, cpus_p)) {
            if (cpus_p != &cpus) cpu_count = CPU_COUNT_S(cpus_size, cpus_p);
            else cpu_count = CPU_COUNT(cpus_p);
        } else {
            Warning(lerror, "Unable to get CPU set affinity: %s",
                    errno != 0 ? strerror(errno) : "unknown error");
        }
        if (cpus_p != &cpus) CPU_FREE(cpus_p);
#endif
        return cpu_count;
#elif defined(_WIN32)
    SYSTEM_INFO siSysInfo;
    GetSystemInfo(&siSysInfo);

    return (int) siSysInfo.dwNumberOfProcessors;
#else
#   error can not detect number of CPUs on this system
#endif
    }
}

