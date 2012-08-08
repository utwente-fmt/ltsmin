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

extern uint32_t        BVLLinc_sat_bits (const bitvector_ll_t *dbs,
                                         const bv_ref_t ref);

extern uint32_t        BVLLdec_sat_bits (const bitvector_ll_t *dbs,
                                         const bv_ref_t ref);

extern bitvector_ll_t *BVLLcreate (size_t bits, size_t size);

extern void            BVLLfree (bitvector_ll_t *bv);
