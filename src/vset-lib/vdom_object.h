#ifndef VDOM_OBJECT_H
#define VDOM_OBJECT_H


#include <vset-lib/vector_set.h>

/** \file vdom_object.h
Object structure and helper functions for vector sets.
*/

struct vector_domain_shared {
	int size;
	vset_t (*set_create)(vdom_t dom,int k,int* proj);
	void (*set_save)(FILE* f, vset_t set);
	vset_t (*set_load)(FILE* f, vdom_t dom);
	void  (*set_add)(vset_t set,const int* e);
    void (*set_update)(vset_t dst, vset_t set, vset_update_cb cb, void *context);
	int (*set_member)(vset_t set,const int* e);
	int (*set_equal)(vset_t set1,vset_t set2);
	int (*set_is_empty)(vset_t set);
	void (*set_clear)(vset_t set);
	void (*set_enum)(vset_t set,vset_element_cb cb,void* context);
	void (*set_enum_match)(vset_t set,int p_len,int* proj,int*match,vset_element_cb cb,void* context);
	void (*set_copy_match)(vset_t src,vset_t dst,int p_len,int* proj,int*match);
	void (*set_copy_match_proj)(vset_t src,vset_t dst,int p_len,int* proj,int p_id,int*match);
	int (*proj_create)(int p_len,int* proj);
	void (*set_example)(vset_t set,int *e);
	void (*set_example_match)(vset_t set,int *e, int p_len, int* proj, int*match);
    void (*set_random)(vset_t set,int *e);
	void (*set_copy)(vset_t dst,vset_t src);
	void (*set_project)(vset_t dst,vset_t src);
    void (*set_project_minus)(vset_t dst,vset_t src,vset_t minus);
	void (*set_union)(vset_t dst,vset_t src);
	void (*set_intersect)(vset_t dst, vset_t src);
	void (*set_minus)(vset_t dst,vset_t src);
	void (*set_zip)(vset_t dst,vset_t src);
	void (*set_count)(vset_t set,long *nodes,double *elements);
	void (*set_count_precise)(vset_t set,long nodes,bn_int_t *elements);
	void (*set_ccount)(vset_t set,long *nodes,long double *elements);
	void (*dom_clear_cache)(vdom_t dom, const int cache_op);
	void (*set_visit_seq)(vset_t set, vset_visit_callbacks_t* cbs, size_t ctx_size, void* context, int cache_op);
	void (*set_visit_par)(vset_t set, vset_visit_callbacks_t* cbs, size_t ctx_size, void* context, int cache_op);
	void (*rel_count)(vrel_t rel,long *nodes,double *elements);
	vrel_t (*rel_create)(vdom_t dom,int k,int* proj);
    vrel_t (*rel_create_rw)(vdom_t dom,int r_k,int* r_proj,int w_k,int* w_proj);
	void (*rel_save_proj)(FILE* f, vrel_t rel);
	void (*rel_save)(FILE* f, vrel_t rel);
	vrel_t (*rel_load_proj)(FILE* f, vdom_t dom);
    void (*rel_load)(FILE* f, vrel_t rel);
	void (*rel_add)(vrel_t rel,const int* src,const int* dst);
    void (*rel_add_cpy)(vrel_t rel,const int* src,const int* dst,const int* cpy);
	void (*rel_add_act)(vrel_t rel,const int* src,const int* dst,const int* cpy,const int act);
    void (*rel_update)(vrel_t rel, vset_t set, vrel_update_cb cb, void *context);
    void (*rel_destroy)(vrel_t rel);

	void (*set_next)(vset_t dst,vset_t src,vrel_t rel);
	void (*set_next_union)(vset_t dst,vset_t src,vrel_t rel,vset_t uni);
	void (*set_prev)(vset_t dst,vset_t src,vrel_t rel,vset_t univ);
	void (*set_universe)(vset_t dst, vset_t src);
	void (*reorder)();
	void (*set_destroy)(vset_t set);
	void (*set_least_fixpoint)(vset_t dst,vset_t src,vrel_t rels[],int rel_count);
	void (*set_dot)(FILE* fp, vset_t src);
	void (*rel_dot)(FILE* fp, vrel_t src);
	void (*set_join)(vset_t dst,vset_t left,vset_t right);

    // Hooks called before/after all save/load operations on f
    void (*pre_save)(FILE* f, vdom_t dom);
    void (*pre_load)(FILE* f, vdom_t dom);
    void (*post_save)(FILE* f, vdom_t dom);
    void (*post_load)(FILE* f, vdom_t dom);

    void (*dom_save)(FILE *f, vdom_t dom);
    // creating a domain from a saved dom: vdom_create_domain_from_file

	int (*separates_rw)();
	int (*supports_precise_counting)();
	char **names;

        int (*dom_next_cache_op)(vdom_t dom);
};

/** Initialise the shared part of the domain. */
extern void vdom_init_shared(vdom_t dom,int n);

extern int vdom_next_cache_op(vdom_t dom);

extern void vdom_clear_cache(vdom_t dom, const int cache_op);

#endif

