#ifndef DBS_H
#define DBS_H

#include "tls.h"


/**
\file dbs.h
\brief Concurrent DB implementation

Implementation uses locks
*/

/**
Abstract type database.
*/
typedef struct dbs_s *dbs_t;

/**
Create a new database.
*/
extern dbs_t DBScreate(int len);

/**
\brief Find a vector with respect to a database and insert it if it cannot be found.
\param dbs The dbs
\param vector The int vector
\return the index of the vector in  one of the segments of the db
*/
extern int DBSlookup(const dbs_t dbs, const int *vector);

/**
\brief Find a vector with respect to a database and insert it if it cannot be found.
\param dbs The dbs
\param vector The int vector
\retval idx The index that the vector was found or inserted at
\return 1 if the vector was present, 0 if it was added
*/
extern int DBSlookup_ret(const dbs_t dbs, const int *vector, int *idx);

/**
\brief Free the memory used by a dbs.
\param dbs The dbs
*/
extern void DBSfree(dbs_t dbs);

/**
\brief return internal statistics
\see tls.h
\param dbs The dbs
*/
extern stats_t *DBSstats(dbs_t dbs);

#endif

