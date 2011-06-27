
#ifndef TREEDBS_LL_H
#define TREEDBS_LL_H

#include "tls.h"
#include "dm/dm.h"

/**
\file treedbs-ll.h
\brief Implementation of tree compression using lockless hashtables.
*/

static const int    DB_SIZE_MAX = 32;

/**
Abstract type tree database.
*/
typedef struct treedbs_ll_s *treedbs_ll_t;

typedef size_t tree_ref_t;

typedef int *tree_t;

/**
Create a new tree database.

- Autosized
- sized
- incremental using the dependency matrix
*/
extern treedbs_ll_t TreeDBSLLcreate (int len, int satellite_bits);
extern treedbs_ll_t TreeDBSLLcreate_sized (int len, int size,
                                           int satellite_bits);
extern treedbs_ll_t TreeDBSLLcreate_dm (int len, int size, matrix_t *m,
                                        int satellite_bits);

extern int          TreeDBSLLtry_set_sat_bit (const treedbs_ll_t dbs,
                                              const tree_ref_t ref, int index);
extern int          TreeDBSLLget_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref,
                                          int index);
extern void         TreeDBSLLset_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref,
                                           uint16_t value);
extern uint32_t     TreeDBSLLget_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref);
extern uint32_t     TreeDBSLLinc_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref);
extern uint32_t     TreeDBSLLdec_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref);

/**
\brief Find a vector with respect to a database and insert it if it cannot be fo
und.
\param dbs The dbs
\param vector The int vector
\retval ret The index that the vector was found or inserted at
\param g the group to do incremntal lookup for (-1 unknown)
\retval arg the input of the previous vector and the output of the new 
            internal tree data
\return 1 if the vector was present, 0 if it was added
*/
extern int          TreeDBSLLlookup (const treedbs_ll_t dbs, const int *v);
extern int          TreeDBSLLlookup_incr (const treedbs_ll_t dbs, const int *v, 
                                          tree_t prev, tree_t next);
extern int          TreeDBSLLlookup_dm (const treedbs_ll_t dbs, const int *v, 
                                        tree_t prev, tree_t next, int group);

extern tree_t       TreeDBSLLget (const treedbs_ll_t dbs, const tree_ref_t ref, 
                                  int *dst);

extern uint32_t     TreeDBSLLindex (tree_t data);
extern int         *TreeDBSLLdata (const treedbs_ll_t dbs, tree_t data);

/**
\brief Free the memory used by a tree dbs.
*/
extern void         TreeDBSLLfree (treedbs_ll_t dbs);

extern void         TreeDBSLLcache (treedbs_ll_t dbs, size_t size);

/**
\brief return internal statistics
\see tls.h
\param dbs The dbs
*/
extern stats_t     *TreeDBSLLstats (treedbs_ll_t dbs);

#endif
