/*
 * bitvector-ll.c
 *
 *  Created on: May 29, 2012
 *      Author: laarman
 */

#include "config.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <atomics.h>
#include <bitvector-ll.h>
#include <runtime.h>

struct bitvector_ll_s {
    size_t              sat_bits;
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
        //new_val = (bits >> rest) & (dbs->sat_bits - 1);
        //assert (new_val < (1UL << dbs->root.sat_bits) && "Too many sat bit incs");
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
        //new_val = (bits >> rest) & (dbs->sat_bits - 1);
        //assert (new_val > 0 && "Too many sat bit decs");
        new_val = bits - (1 << rest);
    } while ( !cas(dbs->bits+pos, bits, new_val) );
    return (new_val >> rest) & (dbs->sat_bits - 1);
}

bitvector_ll_t *
BVLLcreate (size_t bits, size_t size)
{
    bitvector_ll_t     *dbs = RTalign (CACHE_LINE_SIZE, sizeof(bitvector_ll_t));
    dbs->log_bits = ceil (log(bits) / log(2));
    dbs->sat_bits = 1UL << dbs->log_bits;
    assert (dbs->sat_bits < 16 && "too many sat bits for tree DB");
    dbs->bits = RTalignZero (CACHE_LINE_SIZE, dbs->sat_bits << (size - 3));
    return dbs;
}

void
BVLLfree (bitvector_ll_t *bv)
{
    RTfree (bv->bits);
    RTfree (bv);
}
