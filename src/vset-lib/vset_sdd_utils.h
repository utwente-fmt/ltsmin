#ifndef SRC_VSET_LIB_VSET_SDD_UTILS_H_
#define SRC_VSET_LIB_VSET_SDD_UTILS_H_

/*
 * vset_sdd_utils.h
 *
 *  Created on: Oct 25, 2019
 *      Author: Lieuwe Vinkhuijzen <l.t.vinkhuijzen@liacs.leidenuniv.nl>
 */


#include <vset-lib/vector_set.h>
#include <vset-lib/vset_sdd_utils.h>
#include <vset-lib/vset_sdd.h>


void vset_set_domain_rel(vset_t dst, vrel_t rel);

void vset_add_to_domain(vset_t dst, vset_t src);

void vset_set_domain(vset_t dst, vset_t src);

unsigned int vset_uses_var(vset_t set, SddLiteral var);

void vset_exposition(vset_t set);

void vrel_exposition(vrel_t rel);

unsigned int vset_domains_are_disjoint(vset_t a, vset_t b);

unsigned int vset_domains_are_equal(vset_t a, vset_t b);

struct vector_relation_ll;
struct vector_set_ll;
typedef struct vector_relation_ll* vrel_ll_t;
typedef struct vector_set_ll* vset_ll_t;

extern vrel_ll_t first_vrel;
extern vrel_ll_t last_vrel;
extern vset_ll_t first_vset;
extern vset_ll_t last_vset;

// A linked list structure for the vrel_t objects
// Necessary due to freak bug in ltsmin (or freak bug in this code?)
struct vector_relation_ll {
	vrel_t rel;
	unsigned int id;
	int r_k;
	int w_k;
	int* r_proj;
	int* w_proj;
	struct vector_relation_ll* next;
};

// A linked list structure for the vset_t objects
// Here we store which variables are currently relevant for this set
struct vector_set_ll {
	vset_t set;
	unsigned int id;
	int k;
	int* proj;
	vset_ll_t next;
};

void add_vrel(vrel_t rel);
void add_vset(vset_t set);
vrel_ll_t get_vrel(unsigned int id);
vset_ll_t get_vset(unsigned int id);

#endif /* SRC_VSET_LIB_VSET_SDD_UTILS_H_ */

