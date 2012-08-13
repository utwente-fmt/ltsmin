/*
 * bitvector-ll.c
 *
 *  Created on: May 29, 2012
 *      Author: laarman
 */

#include <hre/config.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/bitvector-ll.h>

struct bitvector_ll_s {
    size_t              sat_bits;
    size_t              sat_mask;
    size_t              log_bits;
    uint16_t           *bits;
};

uint32_t
BVLLget_sat_bits (const bitvector_ll_t *dbs, const bv_ref_t ref)
{
    size_t pos = (ref << dbs->log_bits) >> 4;
    size_t rest = (ref << dbs->log_bits) & 15;
    uint16_t b = atomic_read (dbs->bits + pos);
    return (b >> rest) & (dbs->sat_bits - 1);
}

int
BVLLget_sat_bit (const bitvector_ll_t *dbs, const bv_ref_t ref, int index)
{
    size_t pos = (ref << dbs->log_bits) >> 4;
    size_t rest = (ref << dbs->log_bits) & 15;
    uint16_t b = atomic_read (dbs->bits + pos);
    return ((b >> rest) & (1UL << index)) != 0;
}

void
BVLLunset_sat_bit (const bitvector_ll_t *dbs, const bv_ref_t ref, int index)
{
    size_t pos = (ref << dbs->log_bits) >> 4;
    size_t rest = (ref << dbs->log_bits) & 15;
    uint16_t b = 1UL << index << rest;
    fetch_and (dbs->bits+pos, ~b);
    return;
}

int
BVLLtry_set_sat_bit (const bitvector_ll_t *dbs, const bv_ref_t ref, int index)
{
    size_t pos = (ref << dbs->log_bits) >> 4;
    size_t rest = (ref << dbs->log_bits) & 15;
    uint16_t b = 1UL << index << rest;
    uint16_t p = fetch_or (dbs->bits+pos, b);
    return (p & b) == 0;
}

int
BVLLtry_unset_sat_bit (const bitvector_ll_t *dbs, const bv_ref_t ref, int index)
{
    size_t pos = (ref << dbs->log_bits) >> 4;
    size_t rest = (ref << dbs->log_bits) & 15;
    uint16_t b = 1UL << index << rest;
    uint16_t p = fetch_and (dbs->bits+pos, ~b);
    return (p & b) != 0;
}

uint32_t
BVLLinc_sat_bits (const bitvector_ll_t *dbs, const bv_ref_t ref)
{
    uint64_t            new_val, bits;
    size_t pos = (ref << dbs->log_bits) >> 4;
    size_t rest = (ref << dbs->log_bits) & 15;
    do {
        bits = atomic_read (dbs->bits + pos);
        HRE_ASSERT (((bits >> rest) & dbs->sat_mask) < dbs->sat_bits - 1,
                    "Too many sat bit incs");
        new_val = bits + (1 << rest);
    } while ( !cas(dbs->bits+pos, bits, new_val) );
    return  (new_val >> rest) & (dbs->sat_bits - 1);
}

uint32_t
BVLLdec_sat_bits (const bitvector_ll_t *dbs, const bv_ref_t ref)
{
    uint64_t            new_val, bits;
    size_t pos = (ref << dbs->log_bits) >> 4;
    size_t rest = (ref << dbs->log_bits) & 15;
    do {
        bits = atomic_read (dbs->bits + pos);
        HRE_ASSERT (((bits >> rest) & dbs->sat_mask) > 0,
                    "Too many sat bit decs");
        new_val = bits - (1 << rest);
    } while ( !cas(dbs->bits+pos, bits, new_val) );
    return (new_val >> rest) & dbs->sat_mask;
}

bitvector_ll_t *
BVLLcreate (size_t bits, size_t size)
{
    bitvector_ll_t     *dbs = RTalign (CACHE_LINE_SIZE, sizeof(bitvector_ll_t));
    dbs->log_bits = ceil (log(bits) / log(2));
    dbs->sat_bits = 1UL << dbs->log_bits;
    dbs->sat_mask = dbs->sat_bits - 1;
    HREassert (dbs->sat_bits < 16, "too many sat bits for bitvector-ll");
    dbs->bits = RTalignZero (CACHE_LINE_SIZE, dbs->sat_bits << (size - 3));
    return dbs;
}

void
BVLLfree (bitvector_ll_t *bv)
{
    RTfree (bv->bits);
    RTfree (bv);
}
