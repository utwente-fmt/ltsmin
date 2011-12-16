#ifndef BITSET_H
#define BITSET_H

#include <stdio.h>

/**
\brief Library for sets of natural numbers, optimized for sparse and dense sets.

The data structure used is a tree. The top node contains the meta info for the set.
Every internal node refers to a configurable number of children and the leaf nodes
are bitsets of a certain size. To deal with sparse and dense sets, two special
'pointers' are used: ALL_ZEROS and ALL_ONES. These mean empty set and universe, respectively.
*/

/**
\brief Opaque type for a set of integers
*/
typedef struct bitset *bitset_t;

/**
\brief The type of an element.
*/
typedef unsigned int element_t;

/**
\brief Create a new empty set with new allocators.
\param node_size The number of bytes used for the internal nodes.
\param base_size The number of bytes used for the leaf nodes.

Both sizes must be a power of two.
*/
extern bitset_t bitset_create(int node_size,int leaf_size);

/**
\brief Create a new empty set with shared allocators. 
*/
extern bitset_t bitset_create_shared(bitset_t set);

/**
\brief destroy a bitset
*/
extern void bitset_destroy(bitset_t set);

/**
\brief Assign the empty set,
*/
extern void bitset_clear_all(bitset_t set);

/**
\brief Assign the set of all natural numbers.
*/
extern void bitset_set_all(bitset_t set);

/**
\brief Add a range of elements.

This functions adds the interval [low,high] in the set.
*/
extern void bitset_set_range(bitset_t set,element_t low,element_t high);

/**
\brief Remove one element.
*/
extern void bitset_clear(bitset_t set,element_t e);

/**
\brief Add one element.

Return 1 if the element is new, and 0 otherwise.
*/
extern int bitset_set(bitset_t set,element_t e);

/**
\brief Test if an element is a member.
*/
extern int bitset_test(bitset_t set,element_t e);

/**
\brief Compute the complement of the set. */
extern void bitset_invert(bitset_t set);

/**
\brief set1 := set1 intersection set2 
 */
extern void bitset_intersect(bitset_t set1, bitset_t set2);

/**
\brief set1 := set1 union set2
 */
extern void bitset_union(bitset_t set1, bitset_t set2);


/*
 * future expansion might include:
 *
 * extern int bitset_next_clear(bitset_t set,element_t *e);
 * extern int bitset_prev_clear(bitset_t set,element_t *e);
 */

/**
\brief Find the smallest larger or equal element.
*/
extern int bitset_next_set(bitset_t set,element_t *e);

/**
\brief Find the largest smaller or equal element.
*/
extern int bitset_prev_set(bitset_t set,element_t *e);

/**
\brief Print the internal tree representation.
*/
extern void bitset_fprint(FILE*f,bitset_t set);

#endif

