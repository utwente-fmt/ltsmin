#ifndef VECTOR_SET_H
#define VECTOR_SET_H

#include <popt.h>

#include <bignum/bignum.h>

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
\brief Vector set implementation identifier.
*/
typedef enum {
    VSET_IMPL_AUTOSELECT,
    VSET_AtermDD_list,
    VSET_AtermDD_tree,
    VSET_BuDDy_fdd,
    VSET_DDD,
    VSET_ListDD,
    VSET_ListDD64,
    VSET_Sylvan,
} vset_implementation_t;

/**
\brief Create a domain that uses some vector set implementation.

\param n The length of vectors in the domain.
\param impl The particular vector set implementation identifier
*/
extern vdom_t vdom_create_domain(int n, vset_implementation_t impl);

/**
\brief Create a set.

\param k If non-negative this indicates the length of the sub-domain.
\param proj If non-NULL this is a sorted list of the indices of the sub-domain.
*/
extern vset_t vset_create(vdom_t dom,int k,int* proj);

/**
 * \brief Saves a set to a file.
 * \param f the file
 * \param set the set
 */
extern void vset_save(FILE* f, vset_t set);

/**
 * \brief Reads a set from a file.
 * \param f the file
 * \return the set
 */
extern vset_t vset_load(FILE* f, vdom_t dom);

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
\brief Copy the elements of a set that match the given projection.
*/
extern void vset_copy_match_proj(vset_t dst, vset_t src, int p_len, int* proj, int p_id, int*match);

/**
\brief Create a projection.
*/
extern int vproj_create(vdom_t dom, int p_len, int* proj);

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
\brief dst := (a | a in dst and a in src)
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
 * \brief Saves the projection of a relation to a file.
 * \param f the file
 * \param rel the relation
 */
extern void vrel_save_proj(FILE* f, vrel_t rel);

/**
 * \brief Saves a relation to a file.
 * \param f the file
 * \param rel the relation
 */
extern void vrel_save(FILE* f, vrel_t rel);

/**
 * \brief Reads a projection from file and creates a relation based on the projection.
 * \param f the file
 * \param dom the domain
 * \return the relation
 */
extern vrel_t vrel_load_proj(FILE* f, vdom_t dom);

/**
 * \brief Reads a relation from a file.
 * \param f the file
 * \param rel the relation
 */
extern void vrel_load(FILE* f, vrel_t rel);

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
\brief Callback for expanding a relation
*/
typedef void (*expand_cb)(vrel_t rel, vset_t set, void *context);

/**
\brief Set the method for expanding a relation
*/
void vrel_set_expand(vrel_t rel, expand_cb cb, void *context);

/**
\brief Do a least fixpoint using the argument rels on the source states.

This computes the smallest set S inductively satisfying source in S
and rels(S) in S.

Both dst and src arguments must be defined over the complete domain and
not over sub-domains. A relation in rels is expanded on-the-fly in case
an expand callback is set.
*/
void vset_least_fixpoint(vset_t dst, vset_t src, vrel_t rels[], int rel_count);

void vset_dot(FILE* fp, vset_t src);

void vrel_dot(FILE* fp, vrel_t src);

//@}

#endif

