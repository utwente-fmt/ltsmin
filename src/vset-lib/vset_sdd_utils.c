/*
 * vset_sdd_utils.c
 *
 *  Created on: Oct 25, 2019
 *      Author: Lieuwe Vinkhuijzen <l.t.vinkhuijzen@liacs.leidenuniv.nl>
 */

#include <stdlib.h>
#include <string.h>

//#include <hre/user.h>
//#include <hre-io/user.h>
#include <vset-lib/vector_set.h>
#include <vset-lib/vset_sdd_utils.h>
#include <vset-lib/vset_sdd.h>


vrel_ll_t first_vrel = 0;
vrel_ll_t last_vrel = 0;
vset_ll_t first_vset = 0;
vset_ll_t last_vset = 0;

void vset_set_domain_rel(vset_t dst, vrel_t rel) {
	vrel_ll_t rel_ll = get_vrel(rel->id);
	vset_ll_t set_ll = get_vset(dst->id);
	set_ll->k = rel_ll->w_k;
	if (rel_ll->w_k != -1) {
		memcpy(set_ll->proj, rel_ll->w_proj, sizeof(int) * rel_ll->w_k);
	}
}

// Adds the variables from the domain of src to the domain in dst
void vset_add_to_domain(vset_t dst, vset_t src) {
	vset_ll_t dst_ll = get_vset(dst->id);
	vset_ll_t src_ll = get_vset(src->id);
	if (dst_ll->k == -1) return;
	if (src_ll->k == -1) {
		dst_ll->k = -1;
		return;
	}
	int* proj_union = malloc(sizeof(int) * src->dom->vectorsize);
	int k_union = 0;
	int i_dst = 0;
	int i_src = 0;
	int i_union = 0;
	while (i_dst < dst_ll->k && i_src < src_ll->k) {
		if (dst_ll->proj[i_dst] == src_ll->proj[i_src]) {
			proj_union[i_union] = dst_ll->proj[i_dst];
			i_dst++;
			i_src++;
		}
		else if (dst_ll->proj[i_dst] <= src_ll->proj[i_src]) {
			proj_union[i_union] = dst_ll->proj[i_dst];
			i_dst++;
		}
		else {
			proj_union[i_union] = src_ll->proj[i_src];
			i_src++;
		}
		i_union++;
	}
	k_union = i_union;
	if (k_union == src->dom->vectorsize) {
		k_union = -1;
	}
	else {
		memcpy(dst_ll->proj, proj_union, sizeof(int) * src->dom->vectorsize);
	}
	dst_ll->k = k_union;
	free(proj_union);
}

void vset_set_domain(vset_t dst, vset_t src) {
	vset_ll_t dst_ll = get_vset(dst->id);
	vset_ll_t src_ll = get_vset(src->id);
	dst_ll->k = src_ll->k;
	if (src_ll->k != -1) {
		memcpy(dst_ll->proj, src_ll->proj, sizeof(int) * src_ll->k);
	}
}

unsigned int vset_uses_var(vset_t set, SddLiteral var) {
	if (var % 2 == 0) return 0;
	vset_ll_t vset_ll = get_vset(set->id);
	if (vset_ll-> k == -1) {
		return 1;
	}
	unsigned int var_norm = (var - 1) / 2;
	for (int i=0; i<vset_ll->k; i++) {
		if (var_norm >= (unsigned int) xstatebits * vset_ll->proj[i] && var_norm < (unsigned int) xstatebits * (vset_ll->proj[i]+1)) {
			return 1;
		}
	}
	return 0;
}

void vset_exposition(vset_t set) {
	//printf("  [Set exposition] set %u = ", set->id); small_enum(set); printf("\n");
	//printf("    [Set exposition] set @%p\n", set->sdd);
	//printf("    [Set exposition] Models: %llu\n", set_count_exact(set));
	vset_ll_t set_ll = get_vset(set->id);
//	Printf(info, "    [Set exposition] k=%i:  {", set_ll->k);
	for (int i=0; i<set_ll->k; i++) {
//		Printf(info, " %i", set_ll->proj[i]);
	}
//	Printf(info, "}\n");
}

// printf the values of the relation
void vrel_exposition(vrel_t rel) {
	vrel_ll_t rel_ll = get_vrel(rel->id);
	SddModelCount mc = sdd_model_count(rel->sdd, sisyphus);
//	printf("  [Rel exposition] rel %u |.|=%llu ", rel->id, mc);
//	printf("  r_proj = {");
	if (rel_ll->r_k == -1) {
//		printf(" (all) ");
	}
	else {
		for (int v=0; v<rel_ll->r_k; v++) {
//			printf(" %u", rel_ll->r_proj[v]);
		}
	}
//	printf(" }  w_proj = {");
	if (rel_ll->w_k == -1) {
//		printf(" (all) ");
	}
	else {
		for (int v=0; v<rel_ll->w_k; v++) {
//			printf(" %u", rel_ll->w_proj[v]);
		}
	}
//	printf(" }\n");
}

// Returns whether the sets a,b are defined over disjoint sets of variables
unsigned int vset_domains_are_disjoint(vset_t a, vset_t b) {
	vset_ll_t a_ll = get_vset(a->id);
	vset_ll_t b_ll = get_vset(b->id);
	if (a_ll->k == -1 || b_ll->k == -1) {
		return 0;
	}
	// See if they have a common variable
	for (int v=0; v<a_ll->k; v++) {
		for (int w=0; w<b_ll->k; w++) {
			if (a_ll->proj[v] == b_ll->proj[w]) {
				//printf("  [vset domains disjoint] Sets %u and %u share variable %i\n", a->id, b->id, a_ll->proj[v]);
				return 0;
			}
		}
	}
	return 1;
}

unsigned int vset_domains_are_equal(vset_t a, vset_t b) {
	if (a->k != b->k) return 0;
	if (a->k == -1) return 1;
	for (int i=0; i<a->k; i++) {
		if (a->proj[i] != b->proj[i]) return 0;
	}
	return 1;
}


void add_vrel(vrel_t rel) {
	if (first_vrel == 0) {
		first_vrel = malloc(sizeof(struct vector_relation_ll));
		last_vrel = first_vrel;
	}
	else {
		vrel_ll_t previous_last_vrel = last_vrel;
		last_vrel = malloc(sizeof(struct vector_relation_ll));
		previous_last_vrel->next = last_vrel;
	}
	last_vrel->rel = rel;
	last_vrel->id = rel->id;
	last_vrel->r_k = rel->r_k;
	last_vrel->w_k = rel->w_k;
	last_vrel->r_proj = rel->r_proj;
	last_vrel->w_proj = rel->w_proj;
	last_vrel->next = 0;
}

void add_vset(vset_t set) {
	if (first_vset == 0) {
		first_vset = malloc(sizeof(struct vector_set_ll));
		last_vset = first_vset;
	}
	else {
		vset_ll_t previous_last_vset = last_vset;
		last_vset = malloc(sizeof(struct vector_set_ll));
		previous_last_vset->next = last_vset;
	}
	last_vset->set = set;
	last_vset->id = set->id;
	last_vset->k = set->k;
	last_vset->proj = malloc(sizeof(int)*set->dom->vectorsize);
	for (int i=0; i<set->k; i++) {
		last_vset->proj[i] = set->proj[i];
	}
	last_vset->next = 0;
}

// Returns the linked-list-relation struct with the given id.
// If the id is not present, returns the last relation in the linked list
vrel_ll_t get_vrel(unsigned int id) {
	if (first_vrel == 0) return (vrel_ll_t) 0;
	vrel_ll_t rel = first_vrel;
	while (rel->id != id && rel->next != 0) {
		rel = rel->next;
	}
	return rel;
}

vset_ll_t get_vset(unsigned int id) {
	if (first_vset == 0) return (vset_ll_t) 0;
	//printf("  [get vset] Start looking for %u:  ", id);
	vset_ll_t set = first_vset;
	while (set->id != id && set->next != 0) {
		//printf(" %u", set->id);
		set = set->next;
	}
	//printf(" Found %u.\n", set->id);
	return set;
}
