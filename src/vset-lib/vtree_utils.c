/*
 * vtree-utils.c:  Various utilities to Query, navigate and operate on Variable Trees of SDDs
 *
 *  Created on: Oct 25, 2019
 *      Author: lieuwe
 */
#ifndef VTREE_UTILS_C_
#define VTREE_UTILS_C_

#include <vset-lib/vtree_utils.h>

// Every non-leaf vtree node has a unique dissection literal,
// and every literal is the dissection literal of a unique non-leaf vtree,
//   (except the last literal)
SddLiteral vtree_dissection_literal(Vtree* tree) {
//	printf("  Getting unique dissection literal of %li\n", sdd_vtree_position(tree)); fflush(stdout);
	if (sdd_vtree_is_leaf(tree)) {
		return sdd_vtree_var(tree);
	}
	return (sdd_vtree_position(tree) + 1) / 2;
//	return vtree_highest_var_nonprimed(sdd_vtree_left(tree));
}

SddLiteral vtree_highest_var(Vtree* tree) {
	if (tree == 0) return 0;
	else if (sdd_vtree_is_leaf(tree)) {
		SddLiteral v = sdd_vtree_var(tree);
		if (v % 2 == 0) return 0;
		else return (((unsigned int)v)-1) / 2;
	}
	else {
		SddLiteral s, p;
		s = vtree_highest_var(sdd_vtree_left(tree));
		p = vtree_highest_var(sdd_vtree_right(tree));
		return s > p ? s : p;
	}
}

/* Returns the normalised id of the lowest unprimed variable below tree
 */
SddLiteral vtree_lowest_var(Vtree* tree) {
	if (tree == 0) return 0;
	else if (sdd_vtree_is_leaf(tree)) {
		SddLiteral v = sdd_vtree_var(tree);
		if (v % 2 == 0) return -1;
		else return (v-1) / 2;
	}
	else {
		SddLiteral s, p;
		s = vtree_lowest_var(sdd_vtree_left(tree));
		p = vtree_lowest_var(sdd_vtree_right(tree));
		if (s == -1 || p == -1) return (s > p ? s : p);
		return s < p ? s : p;
	}
}

unsigned int vtree_variables_count(Vtree* tree) {
	if (tree == 0) return 0;
	else if (sdd_vtree_is_leaf(tree)) return 1;
	else return vtree_variables_count(sdd_vtree_left(tree)) +
				vtree_variables_count(sdd_vtree_right(tree));
}

// Left => direction = 0    Right => direction = 1
Vtree* sdd_vtree_child(Vtree* v, unsigned int direction) {
	if (v == 0 || sdd_vtree_is_leaf(v)) return 0;
	return direction ? sdd_vtree_right(v) : sdd_vtree_left(v);
}

/* Returns the maximum id of any literal below the tree, in 1, ..., n
 * Does not take into account primed variables
 */
SddLiteral vtree_highest_var_nonprimed(Vtree* tree) {
	if (tree == 0) return 0;
	else if (sdd_vtree_is_leaf(tree)) {
		return sdd_vtree_var(tree);
	}
	else {
		SddLiteral s, p;
		s = vtree_highest_var_nonprimed(sdd_vtree_left (tree));
		p = vtree_highest_var_nonprimed(sdd_vtree_right(tree));
		return s > p ? s : p;
	}
}

/* Returns the minimum id of any literal below the tree, in 1, ..., n
 * Does not take into account primed variables
 */
SddLiteral vtree_lowest_var_nonprimed(Vtree* tree) {
	if (tree == 0) return 0;
	else if (sdd_vtree_is_leaf(tree)) {
		return sdd_vtree_var(tree);
	}
	else {
		SddLiteral s, p;
		s = vtree_lowest_var_nonprimed(sdd_vtree_left (tree));
		p = vtree_lowest_var_nonprimed(sdd_vtree_right(tree));
		if (s == -1 || p == -1) return (s > p ? s : p);
		return s < p ? s : p;
	}
}

Vtree* get_vtree_by_position(Vtree* root, SddLiteral target) {
	if (sdd_vtree_is_leaf(root)) {
		return root;
	}
	SddLiteral position = sdd_vtree_position(root);
	if (position == target) {
		return root;
	} else if (position < target) {
		return get_vtree_by_position(sdd_vtree_right(root), target);
	} else {
		return get_vtree_by_position(sdd_vtree_left(root), target);
	}
}

/* Returns the unique Vtree node with the specified dissection literal */
Vtree* get_vtree_by_dissection_literal(Vtree* root, SddLiteral dissection) {
//	printf("Get tree by dislit %li at node %li\n", dissection, sdd_vtree_position(root)); fflush(stdout);
	if (sdd_vtree_is_leaf(root)) {
		return root; // Base case + error handling
	}
//	printf("Getting dissection literal of root.\n"); fflush(stdout);
	SddLiteral dis = vtree_dissection_literal(root);
	if (dis == dissection) {
//		printf("Found the target tree: %li\n", sdd_vtree_position(root)); fflush(stdout);
		return root;
	}
	if (dis < dissection) {
//		printf("Looking right.\n"); fflush(stdout);
		return get_vtree_by_dissection_literal(sdd_vtree_right(root), dissection);
	} else {
//		printf("Looking left.\n");  fflush(stdout);
		return get_vtree_by_dissection_literal(sdd_vtree_left(root), dissection);
	}
}

// Assumes the literals are sorted left-to-right ascending
Vtree* get_vtree_literal(Vtree* root, SddLiteral target) {
	if (sdd_vtree_is_leaf(root)) {
		if (sdd_vtree_var(root) == target) {
			return root;
		} else {
			return 0;
		}
	}
	SddLiteral dis_lit = vtree_dissection_literal(root);
	if (target <= dis_lit) {
		return get_vtree_literal(sdd_vtree_left (root), target);
	} else {
		return get_vtree_literal(sdd_vtree_right(root), target);
	}
/*
	Vtree* t = get_vtree_literal(sdd_vtree_left(root), target);
	if (t != 0) return t;
	else        return get_vtree_literal(sdd_vtree_right(root), target);
*/
}

unsigned int vtree_vertical_distance(Vtree* parent, Vtree* child) {
	if (!sdd_vtree_is_sub(child, parent) || parent == child) {
		return 0;
	}
	unsigned int distance = 0;
	Vtree* t = parent;
	do {
		if (sdd_vtree_is_sub(child, sdd_vtree_left(t))) {
			t = sdd_vtree_left(t);
		}
		else {
			t = sdd_vtree_right(t);
		}
		distance++;
	} while (t != child);
	return distance;
}

unsigned int vtree_distance(Vtree* a, Vtree* b) {
	unsigned int dist = 0;
	if (a == b) return 0;
	Vtree* sub = 0; Vtree* parent = 0;
	if (sdd_vtree_is_sub(a, b)) {
		parent = b;
		sub = a;
	} else if (sdd_vtree_is_sub(b,a)) {
		parent = a;
		sub = b;
	} else {
		do {
			a = sdd_vtree_parent(a);
			dist++;
		} while (!sdd_vtree_is_sub(b, a));
		parent = a;
		sub = b;
	}
	do {
		sub = sdd_vtree_parent(sub);
		dist++;
	} while (sub != parent);
	return dist;
}

struct sdd_vtree_rotation sdd_vtree_rotate_up(Vtree* v, SddManager* manager) {
	struct sdd_vtree_rotation rotation = {.dislit=0, .direction=0, .manager=manager};
	Vtree* parent = sdd_vtree_parent(v);
	if (parent == 0) return rotation;
	if (sdd_vtree_left(parent) == v) {
		sdd_vtree_rotate_right(parent, manager, 0);
		rotation.direction = right;
		rotation.dislit = vtree_dissection_literal(parent);
	} else {
		sdd_vtree_rotate_left(v, manager, 0);
		rotation.direction = left;
		rotation.dislit = vtree_dissection_literal(v);
	}
	return rotation;
}

struct sdd_vtree_rotation sdd_vtree_rotate_down(Vtree* v, SddManager* manager) {
	struct sdd_vtree_rotation rotation;
	Vtree* parent = sdd_vtree_parent(v);
	if (v == sdd_manager_vtree(manager)) {
		sdd_vtree_rotate_right(v, manager, 0);
		rotation.direction = right;
	} else if (sdd_vtree_left(parent) == v) {
		sdd_vtree_rotate_right(v, manager, 0);
		rotation.direction = right;
	} else {
		sdd_vtree_rotate_left(v, manager, 0);
		rotation.direction = left;
	}
	rotation.dislit = vtree_dissection_literal(v);
	rotation.manager = manager;
	return rotation;
}

// Assumes that the literals in ascending order from left to right
// Assumes that v has a parent
void vtree_make_balanced(Vtree* v, SddManager* manager) {
	if (sdd_vtree_is_leaf(v)) return;
	Vtree* parent = sdd_vtree_parent(v);
	SddLiteral parent_dis = vtree_dissection_literal(parent);
	unsigned int direction = (v == sdd_vtree_right(parent));
	SddLiteral low  = vtree_lowest_var_nonprimed (v);
	SddLiteral high = vtree_highest_var_nonprimed(v);
	SddLiteral middle = low + (high - low) / 2;
	Vtree* intended_root = get_vtree_by_dissection_literal(v, middle);
	while (sdd_vtree_child(parent, direction) != intended_root) {
		sdd_vtree_rotate_up(intended_root, manager);
		intended_root = get_vtree_by_dissection_literal(sdd_manager_vtree(sisyphus), middle);
		parent = get_vtree_by_dissection_literal(sdd_manager_vtree(sisyphus), parent_dis);
	}
	vtree_make_balanced(sdd_vtree_left(intended_root), manager);
	vtree_make_balanced(sdd_vtree_right(intended_root), manager);
}

void vtree_make_right_linear(Vtree* v, SddManager* manager) {
	if (sdd_vtree_is_leaf(v)) return;
	if (sdd_vtree_is_leaf(sdd_vtree_left(v)) && sdd_vtree_is_leaf(sdd_vtree_right(v))) return; // Done
	Vtree* parent = sdd_vtree_parent(v);
	SddLiteral parent_dis = vtree_dissection_literal(parent);
	unsigned int direction = (v == sdd_vtree_right(parent));
	SddLiteral low  = vtree_lowest_var_nonprimed (v);
	Vtree* intended_node = get_vtree_by_dissection_literal(v, low);
	while (sdd_vtree_child(parent, direction) != intended_node) {
		sdd_vtree_rotate_up(intended_node, manager);
		intended_node = get_vtree_by_dissection_literal(sdd_manager_vtree(sisyphus), low);
		parent = get_vtree_by_dissection_literal(sdd_manager_vtree(sisyphus), parent_dis);
	}
	vtree_make_right_linear(sdd_vtree_right(intended_node), manager);
}

void vtree_make_augmented_right_linear(Vtree* v, SddManager* manager) {
	if (sdd_vtree_is_leaf(v)) return;
	if (sdd_vtree_is_leaf(sdd_vtree_left(v)) && sdd_vtree_is_leaf(sdd_vtree_right(v))) return; // Done
	Vtree* parent = sdd_vtree_parent(v);
	SddLiteral parent_dis = vtree_dissection_literal(parent);
	unsigned int direction = (v == sdd_vtree_right(parent));
	SddLiteral low  = vtree_lowest_var_nonprimed (v);
//	SddLiteral high = vtree_highest_var_nonprimed(v);
//	printf("[augmented right] Node %li | %li.\n", low, high); fflush(stdout);
	SddLiteral firstPair = low + 1;
	Vtree* intended_node = get_vtree_by_dissection_literal(v, firstPair);
	while (sdd_vtree_child(parent, direction) != intended_node) {
		sdd_vtree_rotate_up(intended_node, manager);
		intended_node = get_vtree_by_dissection_literal(sdd_manager_vtree(sisyphus), firstPair);
		parent = get_vtree_by_dissection_literal(sdd_manager_vtree(sisyphus), parent_dis);
	}
	vtree_make_augmented_right_linear(sdd_vtree_right(intended_node), manager);
}

void vtree_make_right_balanced(Vtree* v, unsigned int order, SddManager* manager) {
	if (sdd_vtree_is_leaf(v)) return;
	if (sdd_vtree_is_leaf(sdd_vtree_left(v)) && sdd_vtree_is_leaf(sdd_vtree_right(v))) return; // Done
	SddLiteral low  = vtree_lowest_var_nonprimed (v);
	SddLiteral high = vtree_highest_var_nonprimed(v);
//	printf("[augmented right] Node %li | %li.\n", low, high); fflush(stdout);
	SddLiteral firstPair = low + order;
	if (high <= firstPair) return;
	Vtree* parent = sdd_vtree_parent(v);
	SddLiteral parent_dis = vtree_dissection_literal(parent);
	unsigned int direction = (v == sdd_vtree_right(parent));
	Vtree* intended_node = get_vtree_by_dissection_literal(v, firstPair);
	while (sdd_vtree_child(parent, direction) != intended_node) {
		sdd_vtree_rotate_up(intended_node, manager);
		intended_node = get_vtree_by_dissection_literal(sdd_manager_vtree(sisyphus), firstPair);
		parent = get_vtree_by_dissection_literal(sdd_manager_vtree(sisyphus), parent_dis);
	}
//	vtree_make_balanced      (sdd_vtree_left(intended_node));
	vtree_make_augmented_right_linear(sdd_vtree_right(intended_node), manager);
	vtree_make_right_balanced(sdd_vtree_right(intended_node), order, manager);
}

// TODO
void vtree_apply_random_rotation(Vtree* root, SddManager* manager) {
	// Choose an eligible vtree node uniformly at random, and apply rotation upwards, if applicable
	// A vtree node is "eligible" for an upwards rotation if
	//   1) it is not a leaf and
	//   2) not the root
	//   2) its right (left) child is not a leaf
	// Count the eligible nodes
	if(sdd_manager_var_count(manager) <= 2) return;
	unsigned int nEligibleNodes = sdd_manager_var_count(manager) - 2;
	unsigned int targetDissectionLiteral = rand() % nEligibleNodes;
	Vtree* targetNode = get_vtree_by_dissection_literal(root, targetDissectionLiteral);
	if (targetNode == root) {
		// quick fix
		if (!sdd_vtree_is_leaf(sdd_vtree_left(root))) {
			targetNode = sdd_vtree_left(root);
		} else {
			targetNode = sdd_vtree_right(root);
		}
	}
	sdd_vtree_rotate_up(targetNode, manager);
}

void sdd_vtree_apply_rotation(struct sdd_vtree_rotation rot) {
//	printf("[vtree apply rotation] Getting target.\n"); fflush(stdout);
	Vtree* target = get_vtree_by_dissection_literal(sdd_manager_vtree(rot.manager), rot.dislit);
//	printf("[vtree apply rotation] Got target: %lu. Applying rotation.\n", sdd_vtree_position(target)); fflush(stdout);
	switch (rot.direction) {
	case right:
		sdd_vtree_rotate_right(target, rot.manager, 0);
		break;
	case left:
		sdd_vtree_rotate_left(target, rot.manager, 0);
		break;
	}
//	printf("[vtree apply rotation] Applied rotation.\n"); fflush(stdout);
}

void sdd_vtree_undo_rotation(struct sdd_vtree_rotation rot) {
	struct sdd_vtree_rotation inverse = sdd_vtree_rotation_inverse(rot);
	sdd_vtree_apply_rotation(inverse);
}

struct sdd_vtree_rotation sdd_vtree_rotation_inverse(struct sdd_vtree_rotation rot) {
	struct sdd_vtree_rotation inverse = rot;
	switch(rot.direction) {
	case left:
		inverse.direction = right;
		break;
	case right:
		inverse.direction = left;
		break;
	}
//	printf("Inverse: {%lu, %u}\n", sdd_vtree_position(get_vtree_by_dissection_literal(sdd_manager_vtree(inverse.manager), inverse.dislit)), inverse.direction); fflush(stdout);
	return inverse;
}







#endif
