#ifndef VECTOR_SET_H
#define VECTOR_SET_H

#include <popt.h>
#include "bignum/bignum.h"

/**
\file vector_set.h
\defgroup vector_set Data structures for manipulating sets of vectors.

For manipulating sets of vectors, we need use three objects: domains,
sets and relations.
*/
//@{

extern struct poptOption vset_options[];

/**
\brief Abstract type for a domain.
*/
typedef struct vector_domain *vdom_t;

/**
\brief Abstract type for a set in a domain.
*/
typedef struct vector_set *vset_t;

/**
\brief Abstract type for a relation over a domain.
*/
typedef struct vector_relation *vrel_t;

/**
\brief Create a domain that uses the default vector set implementation.

\param n The length of vectors in the domain.
*/
extern vdom_t vdom_create_default(int n);

/**
\brief Create a domain that uses the fdd form of BuDDy.

\param n The length of vectors in the domain.
*/
extern vdom_t vdom_create_fdd(int n);

#ifdef HAVE_ATERM2_H
/**
\brief Create a domain that uses the list variant of ATermDD.

\param n The length of vectors in the domain.
*/
extern vdom_t vdom_create_list(int n);

/**
\brief Create a domain that uses the tree variant of ATermDD.

\param n The length of vectors in the domain.
*/
extern vdom_t vdom_create_tree(int n);
#endif

/**
\brief Create a domain that uses the native implementation of the list MDD type.

\param n The length of vectors in the domain.
*/
extern vdom_t vdom_create_list_native(int n);

/**
\brief Create a set.

\param k If non-zero this indicates the length of the sub-domain.
\param proj If non-NULL this is a sorted list of the indices of the sub-domain.
*/
extern vset_t vset_create(vdom_t dom,int k,int* proj);

/**
\brief Add an element to a set.
*/
extern void vset_add(vset_t set,const int* e);

/**
\brief Test if an element is a member.
*/
extern int vset_member(vset_t set,const int* e);

/**
\brief Test if two sets are equal.
*/
extern int vset_equal(vset_t set1,vset_t set2);

/**
\brief Test if a set is empty.
*/
extern int vset_is_empty(vset_t set);

/**
\brief Remove all elements from a set.
*/
extern void vset_clear(vset_t set);

/**
\brief Callback for set enumeration.
*/
typedef void(*vset_element_cb)(void*context,int *e);

/**
\brief Enumerate the elements of a set.

For each element of the given set, the given callback with be called with as arguments
the given context and the set element.
*/
extern void vset_enum(vset_t set,vset_element_cb cb,void* context);

/**
\brief Enumerate the elements of a set that match the given projection.

For each element of the given set, the given callback with be called with as arguments
the given context and the set element.
*/
extern void vset_enum_match(vset_t set,int p_len,int* proj,int*match,vset_element_cb cb,void* context);

/**
\brief Copy the elements of a set that match the given projection.
*/
extern void vset_copy_match(vset_t dst, vset_t src, int p_len,int* proj,int*match);

/**
\brief Produce a member of a non-empty set.
*/
extern void vset_example(vset_t set,int *e);

/**
\brief Copy a vset.
*/
extern void vset_copy(vset_t dst,vset_t src);

/**
\brief Project src down to dst.
*/
extern void vset_project(vset_t dst,vset_t src);

/**
\brief dst := dst U src
*/
extern void vset_union(vset_t dst,vset_t src);

/**
\brief dst := (a | a \in dst and a \in src)
*/
extern void vset_intersect(vset_t dst,vset_t src);

/**
\brief dst := dst \\ src
*/
extern void vset_minus(vset_t dst,vset_t src);

/**
\brief (dst,src) := (dst U src,src \\ dst)
*/
extern void vset_zip(vset_t dst,vset_t src);

/**
\brief Count the number of diagram nodes and the number of elements stored.

\param elements Pointer to bignum that will contain the count; this bignum
is initialized by vset_count.
*/
extern void vset_count(vset_t set,long *nodes,bn_int_t *elements);

/**
\brief Create a relation
*/
extern vrel_t vrel_create(vdom_t dom,int k,int* proj);

/**
\brief Add an element to a relation.
*/
extern void vrel_add(vrel_t rel,const int* src,const int* dst);

/**
\brief Count the number of diagram nodes and the number of elements stored.

\param elements Pointer to bignum that will contain the count; this bignum
is initialized by vset_count.
*/
extern void vrel_count(vrel_t rel,long *nodes,bn_int_t *elements);

/**
\brief dst := { y | exists x in src : x rel y }
*/
extern void vset_next(vset_t dst,vset_t src,vrel_t rel);

/**
\brief dst := { x | exists y in src : x rel y }
*/
extern void vset_prev(vset_t dst,vset_t src,vrel_t rel);

extern void vset_reorder(vdom_t dom);

/**
\brief Destroy a vset
*/
extern void vset_destroy(vset_t set);

/**
\brief Do a least fixpoint using the argument rel on the source states.

This computes the smallest set S inductively satisfying source in S
and rel(S) in S.
*/
void vset_least_fixpoint(vset_t dst, vset_t src, vrel_t rels[], int rel_count);

//@}

#endif

