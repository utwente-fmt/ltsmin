
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

/**
Unfold an element to a tree,
*/
extern void TreeUnfold(treedbs_t dbs,int index,int*vector);

/**
Get the number of elements
*/
extern int TreeCount(treedbs_t dbs);

#endif

