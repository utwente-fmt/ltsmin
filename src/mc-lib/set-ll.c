#include <hre/config.h>

#include <stdbool.h>
#include <string.h>

#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/cctables.h>
#include <mc-lib/hashtable.h>
#include <mc-lib/is-balloc.h>
#include <mc-lib/set-ll.h>
#include <mc-lib/stats.h>
#include <util-lib/fast_hash.h>


#define MAX_WORKERS 64

typedef struct set_ll_slab_s {
    void               *mem;
    size_t              next;
    size_t              size;
    size_t              cur_len;
} set_ll_slab_t;

struct set_ll_allocator_s {
    set_ll_slab_t      **slabs;
};

set_ll_allocator_t *
set_ll_init_allocator ()
{
    hre_region_t        region = HREdefaultRegion(HREglobal());
    size_t              region_size = HREgetRegionSize(region);
    size_t              workers = HREpeers(HREglobal());
    size_t              size = region_size / 4 / workers;
    set_ll_allocator_t *alloc = HREmalloc(region, sizeof(set_ll_allocator_t *));
    Debug ("Allocating a slab of %zuMB for %zu workers", size >> 20, workers);
    alloc->slabs = HREmalloc(region, sizeof(void *[workers]));
    for (size_t i = 0; i < workers; i++) {
        alloc->slabs[i] = HREalign (region, CACHE_LINE_SIZE, sizeof(set_ll_slab_t));
        set_ll_slab_t      *slab = alloc->slabs[i];
        HREassert (slab != NULL, "Slab allocation failed.");
        slab->mem = HREmalloc (region, size);
        HREassert (slab->mem != NULL, "Slab memory allocation failed.");
        slab->next = 0;
        slab->size = size;
        slab->cur_len = SIZE_MAX;
    }
    return alloc;
}

static set_ll_allocator_t *static_allocator = NULL; //TODO extend datatype with reentrend functions

static uint32_t
strhash (const char *str)
{

    size_t              id = HREme(HREglobal());
    HREassert (static_allocator != NULL, "Failed to pass length via static.");
    set_ll_slab_t      *slab = static_allocator->slabs[id];
    size_t              len = slab->cur_len;
    uint64_t h = MurmurHash64(str, len, 0);
    return (h & 0xFFFFFFFF) ^ (h >> 32);
}

static char *
strclone (char *str)
{
    size_t              id = HREme(HREglobal());
    HREassert (static_allocator != NULL, "Failed to pass length via static.");
    set_ll_slab_t      *slab = static_allocator->slabs[id];
    char               *mem = (char *)slab->mem;
    char               *ptr = mem + slab->next;
    size_t              size = slab->cur_len + 1; // '\0'
    HREassert (slab->cur_len != SIZE_MAX, "Failed to pass length via static.");
    slab->next += size;
    HREassert (slab->next < slab->size, "Local slab of %zu from worker "
               "%zu exceeded: %zu", slab->size, id, slab->next);
    memmove (ptr, str, size);
    return (void *) ptr;
}

static void
strfree (char *str)
{
    Debug ("Deallocating %p.", str);
}

static const size_t INIT_HT_SCALE = 8;
static const datatype_t DATATYPE_HRE_STR = {
    (cmp_fun_t)strcmp,
    (hash_fun_t)strhash,
    (clone_fun_t)strclone,
    (free_fun_t)strfree
};

/**
 * Implementation:
 * A lockless hash map maintains the string to index map, while thread-specific
 * arrays (balloc[]) maintain the inverse mapping. A key is added to a threads
 * local array if it won the race to insert the key in the table.
 * Workers lookup the values found in the table in eachother's arrays, hence it
 * may be the case this value is not yet inserted by the array owner. In that
 * case a polling synchronization is initiated. This operation is cheap, since
 * writes are always to local arrays, while other threads only poll the state.
 */

typedef struct local_s {
    isb_allocator_t     balloc;
    size_t              count;
    char                pad[CACHE_LINE_SIZE - sizeof(size_t) - sizeof(isb_allocator_t)];
} local_t;

struct set_ll_s {
    hashtable_t        *ht;                 // Lockless hash table
    local_t             local[MAX_WORKERS]; // Local indexing arrays
    set_ll_allocator_t *alloc;
};

typedef struct str_s {
    char               *ptr;
    int                 len;
} __attribute__((__packed__)) str_t;

/**
 *
 * GLOBAL invariant: forall i : count[i] == size(balloc[i]) \/
 *                              count[i] == size(balloc[i]) + 1
 *
 * The first conjuct holds when a worker just installed a key/idx in the hash
 * table, but not yet in its local balloc. Immediately after such a situation
 * the worker insert such a key in balloc. The second conjunct then holds.
 */

char    *
set_ll_get  (set_ll_t *set, int idx, int *len)
{
    size_t              read;
    size_t              workers = HREpeers(HREglobal());
    size_t              worker = idx % workers;
    isb_allocator_t     balloc = set->local[worker].balloc;
    size_t              index = idx / workers;
    while ((read = atomic_read(&set->local[worker].count)) == index) {} // poll
    HREassert (index < read, "Invariant violated %zu !< %zu (idx=%d)", index, read, idx);
    // TODO: memory fence?
    int *res = isba_index (balloc, index);
    str_t              *str = (str_t *)isba_index (balloc, index);
    HREassert (res != NULL, "Value %d (%zu/%zu) not in lockless string set", idx, index, worker);
    *len = str->len;
    Debug ("Indx(%d)\t--(%zu,%zu)--> (%s,%d) %p", idx, worker, index, str->ptr, str->len, str->ptr);
    return str->ptr;
}

int
set_ll_put (set_ll_t *set, char *str, int len)
{
    size_t              worker = HREme (HREglobal());
    size_t              workers = HREpeers(HREglobal());
    isb_allocator_t     balloc = set->local[worker].balloc;
    size_t              index = set->local[worker].count;
    map_key_t           idx = index * workers + worker; // global index
    HREassert (idx < (1ULL<<32), "Exceeded int value range for chunk %s, the %zu'st insert for worker %zu", str, index, worker);

    // insert key in table
    map_key_t           clone;
    map_val_t           old;
    map_key_t           key = (map_key_t)str;
    set_ll_slab_t      *slab = set->alloc->slabs[worker];
    static_allocator = set->alloc;
    slab->cur_len = len; // avoid having to recompute the length;
    RTswitchAlloc (true); // in case the table resizes
    //insert idx+1 to avoid collision with DOES_NOT_EXIST:
    old = ht_cas_empty (set->ht, key, idx + 1, &clone);
    RTswitchAlloc (false);
    slab->cur_len = SIZE_MAX;
    static_allocator = NULL;

    if (old == DOES_NOT_EXIST) {
        // install value in balloc (late)
        str_t               string = {.ptr = (char *)clone, .len = len};
        RTswitchAlloc (true); // in case balloc allocates a block
        void *p = isba_push_int (balloc, (int*)&string);
        RTswitchAlloc (false);
        atomic_write (&set->local[worker].count, index + 1); // signal done
        Debug ("Wrot(%zu)\t<--(%zu,%zu)-- (%s,%d) %p", idx, worker, index, str, len, clone);
    } else {
        idx = old - 1;
        worker = idx % workers;
        index = idx / workers;
        Debug ("WrRd(%zu)\t<--(%zu,%zu)-- (%s,%d) %p", idx, worker, index, str, len, clone);
    }
    return (int)idx;
}

int
set_ll_count(set_ll_t *set)
{
    HREassert(false, "set_ll_count not implemented");
    (void) set;
}

set_ll_t*
set_ll_create(set_ll_allocator_t *alloc)
{
    HREassert (sizeof(str_t) == 12);
    set_ll_t *set = HREmalloc (HREdefaultRegion(HREglobal()), sizeof(set_ll_t));
    set->alloc = alloc;

    RTswitchAlloc(true); // global allocation of table
    set->ht = ht_alloc (&DATATYPE_HRE_STR, INIT_HT_SCALE);
    RTswitchAlloc(false);

    RTswitchAlloc(true); // global allocation of ballocs
    for (int i = 0; i < HREpeers(HREglobal()); i++) {
        // a pointer to the string and its length (int) will be put on a balloc:
        set->local[i].balloc = isba_create(sizeof(char *) / sizeof(int) + 1);
        set->local[i].count = 0;
    }
    RTswitchAlloc(false);
    return set;
}

void
set_ll_destroy(set_ll_t *set)
{
    RTswitchAlloc(true); // global deallocation of table
    ht_free (set->ht);
    RTswitchAlloc(false);

    RTswitchAlloc(true); // global deallocation of ballocs
    for (int i = 0; i < HREpeers(HREglobal()); i++)
        isba_destroy (set->local[i].balloc);
    RTswitchAlloc(false);

    HREfree (HREdefaultRegion(HREglobal()), set);
}
