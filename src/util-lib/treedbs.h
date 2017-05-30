
#ifndef TREEDBS_H
#define TREEDBS_H

/**
\file treedbs.h
\brief Implementation of tree compression.
*/

/**
Abstract type tree database.
*/
typedef struct treedbs_s *treedbs_t;

/**
Create a new tree database.
*/
extern treedbs_t TreeDBScreate(int len);

/**
Fold a vector with respect to a database.
*/
extern int TreeFold(treedbs_t dbs,int *vector);
extern int TreeFold_ret(treedbs_t dbs,int *vector, int *idx);
extern int TreeDBSlookup(treedbs_t dbs,int *vector);
extern int TreeDBSlookup_ret(treedbs_t dbs,int *vector, int *idx);

/**
Get a single element of a vector.
*/
extern int TreeDBSGet(treedbs_t dbs,int index,int pos);

/**
Unfold an element to a tree,
*/
extern void TreeUnfold(treedbs_t dbs,int index,int*vector);

/**
Get the number of elements
*/
extern int TreeCount(treedbs_t dbs);

/**
Print node count info for the given dbs.
 */
extern void TreeInfo(treedbs_t dbs);

/**
 *
 */
void TreeDBSclear(treedbs_t dbs);

/**
\brief Free the memory used by a tree dbs.
*/
extern void TreeDBSfree(treedbs_t dbs);

extern void TreeDBSstats(treedbs_t dbs);

#endif

