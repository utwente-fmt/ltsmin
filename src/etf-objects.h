#ifndef ETF_OBJECTS_H
#define ETF_OBJECTS_H

/**
\file etf-objects.h
\brief Data types to manipulate objects in an ETF model.
*/

/**
\brief Opaque type of a relation.
*/
typedef struct etf_rel_s *etf_rel_t;

/**
\brief Create a new ETF relation.
*/
extern etf_rel_t ETFrelCreate(int length,int labels);

/**
\brief Destroy an ETF relation.
*/
extern void ETFrelDestroy(etf_rel_t *rel_p);

/**
\brief Add a new triple to an ETF relation.
*/
extern void ETFrelAdd(etf_rel_t rel,int *src,int*dst,int*label);

/**
\brief Reset the iterator of an ETF relation.
*/
extern void ETFrelIterate(etf_rel_t rel);

/**
\brief Get the next triple for the ETF relation.
*/
extern int ETFrelNext(etf_rel_t rel,int *src,int*dst,int*label);

/**
\brief Get the number of triples in the relation.
*/
extern int ETFrelCount(etf_rel_t rel);

/**
\brief Print statistics for the relation.
*/
extern void ETFrelInfo(etf_rel_t rel);

/**
\brief Opaque type of a map.
*/
typedef struct etf_map_s *etf_map_t;

/**
\brief Create a new ETF map.
*/
extern etf_map_t ETFmapCreate(int length);

/**
\brief Destroy an ETF map.
*/
extern void ETFmapDestroy(etf_map_t *map_p);

/**
\brief Add a key/value entry to a map.
*/
extern void ETFmapAdd(etf_map_t map,int* state,int value);

/**
\brief Reset the iterator of the map.
*/
extern void ETFmapIterate(etf_map_t map);

/**
\brief Get the next entry in the map.
*/
extern int ETFmapNext(etf_map_t map,int *state,int*val);

/**
\brief Get the number of entries in the map.
*/
extern int ETFmapCount(etf_map_t map);


/**
\brief Opaque type of a set.
*/
typedef struct etf_set_s *etf_set_t;

/**
\brief Create a new ETF set.
*/
extern etf_set_t ETFsetCreate(int length);

/**
\brief Destroy an ETF set.
*/
extern void ETFsetDestroy(etf_set_t *set_p);

/**
\brief Add a key/value entry to a set.
*/
extern void ETFsetAdd(etf_set_t set,int* state);

/**
\brief Reset the iterator of the set.
*/
extern void ETFsetIterate(etf_set_t set);

/**
\brief Get the next entry in the set.
*/
extern int ETFsetNext(etf_set_t set,int *state);

/**
\brief Get the number of entries in the set.
*/
extern int ETFsetCount(etf_set_t set);



#endif

