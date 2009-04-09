#include "bitvector.h"

int
bitvector_create (bitvector_t *bv, const int n_bits)
{
    size_t              size = (int)4 * ((n_bits / 32) + 1);
    bv->data = malloc (size);
    if (bv->data == NULL) {
        bv->n_bits = 0;
        return 0;
    } else {
        memset (bv->data, 0, size);
        bv->n_bits = n_bits;
        return 1;
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
bitvector_copy (bitvector_t *bv_src, bitvector_t *bv_tgt)
{
    // check validity src
    if (bv_src->n_bits == 0 || bv_src->data == NULL)
        return 0;

    // alloc memory for target
    int                 alloc_ok =
        bitvector_create (bv_tgt, bv_src->n_bits);
    if (alloc_ok) {
        // copy bitvector
        size_t              size = (int)4 * ((bv_src->n_bits / 32) + 1);
        memcpy (bv_tgt->data, bv_src->data, size);
        return 1;
    } else {
        return 0;
    }
}

int
bitvector_size (bitvector_t *bv)
{
    return bv->n_bits;
}

int
bitvector_set (bitvector_t *bv, const int idx)
{
    // set bit
    unsigned long       mask = 0x1 << (idx % 32);
    bv->data[idx / 32] |= mask;
    return 1;
}

int
bitvector_unset (bitvector_t *bv, const int idx)
{
    // set bit
    unsigned long       mask = ~(0x1 << (idx % 32));
    bv->data[idx / 32] &= mask;
    return 1;
}

int
bitvector_is_set (bitvector_t *bv, const int idx)
{
    unsigned long       mask = 0x1 << (idx % 32);
    return ((bv->data[idx / 32] & mask) != 0);
}
