#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "zobrist.h"
#include "runtime.h"


struct zobrist_s {
    int32_t           **keys;
    int                 key_length;
    int                 key_pow;
    int                 mask;
    int                 length;
    int               **ones;
    uint32_t            start;
    size_t              hits;
};

uint32_t
zobrist_hash (const zobrist_t z, int *v, int *prev, uint32_t h)
{
    if (NULL == prev)
        return z->start = random ();   // start the random sequence
    uint32_t            zhash = h;
    for (int i = 0; i < z->length; i++) {
        if (prev[i] != v[i]) {
            zhash ^= z->keys[i][prev[i] & z->mask];
            zhash ^= z->keys[i][v[i] & z->mask];
            // z->hits++;
        }
    }
    return zhash;
}

uint32_t
zobrist_hash_dm (const zobrist_t z, int *v, int *prev, uint32_t h, int g)
{
    if (g == -1)
        return zobrist_hash(z, v, prev, h);
    if (NULL == prev)
        return z->start = random ();   // start the random sequence
    uint32_t            zhash = h;
    for (size_t i = 0; z->ones[g][i] != -1; i++) {
        int                 idx = z->ones[g][i];
        zhash ^= z->keys[idx][prev[idx] & z->mask];
        zhash ^= z->keys[idx][v[idx] & z->mask];
        // z->hits++;
    }
    return zhash;
}

uint32_t
zobrist_rehash (zobrist_t z, uint32_t seed)
{
    return seed ^ z->keys[seed % z->length][seed >> (32 - z->key_pow)];
}

zobrist_t
zobrist_create (int length, int z_length, matrix_t * m)
{
    zobrist_t           z = RTmalloc (sizeof (struct zobrist_s));
    z->key_length = 1 << z_length;
    z->key_pow = z_length;
    z->mask = z->key_length - 1;
    z->length = length;
    z->keys = RTmalloc (sizeof (int32_t *[length]));
    srandom (time(NULL));
    for (int j = 0; j < length; j++) {
        z->keys[j] = RTmalloc (sizeof (int32_t[z->key_length]));
        for (int i = 0; i < z->key_length; i++) {
            z->keys[j][i] = (uint32_t) random ();
        }
    }
    z->ones = NULL;
    if (m == NULL)
        return z;
    z->ones = RTmalloc (dm_nrows (m) * sizeof (z->ones[0]));
    for (int row = 0; row < dm_nrows (m); ++row) {
        z->ones[row] = RTmalloc ((1 + 
                       dm_ones_in_row (m, row)) * sizeof (z->ones[0][0]));
        dm_row_iterator_t   ri;
        dm_create_row_iterator (&ri, m, row);
        int                 i,
                            j = 0;
        while ((i = dm_row_next (&ri)) != -1)
            z->ones[row][j++] = i;
        z->ones[row][j] = -1;
    }
    return z;
}

void
zobrist_free (zobrist_t z)
{
    RTfree (z->keys);
    if (z->keys)
        RTfree (z->keys);
    RTfree (z);
}
