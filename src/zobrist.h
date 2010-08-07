#ifndef ZOBRIST_H
#define ZOBRIST_H

#include <stdint.h>

#include "dm/dm.h"

typedef struct zobrist_s *zobrist_t;

extern uint32_t     zobrist_hash
    (const zobrist_t z, int *v, int *prev, uint32_t h);

extern uint32_t     zobrist_hash_dm
    (const zobrist_t z, int *v, int *prev, uint32_t h, int g);

extern uint32_t     zobrist_rehash (zobrist_t z, uint32_t seed);

extern zobrist_t    zobrist_create (int length, int z, matrix_t * m);

extern void         zobrist_free (zobrist_t z);

#endif
