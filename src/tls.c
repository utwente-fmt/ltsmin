#include <config.h>
#ifndef _DARWIN_C_SOURCE
# define _DARWIN_C_SOURCE
#endif
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include <runtime.h>
#include <tls.h>

struct tls_s {
    pthread_key_t       key;
    void               *owner;
    size_t              size;
    int                 id;
};

typedef struct owner_id_s owner_id_t;

struct owner_id_s {
    void               *owner;
    int                 id;
    owner_id_t         *next;
};

static owner_id_t      *owner_ids = NULL;
static int              last_id = 0;

int
get_owner_id (void *owner)
{
    owner_id_t        **next = &owner_ids;
    owner_id_t         *n;
    while ((n = *next) != NULL) {
        if (n->owner == owner) {
            return n->id;
        }
        next = &(n->next);
    }
    n = *next = RTmalloc (sizeof *n);
    n->id = fetch_add (&last_id, 1);
    n->owner = owner;
    n->next = NULL;
    return n->id;
}

static void
tls_destroy (void *arg)
{
    RTfree (arg);
}

int tls_get_cpu_count() {
    int numCPU;
#ifdef __APPLE__
    int mib[4];
    size_t len; 

    /* set the mib for hw.ncpu */
    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU;  // alternatively, try HW_AVAILCPU;

    /* get the number of CPUs from the system */
    sysctl(mib, 2, &numCPU, &len, NULL, 0);

    if( numCPU < 1 ) {
         mib[1] = HW_NCPU;
         sysctl( mib, 2, &numCPU, &len, NULL, 0 );
         if( numCPU < 1 ) {
              numCPU = 1;
         }
    }
#else
    numCPU = sysconf( _SC_NPROCESSORS_ONLN );
#endif
    return numCPU;
}

tls_t *
TLScreate (void *owner, size_t size)
{
    tls_t              *tls = RTmalloc (sizeof *tls);
    pthread_key_create (&tls->key, tls_destroy);
    tls->size = size;
    tls->id = get_owner_id (owner);
    assert (tls->id < TLS_MAX_INSTANCES);
    tls->owner = owner;
    return tls;
}

void *
TLSgetInstanceRef (tls_t *tls)
{
    void               *local = pthread_getspecific (tls->key);
    if (local == NULL) {
        size_t              size = tls->size * TLS_MAX_INSTANCES;
        local = RTalign (CACHE_LINE_SIZE, size);
        memset (local, 0, size);
        pthread_setspecific (tls->key, local);
    }
    return local + (tls->id * tls->size);
}

void
add_stats(stats_t *res, stats_t *stat)
{
    res->elts += stat->elts;
    res->tests += stat->tests;
    res->misses += stat->misses;
    res->rehashes += stat->rehashes;
    res->cache_misses += stat->cache_misses;
}
