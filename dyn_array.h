#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

/** @file dyn_array.h
 * @brief utilities for managing dynamically resizing arrays
 */

#include "config.h"

typedef struct dynamic_array *da_t;
typedef enum {EXACT, DOUBLE, FIBONACCI, LINEAR} da_policy_t;

extern da_t DAcreate(size_t init,da_policy_t policy);

extern void DAmax(da_t da,uint32_t index);

extern void DAadd(da_t da,void**array,size_t e_size);

#endif

