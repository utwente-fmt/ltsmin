/*
 * bitvector-ll.h
 *
 *  Created on: May 29, 2012
 *      Author: laarman
 */

#include <stdint.h>

typedef size_t bv_ref_t;

typedef struct bitvector_ll_s bitvector_ll_t;

extern uint32_t        BVLLget_sat_bits (const bitvector_ll_t *dbs,
                                         const bv_ref_t ref);

extern int             BVLLget_sat_bit (const bitvector_ll_t *dbs,
                                        const bv_ref_t ref, int index);

extern void            BVLLunset_sat_bit (const bitvector_ll_t *dbs,
                                          const bv_ref_t ref, int index);

extern int             BVLLtry_set_sat_bit (const bitvector_ll_t *dbs,
                                            const bv_ref_t ref, int index);

extern int             BVLLtry_unset_sat_bit (const bitvector_ll_t *dbs,
                                              const bv_ref_t ref, int index);

extern uint32_t        BVLLinc_sat_value (const bitvector_ll_t *dbs,
                                         const bv_ref_t ref);

extern uint32_t        BVLLdec_sat_value (const bitvector_ll_t *dbs,
                                         const bv_ref_t ref);

extern size_t          BVLLget_size (const bitvector_ll_t *dbs);
extern size_t          BVLLget_bits_in_bucket (const bitvector_ll_t *dbs);

/**
 * Get avaiable values in bucked. Rounded to power of two.
 */
extern size_t          BVLLget_values_in_bucket (const bitvector_ll_t *dbs);

/**
 * Create a bucketed bitvector with room for 'values' values in each bucket and
 * with 2^'size' buckets.
 *
 * Buckets can be dually used as bitsets or as values. See above functions.
 */
extern bitvector_ll_t *BVLLcreate (size_t values, size_t logsize);

extern void            BVLLfree (bitvector_ll_t *bv);
