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
    size_t              values;
    size_t              value_mask;
    size_t              num_bits;
    size_t              size;
    uint16_t           *bits;
};

uint32_t
BVLLget_sat_bits (const bitvector_ll_t *dbs, const bv_ref_t ref)
{
    size_t pos = (ref << dbs->num_bits) >> 4;
    size_t rest = (ref << dbs->num_bits) & 15;
    uint16_t b = atomic_read (dbs->bits + pos);
    return (b >> rest) & dbs->value_mask;
}

int
BVLLget_sat_bit (const bitvector_ll_t *dbs, const bv_ref_t ref, int index)
{
    size_t pos = (ref << dbs->num_bits) >> 4;
    size_t rest = (ref << dbs->num_bits) & 15;
    uint16_t b = atomic_read (dbs->bits + pos);
    return ((b >> rest) & (1UL << index)) != 0;
}

void
BVLLunset_sat_bit (const bitvector_ll_t *dbs, const bv_ref_t ref, int index)
{
    size_t pos = (ref << dbs->num_bits) >> 4;
    size_t rest = (ref << dbs->num_bits) & 15;
    uint16_t b = 1UL << index << rest;
    fetch_and (dbs->bits+pos, ~b);
    return;
}

int
BVLLtry_set_sat_bit (const bitvector_ll_t *dbs, const bv_ref_t ref, int index)
{
    size_t pos = (ref << dbs->num_bits) >> 4;
    size_t rest = (ref << dbs->num_bits) & 15;
    uint16_t b = 1UL << index << rest;
    uint16_t p = fetch_or (dbs->bits+pos, b);
    return (p & b) == 0;
}

int
BVLLtry_unset_sat_bit (const bitvector_ll_t *dbs, const bv_ref_t ref, int index)
{
    size_t pos = (ref << dbs->num_bits) >> 4;
    size_t rest = (ref << dbs->num_bits) & 15;
    uint16_t b = 1UL << index << rest;
    uint16_t p = fetch_and (dbs->bits+pos, ~b);
    return (p & b) != 0;
}

uint32_t
BVLLinc_sat_value (const bitvector_ll_t *dbs, const bv_ref_t ref)
{
    uint64_t            new_val, bits;
    size_t pos = (ref << dbs->num_bits) >> 4;
    size_t rest = (ref << dbs->num_bits) & 15;
    do {
        bits = atomic_read (dbs->bits + pos);
        HRE_ASSERT (((bits >> rest) & dbs->value_mask) < dbs->num_bits - 1,
                    "Too many sat bit incs");
        new_val = bits + (1 << rest);
    } while ( !cas(dbs->bits+pos, bits, new_val) );
    return  (new_val >> rest) & dbs->value_mask;
}

uint32_t
BVLLdec_sat_value (const bitvector_ll_t *dbs, const bv_ref_t ref)
{
    uint64_t            new_val, bits;
    size_t pos = (ref << dbs->num_bits) >> 4;
    size_t rest = (ref << dbs->num_bits) & 15;
    do {
        bits = atomic_read (dbs->bits + pos);
        HRE_ASSERT (((bits >> rest) & dbs->value_mask) > 0,
                    "Too many sat bit decs");
        new_val = bits - (1 << rest);
    } while ( !cas(dbs->bits+pos, bits, new_val) );
    return (new_val >> rest) & dbs->value_mask;
}

size_t
BVLLget_size (const bitvector_ll_t *dbs)
{
    return 1ULL << dbs->size;
}

size_t
BVLLget_bits_in_bucket (const bitvector_ll_t *dbs)
{
    return dbs->num_bits;
}

size_t
BVLLget_values_in_bucket (const bitvector_ll_t *dbs)
{
    return dbs->values;
}

bitvector_ll_t *
BVLLcreate (size_t values, size_t logsize)
{
    bitvector_ll_t     *dbs = RTalign (CACHE_LINE_SIZE, sizeof(bitvector_ll_t));
    dbs->num_bits = ceil (log(values) / log(2));
    dbs->values = 1UL << dbs->num_bits;
    dbs->value_mask = dbs->values - 1;
    dbs->size = logsize;
    HREassert (dbs->num_bits < 16, "too many sat bits for bitvector-ll");
    HREassert (logsize >= 3, "too small bitvector-ll");
    dbs->bits = RTalignZero (CACHE_LINE_SIZE, dbs->num_bits << (logsize - 3));
    return dbs;
}

void
BVLLfree (bitvector_ll_t *bv)
{
    RTalignedFree (bv->bits);
    RTalignedFree (bv);
}
