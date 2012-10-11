#include <hre/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <util-lib/fast_hash.h>

/**
 * fast hash table for already-hashed elements
 */

#define FSET_FULL -1

typedef struct fset_s fset_t;


extern size_t   fset_count (fset_t *dbs);

extern int      fset_find (fset_t *dbs, hash32_t *h, void *data,
                           bool insert_absent);

extern int      fset_delete (fset_t *dbs, hash32_t *mem, void *data);

extern void     fset_clear (fset_t *dbs);

extern fset_t  *fset_create (size_t data_length, size_t init_size,
                                                 size_t max_size);

extern void     fset_free (fset_t *dbs);

extern void     fset_print_statistics (fset_t *dbs, char *s);

extern size_t   fset_mem (fset_t *dbs);
