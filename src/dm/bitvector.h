#ifndef BITVECTOR_H
#define BITVECTOR_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct bitvector {
    size_t              n_bits;
    size_t             *data;
} bitvector_t;

extern int          bitvector_create (bitvector_t *, const int);
extern void         bitvector_free (bitvector_t *);
extern int          bitvector_copy (bitvector_t *, bitvector_t *);

extern size_t       bitvector_size (bitvector_t *);

extern int          bitvector_set (bitvector_t *, const int);
extern int          bitvector_unset (bitvector_t *, const int);
extern int          bitvector_is_set (bitvector_t *, const int);
extern int          bitvector_eq (bitvector_t *, bitvector_t *);

#endif                          // BITVECTOR_H
