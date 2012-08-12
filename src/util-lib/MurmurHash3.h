//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

#ifndef _MURMURHASH3_H_
#define _MURMURHASH3_H_

#include <stdint.h>

//-----------------------------------------------------------------------------

extern void MurmurHash3_x86_32  ( const void * key, int len, uint32_t seed, void * out );

extern void MurmurHash3_x86_128 ( const void * key, int len, uint32_t seed, void * out );

extern void MurmurHash3_x64_128 ( const void * key, int len, uint32_t seed, void * out );

//-----------------------------------------------------------------------------

static inline uint64_t
MurmurHash64 (const void * key, int len, unsigned int seed)
{
    uint64_t hash[2];
#ifdef __x86_64__
    MurmurHash3_x64_128 (key, len, seed, hash);
#else
    MurmurHash3_x86_128 (key, len, seed, hash);
#endif
    hash[0] ^= hash[1];
    return hash[0];
}

static inline uint32_t
MurmurHash32 (const void * key, int len, unsigned int seed)
{
    uint32_t hash;
    MurmurHash3_x86_32 (key, len, seed, &hash);
    return hash;
}

#endif // _MURMURHASH3_H_
