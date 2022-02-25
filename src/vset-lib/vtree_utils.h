/*
 * vtree-utils.h
 *
 *  Created on: Oct 25, 2019
 *      Author: Lieuwe Vinkhuijzen <l.t.vinkhuijzen@liacs.leidenuniv.nl>
 */

#ifndef SRC_VSET_LIB_VTREE_UTILS_H_
#define SRC_VSET_LIB_VTREE_UTILS_H_

#include "sddapi.h"

#include <vset-lib/vset_sdd.h>


typedef enum {
	right,
	left
} sdd_vtree_rotation_direction;

// A rotation upwards
struct sdd_vtree_rotation {
	// the node is determined by its dissection literal
	SddLiteral dislit;
	sdd_vtree_rotation_direction direction;
	SddManager* manager;
};

SddLiteral vtree_dissection_literal(Vtree* tree);

SddLiteral vtree_highest_var(Vtree* tree);

SddLiteral vtree_lowest_var(Vtree* tree);

unsigned int vtree_variables_count(Vtree* tree);

unsigned int vtree_distance(Vtree* a, Vtree* b);

Vtree* sdd_vtree_child(Vtree* v, unsigned int direction);

SddLiteral vtree_highest_var_nonprimed(Vtree* tree);

SddLiteral vtree_lowest_var_nonprimed(Vtree* tree);

Vtree* get_vtree_by_position(Vtree* root, SddLiteral target);

Vtree* get_vtree_by_dissection_literal(Vtree* root, SddLiteral dissection);

Vtree* get_vtree_literal(Vtree* root, SddLiteral target);

unsigned int vtree_vertical_distance(Vtree* parent, Vtree* child);

struct sdd_vtree_rotation sdd_vtree_rotate_up(Vtree* v, SddManager* manager);

struct sdd_vtree_rotation sdd_vtree_rotate_down(Vtree* v, SddManager* manager);

void vtree_make_balanced(Vtree* v, SddManager* manager);

void vtree_make_right_linear(Vtree* v, SddManager* manager);

void vtree_make_augmented_right_linear(Vtree* v, SddManager* manager);

void vtree_make_right_balanced(Vtree* v, unsigned int order, SddManager* manager);

void vtree_apply_random_rotation(Vtree* root, SddManager* manager);

void sdd_vtree_apply_rotation(struct sdd_vtree_rotation);
void sdd_vtree_undo_rotation(struct sdd_vtree_rotation);

struct sdd_vtree_rotation sdd_vtree_rotation_inverse(struct sdd_vtree_rotation);

#endif /* SRC_VSET_LIB_VTREE_UTILS_H_ */
