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

/**
 * bitvector_create
 *  Create a new bitvector
 *   1) unused bitvector_t struct
 *   2) number of bits in the vector
 *
 *  return values:
 *   0: n-bit bitvector has been allocated
 *  -1: error
 */
extern int          bitvector_create (bitvector_t *, const int);

/**
 * bitvector_free
 *  Free memory used by a bitvector
 *   1) the bitvector to free
 *
 *  result:
 *     frees the memory used by the bitvector
 */
extern void         bitvector_free (bitvector_t *);

/**
 * bitvector_copy
 *  Copy a bitvector to another bitvector
 *   1) unused bitvector_t struct
 *   2) source bitvector
 *
 *  return value:
 *   0: new bitvector is created in the target, all bits are copied
 *  -1: error
 */
extern int          bitvector_copy (bitvector_t *, const bitvector_t *);

/**
 * bitvector_size
 *  Returns the size of the bitvector
 *   1) the bitvector to get the size from
 *
 *  return value:
 *   the size of the bitvector
 */
extern size_t       bitvector_size (const bitvector_t *);

/**
 * bitvector_set
 *  Set a bit in the bitvector to one
 *   1) the bitvector to set the bit in
 *   2) the index of the bit that must be set to one
 *
 *  result:
 *   the bit in the bitvector is set to one
 */
extern void         bitvector_set (bitvector_t *, const int);

/**
 * bitvector_unset
 *  Set a bit in the bitvector to clear
 *   1) the bitvector to clear the bit in
 *   2) the index of the bit that must be cleared
 *
 *  result:
 *   the bit in the bitvector is cleared
 */
extern void         bitvector_unset (bitvector_t *, const int);

/**
 * bitvector_is_set
 *  Test a bit in the bitvector for value one
 *   1) the bitvector to test the bit in
 *   2) the index of the bit that must be tested
 *
 *  result:
 *   true:  the bit is one
 *   false: the bit is not one
 */
extern int          bitvector_is_set (const bitvector_t *, const int);

/**
 * bitvector_union
 *  Union two bitvectors
 *   1) the target bitvector
 *      the bitvector should be created with bitvector_create and
 *      should be of the same size as the source bitvector
 *   2) the source bitvector
 *
 *  result:
 *   the target bitvector will have all bits set that are set either of the bitvectors
 */
extern void         bitvector_union(bitvector_t *, const bitvector_t *);

/**
 * bitvector_intersect
 *  Intersect two bitvectors
 *   1) the target bitvector
 *      the bitvector should be created with bitvector_create and
 *      should be of the same size as the source bitvector
 *   2) the source bitvector
 *
 *  result:
 *   the target bitvector have only those bits set that are set in both vectors
 */
extern void         bitvector_intersect(bitvector_t *, const bitvector_t *);

/**
 * bitvector_is_empty
 *  Test wether all bits in the bitvector are not set
 *   1) the bitvector to test
 *
 *  result:
 *   true:  all bits in the bitvector are not set
 *   false: at least one bit in the bitvector is set
 */
extern int          bitvector_is_empty(const bitvector_t *);

/**
 * bitvector_is_disjoint
 *  Test wether two bitvectors are disjoint or not
 *  Disjoint means that the intersection of the two bitvectors is empty
 *   1,2) the bitvectors (of the same size) to test
 *
 *  result:
 *   true:  the bitvectors are disjoint
 *   false: the bitvectors are not disjoint or not of the same size
 */
extern int          bitvector_is_disjoint(const bitvector_t *, const bitvector_t *);

/**
 * bitvector_invert
 *  Invert all bits in the bitvector
 *   1) the bitvectors to invert
 *
 *  result:
 *   All bits that were not set are now set and visa versa
 */
extern void         bitvector_invert(bitvector_t *);

#endif                          // BITVECTOR_H
