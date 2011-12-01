#ifndef LMAP_H
#define LMAP_H

/**
\file lattice-map.h
\brief Multimap for storing lattices.
*/

#include <dm/dm.h>
#include <tls.h>
#include <fast_hash.h>
#include <trace.h>


/**
\typedef
    Associates a reference with a set of lattices and a status.
*/
typedef struct lmap_ll_s   *lmap_t;

/**
\brief Status attached to each lattice location.
 * An internal status is used inside this data structure for efficient
 * allocation of lattices. It is not to be modified from external clients.
 * An external states can be defined and used (modified to another external
 * status) elsewhere outside this class.
 */
typedef enum lmap_status_e {
    LMAP_STATUS_EMPTY           = 0,    /* *internal* no lattice stored here */
    LMAP_STATUS_TOMBSTONE       = 1,    /* *internal* lattice delete, can be reused */
    LMAP_STATUS_OCCUPIED1       = 2,    /* *external* lattice stored */
    LMAP_STATUS_OCCUPIED2       = 3     /* *external* lattice stored */
} lmap_status_t;

/**
\brief A lattice is assumed to be represented by a 64bit pointer.
 */
typedef uint64_t            lattice_t;

/**
\typedef The buckets for storing lattices.
 */
typedef struct lmap_store_s {
    lmap_status_t       status  :  4;
    ref_t               ref     : 60;
    lattice_t           lattice;
} lmap_store_t;

/**
\typedef The return type of the callback function for the iterator.
 */
typedef enum lmap_cb_e {
    LMAP_CB_NEXT,
    LMAP_CB_STOP
} lmap_cb_t;

/**
\typedef The location of the iterator.
 */
typedef uint64_t       lmap_loc_t;

/**
\brief Create a new lattice map.
\param len The length of the vectors to be stored here
\return the hashtable
*/
extern lmap_t       lmap_create (size_t key_size, size_t data_size, int size);

typedef lmap_cb_t   (*lmap_iterate_f)(void *ctx, lmap_store_t *ld, lmap_loc_t h);

/**
\brief
\param map The map
\param k The key
\param hash A pointer to hash index to restart iterations
\param cb The callback for to call for each element
\param ctx The context to pass to the callback for thread-safety
\retval The last hash index to iterate over
\return 1 if the vector was present, 0 if it was added
*/
extern lmap_loc_t   lmap_iterate_hash (const lmap_t map, ref_t k,
                                       lmap_loc_t *start, lmap_iterate_f cb,
                                       void *ctx);

extern lmap_status_t lmap_lookup (const lmap_t map, ref_t k, lattice_t l);

extern lmap_loc_t   lmap_insert_hash (const lmap_t map, ref_t k,
                                      lattice_t l, lmap_status_t status,
                                      lmap_loc_t *start);

extern lmap_status_t lmap_get (const lmap_t map, lmap_loc_t hash);

extern void         lmap_set (const lmap_t map, lmap_status_t status,
                              lmap_loc_t hash);

extern lmap_loc_t   lmap_insert (const lmap_t map, ref_t k, lattice_t l,
                                 lmap_status_t status);

extern lmap_loc_t   lmap_iterate (const lmap_t map, ref_t k,
                                  lmap_iterate_f cb, void *ctx);

/**
\brief Free the memory used by a map.
*/
extern void         lmap_free (lmap_t map);

/**
\brief return a copy of internal statistics
\see tls.h
\param map The map
\returns a copy of the statistics, to be freed with free
*/
extern stats_t     *lmap_stats (lmap_t map);

#endif
