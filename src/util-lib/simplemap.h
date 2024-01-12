/*
 * simplemap.h
 *
 *  Created on: 31 Jul 2012
 *      Author: kant
 */

#ifndef SIMPLEMAP_H_
#define SIMPLEMAP_H_

#include <config.h>

#include <stdint.h>
#include <stdbool.h>

#include <hre/runtime.h>

typedef struct entry_t {
    uint32_t key;
    uint32_t value;
} entry_t;

typedef struct map_t {
    uint32_t size;
    entry_t* values;
} map_t;

/**
\brief Creates a simple map.
*/
map_t simplemap_create(uint32_t size);

void simplemap_destroy(map_t map);

/**
\brief Puts an element for the key.
*/
bool simplemap_put(map_t map, uint32_t key, uint32_t value);

/**
\brief Gets the element for the key.
*/
uint32_t simplemap_get(map_t map, uint32_t key);



typedef struct entry64_t {
    uint64_t key;
    uint64_t value;
} entry64_t;

typedef struct map64_t {
    uint64_t size;
    entry64_t* values;
} map64_t;

/**
\brief Creates a simple map.
*/
map64_t simplemap64_create(uint64_t size);

void simplemap64_destroy(map64_t map);

/**
\brief Puts an element for the key.
*/
bool simplemap64_put(map64_t map, uint64_t key, uint64_t value);

/**
\brief Gets the element for the key.
*/
uint64_t simplemap64_get(map64_t map, uint64_t key);


#endif /* SIMPLEMAP_H_ */
