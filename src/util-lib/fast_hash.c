#include <hre/config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <util-lib/fast_hash.h>

#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)\
                       +(uint32_t)(((const uint8_t *)(d))[0]) )
#endif

uint32_t 
SuperFastHash (const void *data_, int len, uint32_t hash) 
{
    const unsigned char *data = data_;
    uint32_t tmp;
    int rem;

    if (len <= 0 || data == NULL) return 0;

    rem = len & 3;
    len >>= 2;

    /* Main loop */
    for (;len > 0; len--) {
        hash  += get16bits (data);
        tmp    = (get16bits (data+2) << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 2*sizeof (uint16_t);
        hash  += hash >> 11;
    }

    /* Handle end cases */
    switch (rem) {
        case 3: hash += get16bits (data);
                hash ^= hash << 16;
                hash ^= data[sizeof (uint16_t)] << 18;
                hash += hash >> 11;
                break;
        case 2: hash += get16bits (data);
                hash ^= hash << 11;
                hash += hash >> 17;
                break;
        case 1: hash += *data;
                hash ^= hash << 10;
                hash += hash >> 1;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return hash;
}

/*
 * Bob Jenkins, <http://burtleburtle.net/bob/hash/doobs.html>
 * One-at-a-Time hash
 */
uint32_t
oat_hash (const void *data_, int len, uint32_t seed)
{
    const unsigned char *data = data_;
    unsigned             h = seed;
    for (int i = 0; i < len; i++) {
        h += data[i];
        h += (h << 10);
        h ^= (h >> 6);
    }
    h += (h << 3);
    h ^= (h >> 11);
    h += (h << 15);
    return h;
}

int
mix (int a, int b, int c)
{
    a = a - b; a = a - c; a = a ^ (((uint32_t) c) >> 13);
    b = b - c; b = b - a; b = b ^ (a << 8);
    c = c - a; c = c - b; c = c ^ (((uint32_t) b) >> 13);
    a = a - b; a = a - c; a = a ^ (((uint32_t) c) >> 12);
    b = b - c; b = b - a; b = b ^ (a << 16);
    c = c - a; c = c - b; c = c ^ (((uint32_t) b) >> 5);
    c = c - a; c = c - b; c = c ^ (((uint32_t) b) >> 15);
    return c;
}

uint64_t mix64 (uint64_t key)
{
  key = (~key) + (key << 21); // key = (key << 21) - key - 1;
  key = key ^ (((int64_t)key) >> 24);
  key = (key + (key << 3)) + (key << 8); // key * 265
  key = key ^ (((int64_t)key) >> 14);
  key = (key + (key << 2)) + (key << 4); // key * 21
  key = key ^ (((int64_t)key) >> 28);
  key = key + (key << 31);
  return key;
}

uint32_t Mix (uint32_t state)
{
    state += (state << 12);
    state ^= (state >> 22);
    state += (state << 4);
    state ^= (state >> 9);
    state += (state << 10);
    state ^= (state >> 2);
    state += (state << 7);
    state ^= (state >> 12);
    return state;
}

//-----------------------------------------------------------------------------
// MurmurHash2, 64-bit versions, by Austin Appleby

// The same caveats as 32-bit MurmurHash2 apply here - beware of alignment
// and endian-ness issues if used across multiple platforms.

// 64-bit hash for 64-bit platforms

#ifdef __x86_64__

uint64_t MurmurHash64 ( const void * key, int len, unsigned int seed )
{
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint64_t * data = (const uint64_t *)key;
    const uint64_t * end = data + (len/8);

    while(data != end)
    {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char * data2 = (const unsigned char*)data;

    switch(len & 7)
    {
    case 7: h ^= ((uint64_t)data2[6]) << 48;
    case 6: h ^= ((uint64_t)data2[5]) << 40;
    case 5: h ^= ((uint64_t)data2[4]) << 32;
    case 4: h ^= ((uint64_t)data2[3]) << 24;
    case 3: h ^= ((uint64_t)data2[2]) << 16;
    case 2: h ^= ((uint64_t)data2[1]) << 8;
    case 1: h ^= ((uint64_t)data2[0]);
            h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

#else
// 64-bit hash for 32-bit platforms

uint64_t MurmurHash64 ( const void * key, int len, unsigned int seed )
{
    const unsigned int m = 0x5bd1e995;
    const int r = 24;

    unsigned int h1 = seed ^ len;
    unsigned int h2 = 0;

    const unsigned int * data = (const unsigned int *)key;

    while(len >= 8)
    {
        unsigned int k1 = *data++;
        k1 *= m; k1 ^= k1 >> r; k1 *= m;
        h1 *= m; h1 ^= k1;
        len -= 4;

        unsigned int k2 = *data++;
        k2 *= m; k2 ^= k2 >> r; k2 *= m;
        h2 *= m; h2 ^= k2;
        len -= 4;
    }

    if(len >= 4)
    {
        unsigned int k1 = *data++;
        k1 *= m; k1 ^= k1 >> r; k1 *= m;
        h1 *= m; h1 ^= k1;
        len -= 4;
    }

    switch(len)
    {
    case 3: h2 ^= ((unsigned char*)data)[2] << 16;
    case 2: h2 ^= ((unsigned char*)data)[1] << 8;
    case 1: h2 ^= ((unsigned char*)data)[0];
            h2 *= m;
    };

    h1 ^= h2 >> 18; h1 *= m;
    h2 ^= h1 >> 22; h2 *= m;
    h1 ^= h2 >> 17; h1 *= m;
    h2 ^= h1 >> 19; h2 *= m;

    uint64_t h = h1;

    h = (h << 32) | h2;

    return h;
}

#endif
