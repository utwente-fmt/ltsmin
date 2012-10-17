#include <config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hre/runtime.h>
#include <util-lib/fast_set.h>
#include <util-lib/util.h>

typedef hash32_t mem_hash_t;

struct fset_s {
    size_t              key_length;
    size_t              data_length;
    size_t              total_length;
    size_t              size;
    size_t              size3;
    size_t              init_size;
    size_t              init_size3;
    size_t              mask;
    size_t              size_max;
    mem_hash_t         *hash;
    void               *data;
    mem_hash_t         *todo;
    void               *todo_data;
    size_t              load;
    size_t              tombs;
    size_t              lookups;
    size_t              probes;
    size_t              resizes;
    size_t              max_grow;
    size_t              max_load;
    size_t              max_todos;
    int                 resizing;
    rt_timer_t          timer;
};

static const size_t CACHE_LINE_MEM_SIZE = CACHE_LINE_SIZE / sizeof (mem_hash_t);
static const size_t CACHE_LINE_MEM_MASK = -(CACHE_LINE_SIZE / sizeof (mem_hash_t));

#define EMPTY 0UL
static const mem_hash_t FULL  = ((mem_hash_t)-1) ^ (((mem_hash_t)-1)>>1);// 1000
static const mem_hash_t NFULL = ((mem_hash_t)-1) >> 1;                   // 0111
static const mem_hash_t TOMB  = 1UL;                                     // 0001
static const size_t NONE      = -1UL;

static inline mem_hash_t *
memoized (const fset_t *dbs, size_t ref)
{
    return &dbs->hash[ref];
}

static inline void *
state (const fset_t *dbs, void *data, size_t ref)
{
    return ((char*)data) + (ref * dbs->total_length);
}

static const char *fset_resize_names[3] = { "Grow", "Shrink", "Rehash" };

typedef enum fset_resize_e {
    GROW,
    SHRINK,
    REHASH
} fset_resize_t;

static int
resize (fset_t *dbs, fset_resize_t mode)
{
    void               *key, *data;
    bool                res;
    if (dbs->resizing) return true;
    dbs->resizing = 1;

    size_t              old_size = dbs->size;
    switch (mode) {
    case GROW:
        if (dbs->size == dbs->size_max) {
            dbs->resizing = 0;
            return false;
        }
        memset (dbs->hash + dbs->size, 0, sizeof (mem_hash_t[dbs->size]));
        dbs->size <<= 1;
        dbs->size3 <<= 1;
        break;
    case SHRINK:
        if (dbs->size == dbs->init_size) {
            dbs->resizing = 0;
            return true;
        }
        dbs->size >>= 1;
        dbs->size3 >>= 1;
        break;
    case REHASH: break;
    }
    dbs->mask = dbs->size - 1;
    Debug ("%s %zu to %zu", fset_resize_names[mode], old_size, dbs->size);

    //RTstartTimer (dbs->timer);
    size_t              tombs = 0;
    size_t              todos = 0;
    for (size_t i = 0; i < old_size; i++) {
        mem_hash_t          h = *memoized(dbs,i);
        if (TOMB == h) {
            tombs++;
        } else if (h != EMPTY) {// && (h & dbs->mask) != i) {
            dbs->todo[todos] = *memoized(dbs,i);
            void               *data = state(dbs, dbs->todo_data, todos);
            memcpy (data, state(dbs, dbs->data, i), dbs->total_length);
            todos++;
        }
        *memoized(dbs, i) = EMPTY;
    }
    dbs->tombs -= tombs;
    dbs->load  -= todos;
    HREassert (dbs->tombs == 0);
    HREassert (dbs->tombs < 1ULL << 32); // overflow
    HREassert (dbs->load < 1ULL << 32); // overflow

    for (size_t i = 0; i < todos; i++) {
        mem_hash_t          h = dbs->todo[i] & NFULL;
        key = state(dbs, dbs->todo_data, i);
        data = key + dbs->key_length;
        res = fset_find (dbs, &h, key, &data, true); // load++
        HREassert (!res);
        //HREassert (fset_find (dbs, &h, data, false));
        //HREassert (fset_find (dbs, &h, data, true));
    }

    //RTstopTimer (dbs->timer);
    Debug ("%s %zu to %zu took %zu/%zu todos and cleaned %zu/%zu tombstones in %.2f sec",
           fset_resize_names[mode], old_size, dbs->size, todos, dbs->load, tombs,
           dbs->load + tombs, RTrealTime(dbs->timer));
    dbs->max_todos = max (todos, dbs->max_todos);
    dbs->max_grow = max (dbs->max_grow, dbs->size);
    dbs->resizes++;
    dbs->resizing = 0;
    return true;
}

static inline mem_hash_t
rehash (mem_hash_t h, mem_hash_t v)
{
    return h + (primes[v & ((1<<9)-1)] << FSET_MIN_SIZE);
}

static bool
fset_find_loc (fset_t *dbs, mem_hash_t mem, void *data, size_t *ref,
               size_t *tomb)
{
    size_t              b = dbs->key_length;
    mem |= FULL;
    if (tomb) *tomb = NONE;
    mem_hash_t          h = mem;
    size_t              rh = 0;
    dbs->lookups++;
    //Debug ("Locating key %zu,%zu with hash %u", ((size_t*)data)[0], ((size_t*)data)[1], mem);
    while (true) {
        *ref = h & dbs->mask;
        size_t              line_begin = *ref & CACHE_LINE_MEM_MASK;
        size_t              line_end = line_begin + CACHE_LINE_MEM_SIZE;
        for (size_t i = 0; i < CACHE_LINE_MEM_SIZE; i++) {
            dbs->probes++;
            if (NULL != tomb && TOMB == *memoized(dbs,*ref))
                *tomb = *ref; // first found tombstone
            if (EMPTY == *memoized(dbs,*ref))
                return false;
            if ( (mem == *memoized(dbs,*ref)) &&
                 (b == 0 || memcmp (data, state(dbs,dbs->data,*ref), b) == 0) )
                return true;
            *ref = (*ref+1 == line_end ? line_begin : *ref+1);
        }
        h = rehash (h, mem);
        if (rh++ > 1000) {
            return FSET_FULL;
        }
    }
}

bool
fset_delete (fset_t *dbs, mem_hash_t *mem, void *key)
{
    size_t              ref;
    size_t              b = dbs->key_length;
    mem_hash_t          h = (mem == NULL ? MurmurHash64(key, b, 0) : *mem);
    bool found = fset_find_loc (dbs, h, key, &ref, NULL);
    if (!found)
        return false;
    *memoized(dbs,ref) = TOMB; // TODO: avoid TOMB when next in line is EMPTY
    dbs->tombs++;
    dbs->load--;
    if (dbs->load < dbs->size >> 3 && dbs->size != dbs->init_size) {
        bool res = resize (dbs, SHRINK);                // <12.5% keys ==> shrink
        HREassert (res, "Cannot shrink table?");
    } else if (dbs->tombs << 2 > dbs->size3) {
        bool res = resize (dbs, REHASH);                // >75% tombs ==> rehash
        HREassert (res, "Cannot rehash table?");
    }
    return true;
}

int
fset_find (fset_t *dbs, mem_hash_t *mem, void *key, void **data,
           bool insert_absert)
{
    size_t              ref;
    size_t              tomb;
    size_t              k = dbs->key_length;
    size_t              d = dbs->data_length;
    mem_hash_t          h = (mem == NULL ? MurmurHash64(key, k, 0) : *mem);
    bool found = fset_find_loc (dbs, h, key, &ref, &tomb);
    void *data_res = state(dbs, dbs->data, ref) + k;
    if (!insert_absert || found) {
        *data = data_res;
        return found;
    }

    // insert:
    HREassert (dbs->data_length == 0 || data);
    if (tomb != NONE) {
        ref = tomb;
        dbs->tombs--;
    }
    if (dbs->key_length)
        memcpy (state(dbs, dbs->data, ref), key, k);
    if (dbs->data_length) {
        memcpy (data_res, *data, d);
        *data = data_res;
    }
    *memoized(dbs, ref) = h | FULL;
    dbs->load++;
    dbs->max_load = max (dbs->max_load, dbs->load);
    if (((dbs->load + dbs->tombs) << 2) > dbs->size3) {
        if (!resize(dbs, GROW)) {                       // > 75% full ==> grow
            Debug ("Hash table almost full (size = %zu, load = %zu, tombs = %zu)",
                   dbs->size, dbs->load, dbs->tombs);
        }
    }
    return false;
}

size_t
fset_count (fset_t *dbs)
{
    return dbs->load;
}

void
fset_clear (fset_t *dbs)
{
    dbs->load = 0;
    dbs->tombs = 0;
    dbs->size = dbs->init_size;
    dbs->size3 = dbs->init_size3;
    dbs->mask = dbs->size - 1;
    memset (dbs->hash, 0, sizeof(mem_hash_t) * dbs->size);
}

fset_t *
fset_create (size_t key_length, size_t data_length, size_t init_size,
             size_t max_size)
{
    HREassert (true == 1);
    HREassert (false == 0);
    HREassert (sizeof(mem_hash_t) == 4);// CACHE_MEM
    HREassert (max_size < 32);          // FULL bit
    HREassert (init_size <= max_size);  // precondition
    HREassert (init_size >= FSET_MIN_SIZE); // walk the line code
    fset_t           *dbs = RTalign (CACHE_LINE_SIZE, sizeof(fset_t));
    dbs->key_length = key_length;
    dbs->data_length = data_length;
    dbs->total_length = data_length + key_length;
    dbs->size_max = 1UL << max_size;
    if (dbs->total_length) {
        dbs->data = RTalign (CACHE_LINE_SIZE, dbs->total_length * dbs->size_max);
        dbs->todo_data = RTalign (CACHE_LINE_SIZE, dbs->total_length * dbs->size_max);
    }
    dbs->hash = RTalignZero (CACHE_LINE_SIZE, sizeof(mem_hash_t) * dbs->size_max);
    dbs->todo = RTalign (CACHE_LINE_SIZE, 2 * sizeof(mem_hash_t) * dbs->size_max);
    dbs->init_size = 1UL<<init_size;
    dbs->init_size3 = dbs->init_size * 3;
    dbs->size = dbs->init_size;
    dbs->size3 = dbs->init_size3;
    dbs->mask = dbs->size - 1;
    dbs->load = 0;
    dbs->tombs = 0;
    dbs->resizing = 0;
    dbs->resizes = 0;
    dbs->max_grow = dbs->init_size;
    dbs->max_load = 0;
    dbs->max_todos = 0;
    dbs->lookups = 0;
    dbs->probes = 0;
    dbs->timer = RTcreateTimer ();
    //fset_clear (dbs); // mallocZero
    max_size = (size_t)fset_resize_names[0]; // avoid warning unused
    return dbs;
}

void
fset_print_statistics (fset_t *dbs, char *s)
{
    Warning (info, "%s max load = %zu, max_size = %zu, scratch pad = %zu, "
             "resizes = %zu, probes/lookup = %.2f", s,
             dbs->max_load, dbs->max_grow, dbs->max_todos, dbs->resizes,
             (float)dbs->probes / dbs->lookups/*, RTrealTime(dbs->timer)*/);
}

size_t
fset_mem (fset_t *dbs)
{
    return sizeof(mem_hash_t[dbs->max_grow]) +
           sizeof(mem_hash_t[dbs->max_todos]);
}

void
fset_free (fset_t *dbs)
{
    RTfree (dbs->data);
    RTfree (dbs->hash);
    RTfree (dbs);
}
