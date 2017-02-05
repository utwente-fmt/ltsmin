#ifndef DBS_LL_H
#define DBS_LL_H

/**
\file dbs-ll.h
\brief Lockless non-resizing hash table implementation for fixed-length keys

@inproceedings{Laarman:2010:BMR:1998496.1998541,
  author = {Laarman, Alfons and van de Pol, Jaco and Weber, Michael},
  title = {{Boosting Multi-Core Reachability Performance with Shared Hash Tables}},
  booktitle = {Proceedings of the 2010 Conference on Formal Methods in Computer-Aided Design},
  series = {FMCAD '10},
  year = {2010},
  location = {Lugano, Switzerland},
  pages = {247--256},
  numpages = {10},
  url = {http://eprints.eemcs.utwente.nl/19281/},
  acmid = {1998541},
  publisher = {FMCAD Inc},
  address = {Austin, TX},
}
*/

#include <stdbool.h>

#include <dm/dm.h>
#include <mc-lib/stats.h>
#include <util-lib/fast_hash.h>

#define             DB_FULL -1
#define             DB_NOT_FOUND -9

/**
\typedef Lockless hastable database.
*/
typedef struct dbs_ll_s *dbs_ll_t;

typedef uint16_t mem_hash_t;
typedef size_t dbs_ref_t;

typedef stats_t   *(*dbs_stats_f) (const void *dbs);
typedef int       *(*dbs_get_f) (const void *dbs, dbs_ref_t ref, int *dst);
typedef int        (*dbs_try_set_sat_f) (const void *dbs, const dbs_ref_t ref,
                                         int index);
typedef int        (*dbs_try_set_sats_f)(const void *dbs, const dbs_ref_t ref,
                                         size_t bits, size_t offs,
                                         uint64_t old_val, uint64_t new_val);
typedef int        (*dbs_get_sat_f) (const void *dbs, const dbs_ref_t ref,
                                     int index);
typedef void       (*dbs_unset_sat_f) (const void *dbs, const dbs_ref_t ref,
                                       int index);
typedef uint32_t   (*dbs_inc_sat_bits_f) (const void *dbs, const dbs_ref_t ref);
typedef uint32_t   (*dbs_dec_sat_bits_f) (const void *dbs, const dbs_ref_t ref);
typedef uint32_t   (*dbs_get_sat_bits_f) (const void *dbs, const dbs_ref_t ref);


/**
\brief Create a new database.
\param len The length of the vectors to be stored here
\return the hashtable
*/
extern dbs_ll_t     DBSLLcreate (int len);
extern dbs_ll_t     DBSLLcreate_sized (int len, int size, hash64_f hash64, int bits);

/**
\brief Find a vector with respect to a database and insert it if it cannot be fo
und.
\param dbs The dbs
\param vector The int vector
\return the index of the vector in  one of the segments of the db
*/
extern int          DBSLLtry_set_sat_bits (const dbs_ll_t dbs, const ref_t ref,
                                           size_t bits, size_t offs,
                                           uint64_t exp, uint64_t new_val);

extern mem_hash_t   DBSLLget_sat_bits (const dbs_ll_t dbs, const dbs_ref_t ref);

extern void         DBSLLset_sat_bits (const dbs_ll_t dbs, const dbs_ref_t ref,
                                       mem_hash_t value);

extern int          DBSLLtry_set_sat_bit (const dbs_ll_t dbs, const dbs_ref_t ref,
                                          int index);

extern int          DBSLLtry_unset_sat_bit (const dbs_ll_t dbs, const dbs_ref_t ref,
                                          int index);
   
extern int          DBSLLget_sat_bit (const dbs_ll_t dbs, const dbs_ref_t ref,
                                      int index);

extern void         DBSLLunset_sat_bit (const dbs_ll_t dbs, const dbs_ref_t ref,
                                        int index);


extern mem_hash_t   DBSLLinc_sat_bits (const dbs_ll_t dbs, const dbs_ref_t ref);

extern mem_hash_t   DBSLLdec_sat_bits (const dbs_ll_t dbs, const dbs_ref_t ref);

/**
\brief Find a vector with respect to a database and insert it if it cannot be fo
und.
\param dbs The dbs
\param vector The int vector
\retval idx The index that the vector was found or inserted at
\return 1 if the vector was present, 0 if it was added
*/
extern int          DBSLLfop_hash (const dbs_ll_t dbs, const int *v,
                                   dbs_ref_t *ret, hash64_t *hash, bool insert);

extern int         *DBSLLget (const dbs_ll_t dbs, const dbs_ref_t ref, int *dst);

extern mem_hash_t   DBSLLmemoized_hash (const dbs_ll_t dbs, const dbs_ref_t ref);

static inline int
DBSLLlookup_hash (const dbs_ll_t dbs, const int *v, dbs_ref_t *ret, hash64_t *hash)
{
    return DBSLLfop_hash (dbs, v, ret, hash, true);
}

static inline int
DBSLLlookup_ret (const dbs_ll_t dbs, const int *v, dbs_ref_t *ret)
{
    return DBSLLfop_hash (dbs, v, ret, NULL, true);
}

static inline int
DBSLLfind_hash (const dbs_ll_t dbs, const int *v, dbs_ref_t *ret, hash64_t *hash)
{
    return DBSLLfop_hash (dbs, v, ret, hash, false);
}

/**
\brief Free the memory used by a dbs.
*/
extern void         DBSLLfree (dbs_ll_t dbs);

/**
\brief return a copy of internal statistics
\see stats.h
\param dbs The dbs
\returns a copy of the statistics, to be freed with free
*/
extern stats_t     *DBSLLstats (dbs_ll_t dbs);

#endif
