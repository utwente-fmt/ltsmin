#ifndef NB_HASHTABLE_H
#define NB_HASHTABLE_H

#include <stdint.h>

#ifndef __x86_64__
#define NBD32
#endif

#ifdef NBD32
typedef uint32_t map_key_t;
typedef uint32_t map_val_t;
#else
typedef uint64_t map_key_t;
typedef uint64_t map_val_t;
#endif

typedef int      (*cmp_fun_t)   (void *, void *, void *);
typedef void *   (*clone_fun_t) (void *, void *);
typedef uint32_t (*hash_fun_t)  (void *, void *);
typedef void     (*free_fun_t)  (void *);

typedef struct datatype {
    cmp_fun_t           cmp;
    hash_fun_t          hash;
    clone_fun_t         clone;
    free_fun_t          free;
} datatype_t;

///////////////////////////////////////////////////////////////////////////////

#define DOES_NOT_EXIST 0
static const map_key_t CAS_EXPECT_DOES_NOT_EXIST =             0;
static const map_key_t CAS_EXPECT_EXISTS         = (map_key_t)-1;
static const map_key_t CAS_EXPECT_WHATEVER       = (map_key_t)-2;

///////////////////////////////////////////////////////////////////////////////

typedef struct ht hashtable_t;
typedef struct ht_iter ht_iter_t;

hashtable_t * ht_alloc      (const datatype_t *key_type, size_t size);
map_val_t     ht_cas        (hashtable_t *ht, map_key_t key,
                             map_val_t expected_val, map_val_t val,
                             map_key_t *clone_key, void *ctx);
map_val_t     ht_get        (hashtable_t *ht, map_key_t key, void *ctx);
map_val_t     ht_remove     (hashtable_t *ht, map_key_t key, map_key_t *clone_key, void *ctx);
size_t        ht_count      (hashtable_t *ht);
void          ht_print      (hashtable_t *ht, int verbose);
void          ht_free       (hashtable_t *ht);
ht_iter_t *   ht_iter_begin (hashtable_t *ht, map_key_t key, void *ctx);
map_val_t     ht_iter_next  (ht_iter_t *iter, map_key_t *key_ptr, void *ctx);
void          ht_iter_free  (ht_iter_t *iter);
size_t        ht_size       (hashtable_t *ht);

static inline map_val_t
ht_cas_empty (hashtable_t *ht, map_key_t key, map_val_t val, map_key_t *clone,
              void *ctx) {
    return ht_cas (ht, key, CAS_EXPECT_DOES_NOT_EXIST, val, clone, ctx);
}

static inline map_val_t
ht_cas2 (hashtable_t *ht, map_key_t key, map_val_t val, map_key_t *clone) {
    return ht_cas (ht, key, CAS_EXPECT_DOES_NOT_EXIST, val, clone, NULL);
}


#endif // NB_HASHTABLE_H
