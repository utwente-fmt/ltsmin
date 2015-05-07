#include <hre/config.h>

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/cctables.h>
#include <mc-lib/hashtable.h>
#include <mc-lib/set-ll.h>
#include <mc-lib/statistics.h>
#include <mc-lib/stats.h>
#include <util-lib/fast_hash.h>
#include <util-lib/is-balloc.h>
#include <util-lib/util.h>


#define MAX_WORKERS 64
#define SLABS_RATIO 2

typedef struct set_ll_slab_s {
    void               *mem;
    size_t              next;
    size_t              total;
    size_t              size;
    size_t              cur_len;
    char               *cur_key;
} set_ll_slab_t;

struct set_ll_allocator_s {
    set_ll_slab_t     **slabs;
    bool                shared;
};

set_ll_allocator_t *
set_ll_init_allocator (bool shared)
{
    size_t              workers = HREpeers(HREglobal());
    size_t              region_size = 0;
    hre_region_t        region = HREdefaultRegion(HREglobal());
    if (shared && region != NULL) {
        region_size = HREgetRegionSize(region);
    } else {
        region_size = RTmemSize();
    }
    size_t              size = region_size / SLABS_RATIO / workers;

    RTswitchAlloc (shared); // global allocation of allocator?
    set_ll_allocator_t *alloc = RTmalloc (sizeof(set_ll_allocator_t));
    Debug ("Allocating a slab of %zuMB for %zu workers", size >> 20, workers);
    alloc->slabs = RTmalloc (sizeof(void *[workers]));
    for (size_t i = 0; i < workers; i++) {
        alloc->slabs[i] = RTalign (CACHE_LINE_SIZE, sizeof(set_ll_slab_t));
        set_ll_slab_t      *slab = alloc->slabs[i];
        HREassert (slab != NULL, "Slab allocation failed.");
        slab->mem = RTmalloc (size);
        HREassert (slab->mem != NULL, "Slab memory allocation failed. Increase your user limits or SLABS_RATIO");
        slab->next = 0;
        slab->total = 0;
        slab->size = size;
        slab->cur_len = SIZE_MAX;
    }
    RTswitchAlloc (false);

    alloc->shared = shared;
    return alloc;
}

/**
 * We might receive stings from the hash table that are not the one currently
 * being inserted, for example during a resize. This function remedies that.
 */
static size_t
get_length (char *str, set_ll_slab_t *slab)
{
    size_t              len = 0;
    if (str == slab->cur_key) {
        len = slab->cur_len;
        HREassert (len == strlen(str), "Incorrect length passed for '%s', %zu instead of %zu. Rest: '%s'.",
                   str, len, strlen(str), &str[len]);
    } else {
        len = strlen(str);
    }
    return len;
}

static uint32_t
strhash (char *str, set_ll_slab_t *slab)
{
    size_t              len = get_length (str, slab);
    HRE_ASSERT (len == strlen(str), "Incorrect length passed for '%s', %zu instead of %zu. Rest: '%s'",
                str, len, strlen(str), &str[len+1]);
    uint64_t h = MurmurHash64(str, len, 0);
    return (h & 0xFFFFFFFF) ^ (h >> 32);
}

static char *
strclone (char *str, set_ll_slab_t *slab)
{
    char               *mem = (char *)slab->mem;
    char               *ptr = mem + slab->next;
    size_t              len = get_length (str, slab) + 1; // '\0'
    HREassert (slab->cur_len != SIZE_MAX, "Failed to pass length via static.");
    slab->next += len;
    slab->total++;
    HREassert (slab->next < slab->size, "Local slab of %zu from worker "
               "%d exceeded: %zu", slab->size, HREme(HREglobal()), slab->next);
    memmove (ptr, str, len);
    return (void *) ptr;
}

static void
strfree (char *str)
{
    Debug ("Deallocating %p.", str);
    (void) str;
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
    size_t              count;  // TODO: lazy updates with positive feedback
    size_t              offset;
    char                pad[CACHE_LINE_SIZE - 2*sizeof(size_t) - sizeof(isb_allocator_t)];
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
set_ll_get (set_ll_t *set, int idx, int *len)
{
    size_t              read;
    size_t              workers = HREpeers(HREglobal());
    size_t              worker = idx % workers;
    isb_allocator_t     balloc = set->local[worker].balloc;
    size_t              index = idx / workers;
    size_t              offset = set->local[worker].offset;
    while ((read = atomic_read(&set->local[worker].count)) == index) {} // poll
    HREassert (index < read, "Invariant violated %zu !< %zu (idx=%d)", index, read, idx);
    // TODO: memory fence?
    str_t              *str = (str_t *)isba_index (balloc, index - offset);
    HREassert (str != NULL, "Value %d (%zu/%zu) not in lockless string set", idx, index, worker);
    *len = str->len;
    Debug ("Index(%d)\t--(%zu,%zu)--> (%s,%d) %p",
           idx, worker, index, str->ptr, str->len, str->ptr);
    HRE_ASSERT (str->len == (int)strlen(str->ptr), "Incorrect length passed for '%s', %d instead of %zu. Rest: '%s'",
                str->ptr, str->len, strlen(str->ptr), &str->ptr[str->len+1]);
    return str->ptr;
}

int
set_ll_put (set_ll_t *set, char *str, int len)
{
    HRE_ASSERT (len == (int)strlen(str), "Incorrect length passed for '%s', %d instead of %zu. Rest: '%s'",
                str, len, strlen(str), &str[len+1]);
    hre_context_t       global = HREglobal ();
    size_t              worker = HREme (global);
    size_t              workers = HREpeers (global);
    isb_allocator_t     balloc = set->local[worker].balloc;
    size_t              index = set->local[worker].count;
    uint64_t            value = (uint64_t)index * workers + worker;
    HREassert (value < (1ULL<<32), "Exceeded int value range for chunk %s, the %zu'st insert for worker %zu", str, index, worker);
    map_key_t           idx = value; // global index

    // insert key in table
    map_key_t           clone;
    map_val_t           old;
    map_key_t           key = (map_key_t)str;
    set_ll_slab_t      *slab = set->alloc->slabs[worker];

    slab->cur_key = str;
    slab->cur_len = len; // avoid having to recompute the length
    RTswitchAlloc (set->alloc->shared); // in case the table resizes
    //insert idx+1 to avoid collision with DOES_NOT_EXIST:
    old = ht_cas_empty (set->ht, key, idx + 1, &clone, slab);
    RTswitchAlloc (false);
    slab->cur_len = SIZE_MAX;

    if (old == DOES_NOT_EXIST) {
        // install value in balloc (late)
        str_t               string = {.ptr = (char *)clone, .len = len};
        RTswitchAlloc (set->alloc->shared); // in case balloc allocates a block
        isba_push_int (balloc, (int*)&string);
        RTswitchAlloc (false);
        atomic_write (&set->local[worker].count, index + 1); // signal done
        Debug ("Write(%zu)\t<--(%zu,%zu)-- (%s,%d) %p",
               (size_t)idx, worker, index, str, len, (void*)clone);
    } else {
        idx = old - 1;
        Debug ("Find (%zu)\t<--(%zu,%zu)-- (%s,%d) %p", (size_t)idx,
               (size_t)idx % workers, (size_t)idx / workers, str, len, (void *)clone);
    }
    return (int)idx;
}

static int
set_ll_max (set_ll_t *set)
{
    size_t              workers = HREpeers (HREglobal());
    size_t              references = 0;
    for (size_t i = 0; i < workers; i++)
        if (references < set->local[i].count) references = set->local[i].count;
    return references;
}

static double
set_ll_stdev (set_ll_t *set)
{
    size_t              workers = HREpeers (HREglobal());
    statistics_t stats;
    statistics_init(&stats);
    for (size_t i = 0; i < workers; i++)
        statistics_record(&stats, set->local[i].count);
    return statistics_stdev(&stats);
}


int
set_ll_count (set_ll_t *set)
{
    size_t              workers = HREpeers (HREglobal());
    size_t              references = 0;
    for (size_t i = 0; i < workers; i++)
        references += set->local[i].count;
    Debug ("Count %p: %zu", set ,references);
    return references;
}

void
set_ll_install (set_ll_t *set, char *name, int len, int idx)
{
    size_t              workers = HREpeers (HREglobal());
    size_t              worker = idx % workers;
    size_t              index = idx / workers;
    size_t              offset = set->local[worker].offset;
    isb_allocator_t     balloc = set->local[worker].balloc;
    if ((size_t)idx < set->local[worker].count) {
        str_t              *str = (str_t *)isba_index (balloc, index - offset);
        HREassert (str, "Corruption in set.");
        HREassert (strncmp(str->ptr, name, min(str->len,len)) == 0,
                   "String '%s' already inserted at %d, while trying to insert "
                   "'%s' there", str->ptr, idx, name);
        return;
    }
    set_ll_slab_t      *slab = set->alloc->slabs[worker];
    map_key_t           clone, old, key = (map_key_t)name;

    slab->cur_key = name;
    slab->cur_len = len; // avoid having to recompute the length
    RTswitchAlloc (set->alloc->shared);
    old = ht_cas_empty (set->ht, key, idx + 1, &clone, slab);
    RTswitchAlloc (false);
    slab->cur_len = SIZE_MAX;

    if (old - 1 == (size_t)idx)
        return;
    HREassert (old == DOES_NOT_EXIST);
    str_t               string = {.ptr = (char *)clone, .len = len};

    RTswitchAlloc (set->alloc->shared);
    isba_push_int (balloc, (int*)&string);
    RTswitchAlloc (false);
    set->local[worker].offset += index - set->local[worker].count;
    atomic_write (&set->local[worker].count, index + 1); // signal done

    Debug ("Bind (%d)\t<--(%zu,%zu)-> (%s,%d) %zu", idx, worker, index, name,
                                                     len, (size_t) clone);
}

double
set_ll_finalize (set_ll_t *set, char *bogus)
{
    size_t              workers = HREpeers (HREglobal());
    size_t              max_local = set_ll_max (set);
    set_ll_slab_t      *slab = set->alloc->slabs[0];
    HREassert (strlen(bogus) < LTSMIN_PATHNAME_MAX - strlen("4294967296"));
    char                unique[LTSMIN_PATHNAME_MAX];
    size_t              count = 0;

    size_t              added = 0;
    for (size_t i = 0; i < workers; i++) {
        isb_allocator_t     balloc = set->local[i].balloc;
        RTswitchAlloc (set->alloc->shared);
        while (set->local[i].count < max_local) {
            snprintf (unique, LTSMIN_PATHNAME_MAX, "%s (unique %zu)", bogus, count++);
            slab->cur_key = NULL;
            slab->cur_len = strlen(unique);
            str_t               str = { .ptr = strclone (unique, slab),
                                        .len = strlen(unique) };
            slab->cur_len = SIZE_MAX;


            isba_push_int (balloc, (int*)&str);
            set->local[i].count++;
        }
        RTswitchAlloc (false);
    }
    double              underwater = workers * max_local;
    underwater /= underwater - added;
    return underwater;
}

size_t
set_ll_print_stats (log_t log, set_ll_t *set, char *name)
{
    size_t              references = set_ll_count (set); // balloc references
    double              stdev = set_ll_stdev (set);
    size_t              alloc = ht_size (set->ht); // table allocation size in byte
    size_t              table = references * sizeof(map_key_t) * 2;
    Warning (log, "String table '%s': %zuMB pointers, %zuMB "
             "tables (%.2f%% overhead), %zu strings (distr.: %.2f)", name,
             (references * sizeof(str_t)) >> 20, table >> 20,
             (double)alloc / table * 100, references, stdev);
    return alloc + references * sizeof(str_t);
}


size_t
set_ll_print_alloc_stats(log_t log, set_ll_allocator_t *alloc)
{
    size_t              workers = HREpeers(HREglobal());
    size_t              strings = 0; // string storage in byte
    size_t              count = 0; // number of strings
    for (size_t i = 0; i < workers; i++) {
        strings += alloc->slabs[i]->next;
        count += alloc->slabs[i]->total;
    }
    Warning (log, "Stored %zu string chucks using %zuMB", count, strings >> 20);
    return strings;
}

set_ll_t *
set_ll_create (set_ll_allocator_t *alloc)
{
    RTswitchAlloc (alloc->shared); // global allocation of table, ballocs and set
    set_ll_t           *set = RTmalloc (sizeof(set_ll_t));
    set->ht = ht_alloc (&DATATYPE_HRE_STR, INIT_HT_SCALE);
    for (int i = 0; i < HREpeers(HREglobal()); i++) {
        set->local[i].balloc = isba_create(sizeof(str_t) / sizeof(int));
        set->local[i].count = 0;
        set->local[i].offset = 0;
    }
    RTswitchAlloc (false);

    set->alloc = alloc;

    return set;
}

void
set_ll_destroy (set_ll_t *set)
{
    RTswitchAlloc (set->alloc->shared); // global deallocation of table, ballocs and set
    ht_free (set->ht);
    for (int i = 0; i < HREpeers(HREglobal()); i++)
        isba_destroy (set->local[i].balloc);
    RTfree (set);
    RTswitchAlloc (false);
}
