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
    VSET_LDDmc,
} vset_implementation_t;

extern vset_implementation_t vset_default_domain;

/**
\brief Create a domain that uses some vector set implementation.

\param n The length of vectors in the domain.
\param impl The particular vector set implementation identifier
*/
extern vdom_t vdom_create_domain(int n, vset_implementation_t impl);

/**
\brief Create a domain that uses some vector set implementation from a file where the same
       vector set implementation was saved earlier using vdom_save.

\param f a file
\param impl The particular vector set implementation identifier
*/
extern vdom_t vdom_create_domain_from_file(FILE *f, vset_implementation_t impl);

int vdom_vector_size(vdom_t dom);

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
\brief Callback for vset_update. Given state vector e, add new states to set.
*/
typedef void(*vset_update_cb)(vset_t set, void *context, int *e);

/**
\brief Update a set with new states, obtained by calling cb for every state in set.
*/
extern void vset_update(vset_t dst, vset_t set, vset_update_cb cb, void *context);

/**
\brief Update a set with new states, obtained by calling cb for every state in set.
This is a sequential operation.
*/
extern void vset_update_seq(vset_t dst, vset_t set, vset_update_cb cb, void *context);

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
\brief Produce a member of a non-empty set that matches the given projection.
*/
extern void vset_example_match(vset_t set, int *e, int p_len, int* proj, int*match);

/**
\brief Randomly produce a member of a non-empty set.
*/
extern void vset_random(vset_t set,int *e);

/**
\brief Copy a vset.
*/
extern void vset_copy(vset_t dst,vset_t src);

/**
\brief Project src down to dst.
*/
extern void vset_project(vset_t dst,vset_t src);

/**
\brief Project src down to dst and minus with minus.
*/
extern void vset_project_minus(vset_t dst,vset_t src,vset_t minus);

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
extern void vset_count(vset_t set,long *nodes,double *elements);

extern void vset_count_precise(vset_t set,long nodes,bn_int_t *elements);

extern int vdom_supports_precise_counting(vdom_t dom);

extern void vset_ccount(vset_t set,long *nodes,long double *elements);

extern int vdom_supports_ccount(vdom_t dom);

/**
\brief Create a relation
*/
extern vrel_t vrel_create(vdom_t dom,int k,int* proj);

/**
\brief Create a relation with read write separation
*/
extern vrel_t vrel_create_rw(vdom_t dom,int r_k,int* r_proj,int w_k,int* w_proj);

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
\brief Callback for vrel_update. Given state vector e, add new relations to rel.
*/
typedef void(*vrel_update_cb)(vrel_t rel, void *context, int *e);

/**
\brief Update a relation with new transitions, obtained by calling cb for every state in set.
*/
extern void vrel_update(vrel_t rel, vset_t set, vrel_update_cb cb, void *context);

/**
\brief Update a relation with new transitions, obtained by calling cb for every state in set.
This is a sequential operation.
*/
extern void vrel_update_seq(vrel_t rel, vset_t set, vrel_update_cb cb, void *context);

/**
\brief Add an element to a relation, with a copy vector.
*/
extern void vrel_add_cpy(vrel_t rel,const int* src,const int* dst,const int* cpy);

/**
\brief Add an element to a relation, with a copy vector, and an action
*/
extern void vrel_add_act(vrel_t rel,const int* src,const int* dst,const int* cpy,const int act);

/**
\brief Count the number of diagram nodes and the number of elements stored.

\param elements Pointer to bignum that will contain the count; this bignum
is initialized by vset_count.
*/
extern void vrel_count(vrel_t rel,long *nodes,double *elements);

/**
\brief dst := { y | exists x in src : x rel y }
*/
extern void vset_next(vset_t dst,vset_t src,vrel_t rel);

extern void vset_next_union(vset_t dst,vset_t src,vrel_t rel,vset_t uni);

/**
\brief  univ = NULL => dst := { x | exists y in src : x rel y }
        univ != NULL => dst := { x | exists y in src : x rel y } ...
*/
extern void vset_prev(vset_t dst,vset_t src,vrel_t rel, vset_t univ);

extern void vset_universe(vset_t dst, vset_t src);

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

/**
\brief res := (a | a in left and a in right), like join in relational databases.
Put result in dst.
*/
extern void vset_join(vset_t dst, vset_t left, vset_t right);

/**
\brief Hook to call before all loading.
*/
void vset_pre_load(FILE* f, vdom_t dom);

/**
\brief Hook to call after all loading.
*/
void vset_post_load(FILE* f, vdom_t dom);

/**
\brief Hook to call before all saving.
*/
void vset_pre_save(FILE* f, vdom_t dom);

/**
\brief Hook to call after all saving.
*/
void vset_post_save(FILE* f, vdom_t dom);

/**
 * \brief Saves domain information to a file;
 * \param f the file
 * \param dom the domain
 */
void vdom_save(FILE *f, vdom_t dom);

/**
\brief returns whether the vset implementation separates read and write dependencies.
*/
int vdom_separates_rw(vdom_t dom);

/**
\brief sets the name of the ith variable.
*/
void vdom_set_name(vdom_t dom, int i, char* name);

/**
\brief get the name of the ith variable.
*/
char* vdom_get_name(vdom_t dom, int i);

int _cache_diff();

/*
vset visitor api, with caching mechanism.
*/

/**
\brief Pre-order visit of a value.

\param terminal Whether or not this is a terminal.
\param val The value.
\param cached Whether or not there is a cached result available.
\param result The cached result (if available).
\param context The user context.
*/
typedef void (*vset_visit_pre_cb) (int terminal, int val, int cached, void* result, void* context);

/**
\brief Allocates a new user context on the stack.

\param context The context allocated on the stack.
\param parent The parent context.
\param succ Whether or not the context belongs to the same vector.
*/
typedef void (*vset_visit_init_context_cb) (void* context, void* parent, int succ);

/**
\brief Post-order visit of a value.

\param val The value.
\param context The user context.
\param cache Whether or not to add a result to the cache.
\param result The result to add to the cache.
*/
typedef void (*vset_visit_post_cb) (int val, void* context, int* cache, void** result);

/**
\brief Function that is called when something has been added to the cache.

\param context The user context.
\param result The data that has been added to the cache.
*/
typedef void (*vset_visit_cache_success_cb) (void* context, void* result);

typedef struct vset_visit_callbacks_s {
    vset_visit_pre_cb vset_visit_pre;
    vset_visit_init_context_cb vset_visit_init_context;
    vset_visit_post_cb vset_visit_post;
    vset_visit_cache_success_cb vset_visit_cache_success;
} vset_visit_callbacks_t;

/**
\brief Visit values in parallel.

\param set The set to run the visitor on.
\param cbs The callback functions that implement the visitor.
\param ctx_size The number of bytes to allocate on the stack for user context.
\param context The user context.
\param cache_op The operation number for operating the cache.

\see vdom_next_cache_op()
\see vdom_clear_cache()
*/
extern void vset_visit_par(vset_t set, vset_visit_callbacks_t* cbs, size_t ctx_size, void* context, int cache_op);

/**
\brief Visit values sequentially.

\param set The set to run the visitor on.
\param cbs The callback functions that implement the visitor.
\param ctx_size The number of bytes to allocate on the stack for user context.
\param context The user context.
\param cache_op The operation number for operating the cache.

\see vdom_next_cache_op()
\see vdom_clear_cache()
*/
extern void vset_visit_seq(vset_t set, vset_visit_callbacks_t* cbs, size_t ctx_size, void* context, int cache_op);

//@}

#endif

