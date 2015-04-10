
#ifndef FSET_H
#define FSET_H

#include <hre/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <util-lib/fast_hash.h>

/**
 * Allocation-less resizing hash table for (already-hashed) fixed-sized elements
 */

#define FSET_FULL -1
static const size_t FSET_MIN_SIZE = CACHE_LINE - 2; //log(sizeof (mem_hash_t)

typedef struct fset_s fset_t;


extern size_t   fset_count  (fset_t *dbs);

extern size_t   fset_max_load(fset_t *dbs);

extern int      fset_find   (fset_t *dbs, hash32_t *h, void *key, void **data,
                             bool insert_absent);

extern bool     fset_delete (fset_t *dbs, hash32_t *mem, void *key);

extern bool     fset_delete_get_data (fset_t *dbs, hash32_t *mem, void *key,
                                      void **data);

extern void     fset_clear  (fset_t *dbs);

extern fset_t  *fset_create (size_t key_length, size_t data_length,
                             size_t init_size, size_t max_size);

extern void     fset_free   (fset_t *dbs);

extern void     fset_print_statistics (fset_t *dbs, char *s);

extern size_t   fset_mem    (fset_t *dbs);

#endif // FSET_H
