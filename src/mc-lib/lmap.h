#ifndef LM_H
#define LM_H

/**
\file lmap.h
\brief Multimap for storing lattices.
    lmap.h functions like the compact hash table of Geldenhuys & Valmari and
    uses a very simple integrated allocator to hand out blocks.
    The block size B is configurable, with B=2 it degrades to a linked list,
    B=3 is memory optimal in the compact hash table setting and B=8 is optimal
    wrt performance (each block on a cache line)
    Real memory requirements depend on the size of the different sets
    associated with the states.
*/

#include <dm/dm.h>
#include <mc-lib/trace.h>

/**
\typedef
    Associates a reference with a set of lattices and a status.
*/
typedef struct lm_s     lm_t;

/**
\brief A lattice is assumed to be represented by a 64bit pointer.
 */
typedef size_t          lattice_t;
static const lattice_t  NULL_LATTICE = -1;

/**
\typedef The location of the iterator.
 */
typedef size_t          lm_loc_t;
static const lm_loc_t   LM_NULL_LOC = -1;

/**
 * Three bit extenal status flag
 */
typedef uint32_t        lm_status_t;


/**
\typedef The return type of the callback function for the iterator.
 */
typedef enum lm_cb_e {
    LM_CB_NEXT,
    LM_CB_STOP
} lm_cb_t;

/**
\brief Create a new lattice map.
\param len The length of the vectors to be stored here
\return the hashtable
*/
extern lm_t        *lm_create (size_t workers, size_t size, size_t block_size);

typedef lm_cb_t   (*lm_iterate_f)(void *ctx, lattice_t l, lm_status_t status,
                                  lm_loc_t h);

extern void         lm_unlock (lm_t *map, ref_t ref);

extern void         lm_lock (lm_t *map, ref_t ref);

extern int          lm_try_lock (lm_t *map, ref_t ref);

/**
\brief
\param map The map
\param k The key
\param start A pointer to the index to restart iterations
\param cb The callback for to call for each element
\param ctx The context to pass to the callback for thread-safety
\retval The last hash index that was iterated over
\return 1 if the vector was present, 0 if it was added
*/
extern lm_loc_t     lm_iterate_from (lm_t *map, ref_t k,
                                     lm_loc_t *start, lm_iterate_f cb,
                                     void *ctx);

extern lm_loc_t     lm_insert_from_cas (lm_t *map, ref_t k, lattice_t l,
                                        lm_status_t status, lm_loc_t *start);

extern lm_loc_t     lm_insert_from (lm_t *map, ref_t k, lattice_t l,
                                    lm_status_t status, lm_loc_t *start);

extern lattice_t    lm_get (lm_t *map, lm_loc_t loc);

extern lm_status_t  lm_get_status (lm_t *map, lm_loc_t loc);

extern void         lm_set_status (lm_t *map, lm_loc_t loc,
                                   lm_status_t status);

extern void         lm_delete (lm_t *map, lm_loc_t loc);

/**
\brief
A compare-and-swap based delete operation, which does not avoid the ABA problem.
In other words, the clients have to guarantee that the deleted lattice is not
reinserted for the explicit state which loc is associated to.
\param map The map
\param loc the location of a lattice store
\param l the expected lattice stored there
*/
extern void         lm_cas_delete (lm_t *map, lm_loc_t loc, lattice_t l,
                                   lm_status_t status);

extern int          lm_cas_update (lm_t *map, lm_loc_t location,
                                   lattice_t l_old, lm_status_t status_old,
                                   lattice_t l, lm_status_t status);

extern lm_loc_t     lm_insert (lm_t *map, ref_t k, lattice_t l,
                               lm_status_t status);

extern lm_loc_t     lm_iterate (lm_t *map, ref_t k,
                                lm_iterate_f cb, void *ctx);

/**
\brief Free the memory used by a map.
*/
extern void         lm_free (lm_t *map);

extern size_t       lm_allocated (lm_t *map);

#endif
