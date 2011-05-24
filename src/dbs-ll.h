#ifndef DBS_LL_H
#define DBS_LL_H

/**
\file dbs-ll.h
\brief Concurrent synchronous hashtable implementation for fixed size hashes

Implementation uses lockless operations

\todo implement resizing
*/

#include <dm/dm.h>
#include <tls.h>
#include <fast_hash.h>


/**
\typedef Lockless hastable database.
*/
typedef struct dbs_ll_s *dbs_ll_t;

typedef stats_t   *(*dbs_stats_f) (const void *dbs);
typedef int       *(*dbs_get_f) (const void *dbs, int idx, int *dst);
typedef int        (*dbs_try_set_sat_f) (const void *dbs, const int idx,
                                          int index);
typedef int        (*dbs_get_sat_f) (const void *dbs, const int idx,
                                      int index);

/**
\brief Create a new database.
\param len The length of the vectors to be stored here
\return the hashtable
*/
extern dbs_ll_t     DBSLLcreate (int len);
extern dbs_ll_t     DBSLLcreate_sized (int len, int size, hash32_f hash32, int bits);

/**
\brief Find a vector with respect to a database and insert it if it cannot be fo
und.
\param dbs The dbs
\param vector The int vector
\return the index of the vector in  one of the segments of the db
*/
extern uint32_t     DBSLLlookup (const dbs_ll_t dbs, const int *vector);

extern uint16_t     DBSLLget_sat_bits (const dbs_ll_t dbs, const int idx);

extern void         DBSLLset_sat_bits (const dbs_ll_t dbs, const int idx,
                                       uint16_t value);

extern int          DBSLLtry_set_sat_bit (const dbs_ll_t dbs, const int idx,
                                          int index);
   
extern int          DBSLLget_sat_bit (const dbs_ll_t dbs, const int idx,
                                      int index);

/**
\brief Find a vector with respect to a database and insert it if it cannot be fo
und.
\param dbs The dbs
\param vector The int vector
\retval idx The index that the vector was found or inserted at
\return 1 if the vector was present, 0 if it was added
*/
extern int          DBSLLlookup_ret (const dbs_ll_t dbs, const int *v,
                                     uint32_t *ret);
extern int          DBSLLlookup_hash (const dbs_ll_t dbs, const int *v,
                                      uint32_t *ret, uint32_t * hh);

extern int         *DBSLLget (const dbs_ll_t dbs, const int idx, int *dst);

extern uint32_t     DBSLLmemoized_hash (const dbs_ll_t dbs, const int idx);

/**
\brief Free the memory used by a dbs.
*/
extern void         DBSLLfree (dbs_ll_t dbs);

/**
\brief return a copy of internal statistics
\see tls.h
\param dbs The dbs
\returns a copy of the statistics, to be freed with free
*/
extern stats_t     *DBSLLstats (dbs_ll_t dbs);

#endif
