#ifndef VDOM_OBJECT_H
#define VDOM_OBJECT_H


#include <vector_set.h>

/** \file vdom_object.h
Object structure and helper functions for vector sets.
*/


struct vector_domain_shared {
	int size;
	vset_t (*set_create)(vdom_t dom,int k,int* proj);
	void  (*set_add)(vset_t set,const int* e);
	int (*set_member)(vset_t set,const int* e);
	int (*set_equal)(vset_t set1,vset_t set2);
	int (*set_is_empty)(vset_t set);
	void (*set_clear)(vset_t set);
	void (*set_enum)(vset_t set,vset_element_cb cb,void* context);
	void (*set_enum_match)(vset_t set,int p_len,int* proj,int*match,vset_element_cb cb,void* context);
	void (*set_copy_match)(vset_t src,vset_t dst,int p_len,int* proj,int*match);
	void (*set_example)(vset_t set,int *e);
	void (*set_copy)(vset_t dst,vset_t src);
	void (*set_project)(vset_t dst,vset_t src);
	void (*set_union)(vset_t dst,vset_t src);
	void (*set_minus)(vset_t dst,vset_t src);
	void (*set_zip)(vset_t dst,vset_t src);
	void (*set_count)(vset_t set,long *nodes,bn_int_t *elements);
	void (*rel_count)(vrel_t rel,long *nodes,bn_int_t *elements);
	vrel_t (*rel_create)(vdom_t dom,int k,int* proj);
	void (*rel_add)(vrel_t rel,const int* src,const int* dst);
	void (*set_next)(vset_t dst,vset_t src,vrel_t rel);
	void (*set_prev)(vset_t dst,vset_t src,vrel_t rel);
  void (*reorder)();
};

/** Initialise the shared part of the domain. */
extern void vdom_init_shared(vdom_t dom,int n);


#endif

