/*
 * Written by Josh Dybnis and released to the public domain, as explained at
 * http://creativecommons.org/licenses/publicdomain
 *
 * C implementation of Cliff Click's lock-free hash table from
 * http://www.azulsystems.com/events/javaone_2008/2008_CodingNonBlock.pdf
 * http://sourceforge.net/projects/high-scale-lib
 *
 * Note: This is code uses synchronous atomic operations because that is all that x86 provides.
 * Every atomic operation is also an implicit full memory barrier. The upshot is that it simplifies
 * the code a bit, but it won't be as fast as it could be on platforms that provide weaker
 * operations like unfenced CAS which would still do the job.
 *
 * 11FebO9 - Bug fix in ht_iter_next() from Rui Ueyama
 */

#include <hre/config.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include <hre/user.h>
#include <util-lib/fast_hash.h>
#include <mc-lib/atomics.h>
#include <mc-lib/hashtable.h>

///////////////////////////////////////////////////////////////////////////////

#define MASK(n)     ((1ULL << (n)) - 1)

#ifndef NBD32
#define GET_PTR(x) ((void *)((x) & MASK(48))) // low-order 48 bits is a pointer to a nstring_t
#else
#define GET_PTR(x) ((void *)(x))
#endif

#ifdef NBD_DEBUG
#define TRACE(a, ...)       Debug(__VA_ARGS__)
#else
#define TRACE(a, ...)       ((void)0)
#endif

#ifdef NBD32
#define TAG1         (1U << 31)
#define TAG2         (1U << 30)
#else
#define TAG1         (1ULL << 63)
#define TAG2         (1ULL << 62)
#endif
#define TAG_VALUE(v, tag) ((v) |  tag)
#define IS_TAGGED(v, tag) ((v) &  tag)
#define STRIP_TAG(v, tag) ((v) & ~tag)

static const map_val_t  COPIED_VALUE    = TAG_VALUE(DOES_NOT_EXIST, TAG1);
static const map_val_t  TOMBSTONE       = STRIP_TAG(-1, TAG1);

///////////////////////////////////////////////////////////////////////////////

typedef struct entry {
    map_key_t key;
    map_val_t val;
} entry_t;

typedef struct hti {
    volatile entry_t *table;
    hashtable_t *ht; // parent ht;
    struct hti *next;
    //size_t count; // TODO: make these counters distributed
    size_t key_count;
    size_t copy_scan;
    size_t num_entries_copied;
    int probe;
    int ref_count;
    uint8_t scale;
} hti_t;

struct ht_iter {
    hti_t *  hti;
    int64_t  idx;
};

struct ht {
    hti_t *hti;
    hti_t *first;
    const datatype_t *key_type;
    uint32_t hti_copies;
    double density;
    int probe;
};

static const unsigned ENTRIES_PER_BUCKET     = CACHE_LINE_SIZE/sizeof(entry_t);
static const unsigned ENTRIES_PER_COPY_CHUNK = CACHE_LINE_SIZE/sizeof(entry_t)*2;
static const unsigned MIN_SCALE              = 4; // min 16 entries (4 buckets)

static int hti_copy_entry (hti_t *ht1, volatile entry_t *ent,
                           uint32_t ent_key_hash, hti_t *ht2, void *ctx);

static inline void
nbd_free (void *x)
{
    TRACE("m1", "nbd_free: %p", x, 0);
#ifndef NDEBUG
    memset(x, 0xcd, sizeof(void *)); // bear trap
#endif//NDEBUG
    RTalignedFree(x);
    return;
}

static inline void *
nbd_malloc (size_t n) {
    TRACE("m1", "nbd_malloc: request size %llu", n, 0);
    void *x = RTalignZero (CACHE_LINE_SIZE, n);
    TRACE("m1", "nbd_malloc: returning %p", x, 0);
    HREassert(x, "Allocation failed!");
    return x;
}

// Choose the next bucket to probe using the high-order bits of <key_hash>.
static inline int
get_next_ndx(int old_ndx, uint32_t key_hash, int ht_scale)
{
#if 1
    unsigned incr = (key_hash >> (32 - ht_scale));
    if (incr < ENTRIES_PER_BUCKET) { incr += ENTRIES_PER_BUCKET; }
    return (old_ndx + incr) & MASK(ht_scale);
#else
    return (old_ndx + ENTRIES_PER_BUCKET) & MASK(ht_scale);
#endif
}


static inline uint64_t
hash32 (hashtable_t *ht, map_key_t key, void *ctx)
{
    return (ht->key_type->hash == NULL) ?
            MurmurHash32(&key, sizeof(map_key_t), 0) :
            ht->key_type->hash((void *)key, ctx);
}

// Lookup <key> in <hti>.
//
// Return the entry that <key> is in, or if <key> isn't in <hti> return the entry that it would be
// in if it were inserted into <hti>. If there is no room for <key> in <hti> then return NULL, to
// indicate that the caller should look in <hti->next>.
//
// Record if the entry being returned is empty. Otherwise the caller will have to waste time
// re-comparing the keys to confirm that it did not lose a race to fill an empty entry.
static volatile entry_t *hti_lookup (hti_t *hti, map_key_t key, uint32_t key_hash,
                                     int *is_empty, void *ctx) {
    TRACE("h2", "hti_lookup(key %p in hti %p)", key, hti);
    *is_empty = 0;

    // Probe one cache line at a time
    int ndx = key_hash & MASK(hti->scale); // the first entry to search
    for (int i = 0; i < hti->probe; ++i) {

        // The start of the bucket is the first entry in the cache line.
        volatile entry_t *bucket = hti->table + (ndx & ~(ENTRIES_PER_BUCKET-1));

        // Start searching at the indexed entry. Then loop around to the begining of the cache line.
        for (unsigned j = 0; j < ENTRIES_PER_BUCKET; ++j) {
            volatile entry_t *ent = bucket + ((ndx + j) & (ENTRIES_PER_BUCKET-1));

            map_key_t ent_key = ent->key;
            if (ent_key == DOES_NOT_EXIST) {
                TRACE("h1", "hti_lookup: entry %p for key %p is empty", ent,
                            (hti->ht->key_type == NULL) ? (void *)key : GET_PTR(key));
                *is_empty = 1; // indicate an empty so the caller avoids an expensive key compare
                return ent;
            }

            // Compare <key> with the key in the entry.
            if (EXPECT_TRUE(hti->ht->key_type->cmp == NULL)) {
                // fast path for integer keys
                if (ent_key == key) {
                    TRACE("h1", "hti_lookup: found entry %p with key %p", ent, ent_key);
                    return ent;
                }
            } else {
#ifndef NBD32
                // The key in <ent> is made up of two parts. The 48 low-order bits are a pointer. The
                // high-order 16 bits are taken from the hash. The bits from the hash are used as a
                // quick check to rule out non-equal keys without doing a complete compare.
                if ((key_hash >> 16) == (ent_key >> 48)) {
#endif
                    if (hti->ht->key_type->cmp(GET_PTR(ent_key), (void *)key, ctx) == 0) {
                        TRACE("h1", "hti_lookup: found entry %p with key %p", ent, GET_PTR(ent_key));
                        return ent;
#ifndef NBD32
                    }
#endif
                }
            }
        }

        ndx = get_next_ndx(ndx, key_hash, hti->scale);
    }

    // maximum number of probes exceeded
    TRACE("h1", "hti_lookup: maximum number of probes exceeded returning 0x0", 0, 0);
    return NULL;
}

// Allocate and initialize a hti_t with 2^<scale> entries.
static hti_t *hti_alloc (hashtable_t *parent, size_t scale) {
    Debug ("initializing to %llu elements", 1ULL << scale);
    hti_t *hti = (hti_t *)nbd_malloc(sizeof(hti_t));
    //memset(hti, 0, sizeof(hti_t)); // ensured by HREalignZero
    hti->scale = scale;

    size_t sz = sizeof(entry_t) * (1ULL << scale);
    hti->table = nbd_malloc(sz);
    //memset((void *)hti->table, 0, sz); // ensured by HREalignZero

    hti->probe = (int)(hti->scale * 1.5) + 2;
    int quarter = (1ULL << (hti->scale - 2)) / ENTRIES_PER_BUCKET;
    if (hti->probe > quarter && quarter > 4) {
        // When searching for a key probe a maximum of 1/4
        hti->probe = quarter;
    }
    HRE_ASSERT(hti->probe);
    hti->ht = parent;
    hti->ref_count = 1; // one for the parent
    HREassert(hti->scale >= MIN_SCALE && hti->scale < 63); // size must be a power of 2
    HREassert(sizeof(entry_t) * ENTRIES_PER_BUCKET % CACHE_LINE_SIZE == 0); // divisible into cache
    HREassert((((size_t)hti->table) % CACHE_LINE_SIZE) == 0,
              "Table %p not aligned at %d", hti->table, CACHE_LINE_SIZE); // cache aligned

    return hti;
}

// Called when <hti> runs out of room for new keys.
//
// Initiates a copy by creating a larger hti_t and installing it in <hti->next>.
static void hti_start_copy (hti_t *hti) {
    TRACE("h0", "hti_start_copy(hti %p scale %llu)", hti, hti->scale);


    // heuristics to determine the size of the new table
    size_t count = ht_count(hti->ht);
    unsigned int new_scale = hti->scale;
    new_scale += (count > (1ULL << (hti->scale - 1))) || (hti->key_count > (1ULL << (hti->scale - 2)) + (1ULL << (hti->scale - 3))); // double size if more than 1/2 full

    Debug ("Resizing table from %llu to %llu", (1ULL << hti->scale), (1ULL << new_scale));

    // Allocate the new table and attempt to install it.
    hti_t *next = hti_alloc(hti->ht, new_scale);
    hti_t *old_next = cas_ret(&hti->next, NULL, next);
    if (old_next != NULL) {
        // Another thread beat us to it.
        TRACE("h0", "hti_start_copy: lost race to install new hti; found %p", old_next, 0);
        nbd_free((void *)next->table);
        return;
    }
    TRACE("h0", "hti_start_copy: new hti %p scale %llu", next, next->scale);
    add_fetch(&hti->ht->hti_copies, 1);
    hti->ht->density = (double)hti->key_count / (1ULL << hti->scale) * 100;
    hti->ht->probe = hti->probe;
}

// Copy the key and value stored in <ht1_ent> (which must be an entry in <ht1>) to <ht2>.
//
// Return 1 unless <ht1_ent> is already copied (then return 0), so the caller can account for the total
// number of entries left to copy.
static int hti_copy_entry (hti_t *ht1, volatile entry_t *ht1_ent,
                           uint32_t key_hash, hti_t *ht2, void *ctx) {
    TRACE("h2", "hti_copy_entry: entry %p to table %p", ht1_ent, ht2);
    HREassert(ht1);
    HREassert(ht1->next);
    HREassert(ht2);
    HREassert(ht1_ent >= ht1->table && ht1_ent < ht1->table + (1ULL << ht1->scale));
#ifndef NBD32
    HREassert(key_hash == 0 || ht1->ht->key_type->hash == NULL ||
              (key_hash >> 16) == (ht1_ent->key >> 48));
#endif

    map_val_t ht1_ent_val = ht1_ent->val;
    if (EXPECT_FALSE(ht1_ent_val == COPIED_VALUE || ht1_ent_val == TAG_VALUE(TOMBSTONE, TAG1))) {
        TRACE("h1", "hti_copy_entry: entry %p already copied to table %p", ht1_ent, ht2);
        return false; // already copied
    }

    // Kill empty entries.
    if (EXPECT_FALSE(ht1_ent_val == DOES_NOT_EXIST)) {
        map_val_t ht1_ent_val = cas_ret(&ht1_ent->val, DOES_NOT_EXIST, COPIED_VALUE);
        if (ht1_ent_val == DOES_NOT_EXIST) {
            TRACE("h1", "hti_copy_entry: empty entry %p killed", ht1_ent, 0);
            return true;
        }
        TRACE("h0", "hti_copy_entry: lost race to kill empty entry %p; the entry is not empty", ht1_ent, 0);
    }

    // Tag the value in the old entry to indicate a copy is in progress.
    ht1_ent_val = fetch_or(&ht1_ent->val, TAG_VALUE(0, TAG1));
    TRACE("h2", "hti_copy_entry: tagged the value %p in old entry %p", ht1_ent_val, ht1_ent);
    if (ht1_ent_val == COPIED_VALUE || ht1_ent_val == TAG_VALUE(TOMBSTONE, TAG1)) {
        TRACE("h1", "hti_copy_entry: entry %p already copied to table %p", ht1_ent, ht2);
        return false; // <value> was already copied by another thread.
    }

    // The old table's dead entries don't need to be copied to the new table
    if (ht1_ent_val == TOMBSTONE)
        return true;

    // Install the key in the new table.
    map_key_t ht1_ent_key = ht1_ent->key;
    map_key_t key = (ht1->ht->key_type == NULL) ? (map_key_t)ht1_ent_key : (map_key_t)GET_PTR(ht1_ent_key);

    // We use 0 to indicate that <key_hash> is uninitialized. Occasionally the key's hash will really be 0 and we
    // waste time recomputing it every time. It is rare enough that it won't hurt performance.
    if (key_hash == 0) {
        key_hash = hash32(ht1->ht, key, ctx);
    }

    int ht2_ent_is_empty;
    volatile entry_t *ht2_ent = hti_lookup(ht2, key, key_hash, &ht2_ent_is_empty, ctx);
    TRACE("h0", "hti_copy_entry: copy entry %p to entry %p", ht1_ent, ht2_ent);

    // It is possible that there isn't any room in the new table either.
    if (EXPECT_FALSE(ht2_ent == NULL)) {
        TRACE("h0", "hti_copy_entry: no room in table %p copy to next table %p", ht2, ht2->next);
        if (ht2->next == NULL) {
            hti_start_copy(ht2); // initiate nested copy, if not already started
        }
        return hti_copy_entry(ht1, ht1_ent, key_hash, ht2->next, ctx); // recursive tail-call
    }

    if (ht2_ent_is_empty) {
        map_key_t old_ht2_ent_key = cas_ret(&ht2_ent->key, DOES_NOT_EXIST, ht1_ent_key);
        if (old_ht2_ent_key != DOES_NOT_EXIST) {
            TRACE("h0", "hti_copy_entry: lost race to CAS key %p into new entry; found %p",
                    ht1_ent_key, old_ht2_ent_key);
            return hti_copy_entry(ht1, ht1_ent, key_hash, ht2, ctx); // recursive tail-call
        }
        add_fetch(&ht2->key_count, 1);
    }

    // Copy the value to the entry in the new table.
    ht1_ent_val = STRIP_TAG(ht1_ent_val, TAG1);
    map_val_t old_ht2_ent_val = cas_ret(&ht2_ent->val, DOES_NOT_EXIST, ht1_ent_val);

    // If there is a nested copy in progress, we might have installed the key into a dead entry.
    if (old_ht2_ent_val == COPIED_VALUE) {
        TRACE("h0", "hti_copy_entry: nested copy in progress; copy %p to next table %p", ht2_ent, ht2->next);
        return hti_copy_entry(ht1, ht1_ent, key_hash, ht2->next, ctx); // recursive tail-call
    }

    // Mark the old entry as dead.
    ht1_ent->val = COPIED_VALUE;

    // Update the count if we were the one that completed the copy.
    if (old_ht2_ent_val == DOES_NOT_EXIST) {
        TRACE("h0", "hti_copy_entry: key %p value %p copied to new entry", key, ht1_ent_val);
        //(void)add_fetch(&ht1->count, -1);
        //(void)add_fetch(&ht2->count, 1);
        return true;
    }

    TRACE("h0", "hti_copy_entry: lost race to install value %p in new entry; found value %p",
                ht1_ent_val, old_ht2_ent_val);
    return false; // another thread completed the copy
}

// Compare <expected> with the existing value associated with <key>. If the values match then
// replace the existing value with <new>. If <new> is DOES_NOT_EXIST, delete the value associated with
// the key by replacing it with a TOMBSTONE.
//
// Return the previous value associated with <key>, or DOES_NOT_EXIST if <key> is not in the table
// or associated with a TOMBSTONE. If a copy is in progress and <key> has been copied to the next
// table then return COPIED_VALUE.
//
// NOTE: the returned value matches <expected> iff the set succeeds
//
// Certain values of <expected> have special meaning. If <expected> is CAS_EXPECT_EXISTS then any
// real value matches (i.ent. not a TOMBSTONE or DOES_NOT_EXIST) as long as <key> is in the table. If
// <expected> is CAS_EXPECT_WHATEVER then skip the test entirely.
//
static map_val_t hti_cas (hti_t *hti, map_key_t key, uint32_t key_hash,
                          map_val_t expected, map_val_t new,
                          map_key_t *clone_key, void *ctx) {
    TRACE("h1", "hti_cas: hti %p key %p", hti, key);
    TRACE("h1", "hti_cas: value %p expect %p", new, expected);
    HREassert(hti);
    HREassert(!IS_TAGGED(new, TAG1));
    HREassert(key);

    int is_empty;
    volatile entry_t *ent = hti_lookup(hti, key, key_hash, &is_empty, ctx);

    // There is no room for <key>, grow the table and try again.
    if (ent == NULL) {
        if (hti->next == NULL) {
            hti_start_copy(hti);
        }
        return COPIED_VALUE;
    }

    // Install <key> in the table if it doesn't exist.
    if (is_empty) {
        TRACE("h0", "hti_cas: entry %p is empty", ent, 0);
        if (expected != CAS_EXPECT_WHATEVER && expected != CAS_EXPECT_DOES_NOT_EXIST)
            return DOES_NOT_EXIST;

        // No need to do anything, <key> is already deleted.
        if (new == DOES_NOT_EXIST)
            return DOES_NOT_EXIST;

        // Allocate <new_key>.
        map_key_t new_key = *clone_key =
                         (hti->ht->key_type->clone == NULL)
                          ? (map_key_t)key
                          : (map_key_t)hti->ht->key_type->clone((void *)key, ctx);
#ifndef NBD32
        if ((hti->ht->key_type->hash != NULL)) { //EXPECT_FALSE
            HREassert (GET_PTR(new_key) == (void *)new_key);
            // Combine <new_key> pointer with bits from its hash
            new_key = ((uint64_t)(key_hash >> 16) << 48) | new_key;
        }
#endif

        // CAS the key into the table.
        map_key_t old_ent_key = cas_ret(&ent->key, DOES_NOT_EXIST, new_key);

        // Retry if another thread stole the entry out from under us.
        if (old_ent_key != DOES_NOT_EXIST) {
            TRACE("h0", "hti_cas: lost race to install key %p in entry %p", new_key, ent);
            TRACE("h0", "hti_cas: found %p instead of NULL",
                        (hti->ht->key_type->clone == NULL) ? (void *)old_ent_key : GET_PTR(old_ent_key), 0);
            if (hti->ht->key_type->free != NULL) {
                hti->ht->key_type->free(GET_PTR(new_key));
            } else if (hti->ht->key_type->clone != NULL) {
                nbd_free(GET_PTR(new_key));
            }
            return hti_cas(hti, key, key_hash, expected, new, clone_key, ctx); // tail-call
        }
        TRACE("h2", "hti_cas: installed key %p in entry %p", new_key, ent);
        add_fetch(&hti->key_count, 1);
    } else {
        *clone_key = (map_key_t)GET_PTR(ent->key); // we might still win the race to install the
                               // value, in which case we also want to return a
                               // table key. Although not meaningful in presence of delete
    }

    TRACE("h0", "hti_cas: entry for key %p is %p",
                (hti->ht->key_type == NULL) ? (void *)ent->key : GET_PTR(ent->key), ent);

    // If the entry is in the middle of a copy, the copy must be completed first.
    map_val_t ent_val = ent->val;
    if (EXPECT_FALSE(IS_TAGGED(ent_val, TAG1))) {
        if (ent_val != COPIED_VALUE && ent_val != TAG_VALUE(TOMBSTONE, TAG1)) {
            hti_t *n = ((hti_t)atomic_read(hti)).next;
            int did_copy = hti_copy_entry(hti, ent, key_hash, n, ctx);
            if (did_copy) {
                (void)add_fetch(&hti->num_entries_copied, 1);
            }
            TRACE("h0", "hti_cas: value in the middle of a copy, copy completed by %s",
                        (did_copy ? "self" : "other"), 0);
        }
        TRACE("h0", "hti_cas: value copied to next table, retry on next table", 0, 0);
        return COPIED_VALUE;
    }

    // Fail if the old value is not consistent with the caller's expectation.
    int old_existed = (ent_val != TOMBSTONE && ent_val != DOES_NOT_EXIST);
    if (EXPECT_FALSE(expected != CAS_EXPECT_WHATEVER && expected != ent_val)) {
        if (EXPECT_FALSE(expected != (old_existed ? CAS_EXPECT_EXISTS : CAS_EXPECT_DOES_NOT_EXIST))) {
            TRACE("h1", "hti_cas: value %p expected by caller not found; found value %p",
                        expected, ent_val);
            return ent_val;
        }
    }

    // No need to update if value is unchanged.
    if ((new == DOES_NOT_EXIST && !old_existed) || ent_val == new) {
        TRACE("h1", "hti_cas: old value and new value were the same", 0, 0);
        return ent_val;
    }

    // CAS the value into the entry. Retry if it fails.
    map_val_t v = cas_ret(&ent->val, ent_val, new == DOES_NOT_EXIST ? TOMBSTONE : new);
    if (EXPECT_FALSE(v != ent_val)) {
        TRACE("h0", "hti_cas: value CAS failed; expected %p found %p", ent_val, v);
        return hti_cas(hti, key, key_hash, expected, new, clone_key, ctx); // recursive tail-call
    }

    /* The set succeeded. Adjust the value count.
    if (old_existed && new == DOES_NOT_EXIST) {
        (void)add_fetch(&hti->count, -1);
    } else if (!old_existed && new != DOES_NOT_EXIST) {
        (void)add_fetch(&hti->count, 1);
    }*/

    // Return the previous value.
    TRACE("h0", "hti_cas: CAS succeeded; old value %p new value %p", ent_val, new);
    return ent_val;
}

//
static map_val_t hti_get (hti_t *hti, map_key_t key, uint32_t key_hash, void *ctx) {
    int is_empty;
    volatile entry_t *ent = hti_lookup(hti, key, key_hash, &is_empty, ctx);

    // When hti_lookup() returns NULL it means we hit the reprobe limit while
    // searching the table. In that case, if a copy is in progress the key
    // might exist in the copy.
    if (EXPECT_FALSE(ent == NULL)) {
        hti_t *n = ((hti_t)atomic_read(hti)).next;
        if (n != NULL)
            return hti_get(hti->next, key, key_hash, ctx); // recursive tail-call
        return DOES_NOT_EXIST;
    }

    if (is_empty)
        return DOES_NOT_EXIST;

    // If the entry is being copied, finish the copy and retry on the next table.
    map_val_t ent_val = ent->val;
    if (EXPECT_FALSE(IS_TAGGED(ent_val, TAG1))) {
        if (EXPECT_FALSE(ent_val != COPIED_VALUE && ent_val != TAG_VALUE(TOMBSTONE, TAG1))) {
            hti_t *n = ((hti_t)atomic_read(hti)).next;
            int did_copy = hti_copy_entry(hti, ent, key_hash, n, ctx);
            if (did_copy) {
                (void)add_fetch(&hti->num_entries_copied, 1);
            }
        }
        hti_t *n = ((hti_t)atomic_read(hti)).next;
        return hti_get(n, key, key_hash, ctx); // tail-call
    }

    return (ent_val == TOMBSTONE) ? DOES_NOT_EXIST : ent_val;
}

//
map_val_t ht_get (hashtable_t *ht, map_key_t key, void *ctx) {
    uint32_t key_hash = hash32(ht, key, ctx);
    return hti_get(ht->hti, key, key_hash, ctx);
}

// returns true if copy is done
static int hti_help_copy (hti_t *hti, void *ctx) {
    volatile entry_t *ent;
    size_t limit;
    size_t total_copied = hti->num_entries_copied;
    size_t num_copied = 0;
    size_t x = hti->copy_scan;

    TRACE("h1", "ht_cas: help copy. scan is %llu, size is %llu", x, 1<<hti->scale);
    if (total_copied != (1ULL << hti->scale)) {
        // Panic if we've been around the array twice and still haven't finished the copy.
        int panic = (x >= (1ULL << (hti->scale + 1)));
        if (!panic) {
            limit = ENTRIES_PER_COPY_CHUNK;

            // Reserve some entries for this thread to copy. There is a race condition here because the
            // fetch and add isn't atomic, but that is ok.
            hti->copy_scan = x + ENTRIES_PER_COPY_CHUNK;

            // <copy_scan> might be larger than the size of the table, if some thread stalls while
            // copying. In that case we just wrap around to the begining and make another pass through
            // the table.
            ent = hti->table + (x & MASK(hti->scale));
        } else {
            TRACE("h1", "ht_cas: help copy panic", 0, 0);
            // scan the whole table
            ent = hti->table;
            limit = (1ULL << hti->scale);
        }

        // Copy the entries
        for (size_t i = 0; i < limit; ++i) {
            num_copied += hti_copy_entry(hti, ent++, 0, hti->next, ctx);
            HREassert(ent <= hti->table + (1ULL << hti->scale));
        }
        if (num_copied != 0) {
            total_copied = add_fetch(&hti->num_entries_copied, num_copied);
        }
    }

    return (total_copied == (1ULL << hti->scale));
}

static void hti_defer_free (hti_t *hti) {
    HREassert(hti->ref_count == 0);

    for (uint32_t i = 0; i < (1ULL << hti->scale); ++i) {
        map_key_t key = hti->table[i].key;
        map_val_t val = hti->table[i].val;
        if (val == COPIED_VALUE)
            continue;
        HREassert(!IS_TAGGED(val, TAG1) || val == TAG_VALUE(TOMBSTONE, TAG1)); // copy not in progress
        if (key != DOES_NOT_EXIST) {
            if (hti->ht->key_type->free != NULL) {
                hti->ht->key_type->free(GET_PTR(key));
            } else if (hti->ht->key_type->clone != NULL) {
                nbd_free(GET_PTR(key));
            }
        }
    }
    //nbd_free((void *)hti->table); //TODO: reclaim memory

    //nbd_free(hti);
}

static void hti_release (hti_t *hti) {
    HREassert(hti->ref_count > 0);
    int ref_count = add_fetch(&hti->ref_count, -1);
    if (ref_count == 0) {
        hti_defer_free(hti);
    }
}

//
map_val_t ht_cas (hashtable_t *ht, map_key_t key, map_val_t expected_val,
                  map_val_t new_val, map_key_t *clone_key, void *ctx) {

    TRACE("h2", "ht_cas: key %p ht %p", key, ht);
    TRACE("h2", "ht_cas: expected val %p new val %p", expected_val, new_val);
    HREassert(key != DOES_NOT_EXIST);
    HREassert(!IS_TAGGED(new_val, TAG1) && new_val != DOES_NOT_EXIST && new_val != TOMBSTONE);

    hti_t *hti = ht->hti;

    // Help with an ongoing copy.
    if (EXPECT_FALSE(hti->next != NULL)) {
        int done = hti_help_copy(hti, ctx);

        // Unlink fully copied tables.
        if (done) {
            HREassert(hti->next);
            if (cas_ret(&ht->hti, hti, hti->next) == hti) {
                hti_release(hti);
            }
        }
    }

    map_val_t old_val;
    uint32_t key_hash = hash32(ht, key, ctx);
    while ((old_val = hti_cas(hti, key, key_hash, expected_val, new_val, clone_key, ctx)) == COPIED_VALUE) {
        HREassert(hti->next);
        hti = hti->next;
    }

    return old_val == TOMBSTONE ? DOES_NOT_EXIST : old_val;
}

// Remove the value in <ht> associated with <key>. Returns the value removed, or DOES_NOT_EXIST if there was
// no value for that key.
map_val_t ht_remove (hashtable_t *ht, map_key_t key, map_key_t *clone_key, void *ctx) {
    hti_t *hti = ht->hti;
    map_val_t val;
    uint32_t key_hash = hash32(ht, key, ctx);
    do {
        val = hti_cas(hti, key, key_hash, CAS_EXPECT_WHATEVER, DOES_NOT_EXIST, clone_key, ctx);
        if (val != COPIED_VALUE)
            return val == TOMBSTONE ? DOES_NOT_EXIST : val;
        HREassert(hti->next);
        hti = hti->next;
        HREassert(hti);
    } while (1);
}

// Returns the number of key-values pairs in <ht>
size_t ht_count (hashtable_t *ht) {
    hti_t *hti = ht->hti;
    size_t count = 0;
    while (hti) {
        //count += hti->count;
        hti = hti->next;
    }
    return count;
}

// Allocate and initialize a new hash table.
hashtable_t *ht_alloc (const datatype_t *key_type, size_t size) {
    hashtable_t *ht = nbd_malloc(sizeof(hashtable_t));
    ht->key_type = key_type;
    ht->hti = ht->first = (hti_t *)hti_alloc(ht, size);
    ht->hti_copies = 0;
    ht->density = 0.0;
    return ht;
}

// Free <ht> and its internal structures.
void ht_free (hashtable_t *ht) {
    hti_t *hti = ht->hti;
    do {
        hti_t *next = hti->next;
        HREassert(hti->ref_count == 1);
        hti_release(hti);
        hti = next;
    } while (hti);
    nbd_free(ht);
}

void ht_print (hashtable_t *ht, int verbose) {
    printf("probe:%-2d density:%.1f%% count:%-8zu ", ht->probe, ht->density, ht_count(ht));
    hti_t *hti = ht->hti;
    while (hti) {
        if (verbose) {
            for (size_t i = 0; i < (1ULL << hti->scale); ++i) {
                volatile entry_t *ent = hti->table + i;
                printf("[0x%zu] 0x%zu:0x%zu\n", i, (size_t)ent->key, (size_t)ent->val);
                if (i > 30) {
                    printf("...\n");
                    break;
                }
            }
        }
        //int scale = hti->scale;
        //printf("hti count:%lld scale:%d key density:%.1f%% value density:%.1f%% probe:%d\n",
         //       (uint64_t)hti->count, scale, (double)hti->key_count / (1ULL << scale) * 100,
         //       (double)hti->count / (1ULL << scale) * 100, hti->probe);
        hti = hti->next;
    }
}

ht_iter_t *ht_iter_begin (hashtable_t *ht, map_key_t key, void *ctx) {
    hti_t *hti;
    int ref_count;
    do {
        hti = ht->hti;
        while (hti->next != NULL) {
            do { } while (hti_help_copy(hti, ctx) != true);
            hti = hti->next;
        }
        do {
            ref_count = hti->ref_count;
            if(ref_count == 0)
                break;
        } while (ref_count != cas_ret(&hti->ref_count, ref_count, ref_count + 1));
    } while (ref_count == 0);

    ht_iter_t *iter = nbd_malloc(sizeof(ht_iter_t));
    iter->hti = hti;
    iter->idx = -1;

    return iter;
    (void) key;
}

size_t ht_size (hashtable_t *ht) {
    hti_t *hti = ht->first;
    size_t sizes = 0; // sizes of all tables in byte
    while (hti != NULL) {
        sizes += 1ULL << hti->scale;
        hti = hti->next;
    }
    return sizes * sizeof(map_key_t) * 2;
}

map_val_t ht_iter_next (ht_iter_t *iter, map_key_t *key_ptr, void *ctx) {
    volatile entry_t *ent;
    map_key_t key;
    map_val_t val;
    int64_t table_size = (1ULL << iter->hti->scale);
    do {
        iter->idx++;
        if (iter->idx == table_size) {
            return DOES_NOT_EXIST;
        }
        ent = &iter->hti->table[iter->idx];
        key = (iter->hti->ht->key_type->clone == NULL) ? (map_key_t)ent->key : (map_key_t)GET_PTR(ent->key);
        val = ent->val;

    } while (key == DOES_NOT_EXIST || val == DOES_NOT_EXIST || val == TOMBSTONE);

    if (val == COPIED_VALUE) {
        uint32_t hash = hash32(iter->hti->ht, key, ctx);
        val = hti_get(iter->hti->next, (map_key_t)ent->key, hash, ctx);

        // Go to the next entry if key is already deleted.
        if (val == DOES_NOT_EXIST)
            return ht_iter_next(iter, key_ptr, ctx); // recursive tail-call
    }

    if (key_ptr) {
        *key_ptr = key;
    }
    return val;
}

void ht_iter_free (ht_iter_t *iter) {
    hti_release(iter->hti);
    nbd_free(iter);
}
