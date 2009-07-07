#include "bitvector.h"
#include <limits.h>

static inline       size_t
utrunc (size_t x, size_t m)
{
    return (x + (m - 1)) / m;
}

static const size_t WORD_BITS = sizeof (size_t) * 8;

static inline size_t
bv_seg (size_t i) { return i / WORD_BITS; }

static inline size_t
bv_ofs (size_t i) { return i % WORD_BITS; }

int
bitvector_create (bitvector_t *bv, const int n_bits)
{
    size_t              n_words = utrunc (n_bits, WORD_BITS);
    bv->data = calloc (n_words, sizeof (size_t));
    if (bv->data == NULL) {
        bv->n_bits = 0;
        return -1;
    } else {
        bv->n_bits = n_bits;
        return 0;
    }
}

void
bitvector_free (bitvector_t *bv)
{
    // free memory
    if (bv->data != NULL)
        free (bv->data);
    bv->n_bits = 0;
}

int
bitvector_copy (const bitvector_t *bv_src, bitvector_t *bv_tgt)
{
    // check validity src
    if (bv_src->n_bits == 0 || bv_src->data == NULL)
        return -1;

    // alloc memory for target
    if (bitvector_create (bv_tgt, bv_src->n_bits) != 0) {
        return -1;
    } else {
        // copy bitvector
        size_t              size =
            utrunc (bv_src->n_bits, WORD_BITS) * sizeof (size_t);
        memcpy (bv_tgt->data, bv_src->data, size);
        return 0;
    }
}

size_t
bitvector_size (const bitvector_t *bv)
{
    return bv->n_bits;
}

void
bitvector_set (bitvector_t *bv, const int idx)
{
    // set bit
    size_t              mask = 1UL << bv_ofs (idx);
    bv->data[bv_seg (idx)] |= mask;
}

void
bitvector_unset (bitvector_t *bv, const int idx)
{
    // set bit
    size_t              mask = ~(1UL << bv_ofs (idx));
    bv->data[bv_seg (idx)] &= mask;
}

int
bitvector_is_set (const bitvector_t *bv, const int idx)
{
    size_t              mask = 1UL << bv_ofs (idx);
    return (bv->data[bv_seg (idx)] & mask) != 0;
}
