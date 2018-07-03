#include <hre/config.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <hre/user.h>
#include <util-lib/zobrist.h>

static const hash64_t   INITIAL_HASH = 0;

struct zobrist_s {
    hash64_t          **keys;
    size_t              key_length;
    int                 key_pow;
    uint32_t            mask;
    size_t              length;
    int               **ones;
    hash64_t            start;
    size_t              hits;
    size_t              rows;
};

hash64_t
zobrist_hash (const zobrist_t z, int *v, int *prev, hash64_t h)
{
    if (NULL == prev)
        return INITIAL_HASH;   // start the random sequence
    hash64_t            zhash = h;
    for (size_t i = 0; i < z->length; i++) {
        if (prev[i] != v[i]) {
            zhash ^= z->keys[i][prev[i] & z->mask];
            zhash ^= z->keys[i][v[i] & z->mask];
            // z->hits++;
        }
    }
    return zhash;
}

hash64_t
zobrist_hash_dm (const zobrist_t z, int *v, int *prev, hash64_t h, int g)
{
    if (g == -1)
        return zobrist_hash(z, v, prev, h);
    if (NULL == prev)
        return INITIAL_HASH;   // start the random sequence
    hash64_t            zhash = h;
    for (size_t i = 0; z->ones[g][i] != -1; i++) {
        int                 idx = z->ones[g][i];
        zhash ^= z->keys[idx][prev[idx] & z->mask];
        zhash ^= z->keys[idx][v[idx] & z->mask];
        // z->hits++;
    }
    return zhash;
}

hash64_t
zobrist_rehash (zobrist_t z, hash64_t seed)
{
    return seed ^ z->keys[seed % z->length][seed >> (64 - z->key_pow)];
}

zobrist_t
zobrist_create (size_t length, size_t z_length, matrix_t * m)
{
    zobrist_t           z = RTmalloc (sizeof (struct zobrist_s));
    z->key_length = 1ULL << z_length;
    z->key_pow = z_length;
    z->mask = z->key_length - 1;
    z->length = length;
    z->keys = RTmalloc (sizeof (hash64_t *[length]));
    z->rows = dm_nrows (m);
    srandom (time(NULL));
    for (size_t j = 0; j < length; j++) {
        z->keys[j] = RTmalloc (sizeof (hash64_t[z->key_length]));
        for (size_t i = 0; i < z->key_length; i++) {
            z->keys[j][i] = (((hash64_t)random()) << 32) | rand();
        }
    }
    z->ones = NULL;
    if (m == NULL)
        return z;
    z->ones = RTmalloc (z->rows * sizeof (z->ones[0]));
    for (size_t row = 0; row < z->rows; ++row) {
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
    for (size_t j = 0; j < z->length; j++)
        RTfree (z->keys[j]);
    RTfree (z->keys);
    if (z->ones) {
        for (size_t row = 0; row < z->rows; ++row)
            RTfree (z->ones[row]);
        RTfree (z->ones);
    }
    RTfree (z);
}
