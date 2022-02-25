/*
 * sdd-utils.c
 *
 *  Created on: Oct 25, 2019
 *      Author: lieuwe
 */

#ifndef _SDD_UTILS_C_
#define _SDD_UTILS_C_

#include <vset-lib/sdd_utils.h>
#include <vset-lib/vdom_object.h>
#include <vset-lib/vset_sdd.h>
#include <vset-lib/vset_sdd_utils.h>
#include <assert.h>

unsigned int sdd_literal_value(SddNode* x) {
	if (x == 0) return 0;
	assert(sdd_node_is_literal(x));
    return sdd_node_literal(x) > 0 ? 1 : 0;
}

SddLiteral sdd_literal_index(SddNode* x) {
	if (x == 0) return 0;
    assert(sdd_node_is_literal(x));
    SddLiteral lit = sdd_node_literal(x);
    return lit > 0 ? lit : -lit;
}

unsigned int sdd_literal_unprimed_index(SddNode* x) {
	if (x == 0) return 0;
	if (sdd_node_is_literal(x)) {
		int lit = sdd_node_literal(x);
		lit = lit > 0 ? lit : -lit;
		lit = (lit % 2 == 0) ? lit / 2 : (lit+1) / 2;
		return lit;
	}
	else {
		return 0;
	}
}

// Returns an SDD representing the literal litId (with litId in 1...n)
// TODO refactor to take values in 0... n-1
SddNode* getLiteral(SddLiteral litId, unsigned int value, unsigned int primed) {
	SddLiteral lit = litId > 0 ? litId : -litId;
	lit = primed ? 2*litId : 2*litId - 1;
	lit = value ? lit : -lit;
	return sdd_manager_literal(lit ,sisyphus);
}

/* Returns an SDD representing the conjunction of literals in "literals"
 *   with "literals" a list of (signed) variable indices. Example: With the following values,
 *       literals[0] == -5,
 *       literals[1] ==  6,
 *   we return the SDD (!5 & 6) is returned
 */
SddNode* sdd_getCubeLiterals(int* literals, unsigned int nVars) {
	SddNode* conj = sdd_manager_true(sisyphus);
	for (unsigned int i=0; i<nVars; i++) {
		conj = sdd_conjoin(conj, sdd_manager_literal(literals[i], sisyphus), sisyphus);
	}
	return conj;
}


/* Returns an SDD representing the conjunction of the literals with indices in "vars",
 *   with values in "values". Example:
 *   If vars[0] == 5, values[0] == 0, vars[1] == 6, values[1] == 1, then
 *   the SDD (!5 & 6) is returned.
 */
SddNode* sdd_getCube(int* vars, uint8_t* values, unsigned int nVars) {
//	printf("[Sdd getCube] Making cube of %u vars. Manager has %li vars.\n", nVars, sdd_manager_var_count(sisyphus));
	SddNode* conj = sdd_manager_true(sisyphus);
//	printf("  [Sdd getCube]  Value: ");
	for (unsigned int i=0; i<nVars; i++) {
		//printf("  [SDD getCube] Cubing variable %i = %u.\n", vars[i], values[i]);
/*
		if (i % 16 == 0) {
			printf(" ");
		}
		printf("%u", values[i]);
*/
		conj = sdd_conjoin(conj, sdd_manager_literal(values[i] ? (vars[i] + 1) : -(vars[i] + 1), sisyphus), sisyphus);
	}
	return conj;
}


// in bytes
SddSize sdd_memory_live_footprint() {
	return (18*sizeof(int) * sdd_manager_live_count(sisyphus) + 2*sizeof(int) * sdd_manager_live_size(sisyphus));
}

// in bytes
SddSize sdd_memory_dead_footprint() {
	return (18*sizeof(int) * sdd_manager_dead_count(sisyphus) + 2*sizeof(int) * sdd_manager_dead_size(sisyphus));
}

// Returns an SDD in which all primes have value 0
SddNode* sdd_primes_zero() {
	SddNode* z = sdd_manager_true(sisyphus);
	for (SddLiteral v=2; v<= sdd_manager_var_count(sisyphus); v+=2) {
		z = sdd_conjoin(z, sdd_manager_literal(-v, sisyphus), sisyphus);
	}
	return z;
}




//   ---   SDD Model Iterator



struct sdd_model_iterator {
	//vset_t vset; //obsolete
	SddNode* root;
	//Vtree* vtree; //obsolete
	unsigned int i;
	//int* e; // obsolete
	int finished;

	//unsigned int iprime; // obsolete
	//unsigned int isub;   // obsolete
	//unsigned int primeFinished; // obsolete
	//unsigned int subFinished;   // obsolete
	unsigned int var_is_used;
};

void sdd_mit_skip_false_prime(struct sdd_model_iterator* it) {
	if (it->root != 0 && sdd_node_is_decision(it->root) && it->i < sdd_node_size(it->root)) {
		SddNode* prime = sdd_node_elements(it->root)[2*(it->i)+1];
		if (sdd_node_is_false(prime)) {
			//printf("    [Sdd skip false] Skipping False prime.\n");
			it->i++;
		}
	}
}

/* Version 3.0
 */
void sdd_mit_init_prime(struct sdd_mit_master* mas, Vtree* tree) {
	if (tree == 0 || sdd_vtree_is_leaf(tree)) return; // There is no prime to initialise
	struct sdd_model_iterator* it = &(mas->nodes[sdd_vtree_position(tree)]);

	Vtree* root_tree = (it->root == 0) ? 0 : sdd_vtree_of(it->root);
	Vtree* right_tree = sdd_vtree_right(tree);
	if (it->root == 0) {
		//printf("  [mit init prime] root == 0.\n"); fflush(stdout);
		sdd_get_iterator_rec(mas, 0, right_tree);
	} else if (root_tree == tree) {
		//printf("  [mit init prime] root normalised to this Vtree.\n"); fflush(stdout);
		SddNode* prime = sdd_node_elements(it->root)[2*(it->i)+1];
		if (prime == sdd_manager_true(sisyphus)) {
			sdd_get_iterator_rec(mas, 0, right_tree);
		}
		else {
			sdd_get_iterator_rec(mas, prime, right_tree);
		} // TODO maybe pass True node as prime? Would be cleaner
	} else if (sdd_vtree_is_sub(root_tree, right_tree)) {// Is the root normalised to the left tree?
		//printf("  [mit init prime] root normalised to right Vtree.\n"); fflush(stdout);
		sdd_get_iterator_rec(mas, it->root, right_tree);
	} else {
		//printf("  [mit init prime] root normalised to left, so passing null root to the right.\n");
		sdd_get_iterator_rec(mas, 0, right_tree);
	}
}

/* Version 3.0
 */
void sdd_mit_init_sub(struct sdd_mit_master* mas, Vtree* tree) {
	if (tree == 0 || sdd_vtree_is_leaf(tree)) return; // There is no sub to initialise
	struct sdd_model_iterator* it = &(mas->nodes[sdd_vtree_position(tree)]);
	Vtree* root_tree = (it->root == 0) ? 0 : sdd_vtree_of(it->root);
	Vtree* left_tree = sdd_vtree_left(tree);
	if (it->root == 0) {
		sdd_get_iterator_rec(mas, 0, left_tree);
	} else if (root_tree == tree) {
		sdd_get_iterator_rec(mas, sdd_node_elements(it->root)[2*(it->i)], left_tree);
	} else if (sdd_vtree_is_sub(root_tree, left_tree)) {
		sdd_get_iterator_rec(mas, it->root, left_tree);
	} else {
		sdd_get_iterator_rec(mas, 0, left_tree);
	}
}

/* Version 3.0
 */
void sdd_get_iterator_rec(struct sdd_mit_master* mas, SddNode* root, Vtree* tree) {
	struct sdd_model_iterator* it = &(mas->nodes[sdd_vtree_position(tree)]);
	it->root = root;
	it->i = 0;
	it->finished = 0;
	it->var_is_used = 0;
	sdd_mit_skip_false_prime(it);

	if (sdd_vtree_is_leaf(tree)) {
		SddLiteral var = sdd_vtree_var(tree);
		if (it->root != 0) {
			it->i = sdd_node_literal(it->root) > 0 ? 1 : 0;
			it->var_is_used = 1;
		}
		else {
			it->var_is_used = vset_uses_var(mas->vset, var);
		}
		if (it->var_is_used) {
			mas->e[(var-1)/2] = it->i;
			//printf("    [get iterator]  e[%u] := %u\n", (var-1)/2, it->i);
		}
	}
	else {
		sdd_mit_init_prime(mas, tree);
		sdd_mit_init_sub(mas, tree);
	}
}

struct sdd_mit_master sdd_get_iterator(vset_t set) {
	struct sdd_mit_master mas;
	mas.vset = set;
	mas. e = (int*) malloc(sizeof(int) * set->dom->totalbits);
	for (int i=0; i<set->dom->totalbits; i++) {
		mas.e[i] = 0;
	}
	mas.finished = 0;
	mas.nodes = (struct sdd_model_iterator*) malloc(sizeof(struct sdd_model_iterator) * 2*sdd_manager_var_count(sisyphus));
	sdd_get_iterator_rec(&mas, set->sdd, sdd_manager_vtree(sisyphus));
	return mas;
}

/* Version 3.0
 */
void sdd_mit_free(struct sdd_mit_master mas) {
	free(mas.e);
	free(mas.nodes);
}

/* Version 3.0 Next model
 * Algorithm description:
	 * In case of non-leaf:
	 * Try to get another prime model
	 *   If that succeeds, exit.
	 * Otherwise, try to get another sub model
	 *   If that succeeds, reset the prime and exit.
	 *   Otherwise, go to the next element of this decision node, it->i++
	 *   If prime == False, then go to the next element again, it->i++
	 *   If this element exists, then
	 *     Set the sub, prime accordingly, again.
	 *   Otherwise, indicate it->finished = 1 that there are no more models here.
 */
void sdd_next_model_rec(struct sdd_mit_master* mas, Vtree* tree) {
	SddLiteral treeid = sdd_vtree_position(tree);
	struct sdd_model_iterator* it = &(mas->nodes[treeid]);
	if (it->finished == 1) return;
	if (sdd_vtree_is_leaf(tree)) {
		SddLiteral var_sdd = sdd_vtree_var(tree);
		SddLiteral var = (var_sdd - 1) / 2;
		if (it->root != 0) {
			//printf("  [Sdd next model]  Leaf (var %li) is fixed\n", var);
			it->finished = 1;
		}
		else if (var_sdd % 2 == 0) {
			//printf("  [Sdd next model]  Leaf var %li prime\n", var);
			it->finished = 1;
		}
		else if (it->var_is_used && it->i == 0) {
			//printf("  [Sdd next model]  Leaf;  e[%lu] := 1\n", var); fflush(stdout);
			it->i++;
			mas->e[var] = 1;
		}
		else {
			//printf("  [Sdd next model]  Leaf (var %li) finished\n", sdd_vtree_var(tree));fflush(stdout);
			it->finished = 1;
		}
		return;
	}
/*
	SddLiteral tree_hi = vtree_highest_var(tree);
	SddLiteral tree_lo = vtree_lowest_var(tree);
	if (it->root != 0 && sdd_node_is_decision(it->root) && sdd_vtree_of(it->root) == tree) {
		//printf("  [Sdd next model] Node %li|%li  :  %u / %u\n", tree_lo, tree_hi, it->i, sdd_node_size(it->root));
	}
	else {
		//printf("  [Sdd next model] Node %li|%li\n", tree_lo, tree_hi);
	}
*/
	sdd_next_model_rec(mas, sdd_vtree_right(tree));
	SddLiteral primeid = sdd_vtree_position(sdd_vtree_right(tree));
	if (mas->nodes[primeid].finished == 1) {
		//printf("  [Sdd next model]  Node %li|%li's prime is finished. Trying next sub model.\n", tree_lo, tree_hi);
		SddLiteral subid   = sdd_vtree_position(sdd_vtree_left(tree));
		sdd_next_model_rec(mas, sdd_vtree_left(tree));
		if (mas->nodes[subid].finished == 0) {
			//printf("  [Sdd next model]  Node %li|%li's sub worked. restarting prime.\n", tree_lo, tree_hi); fflush(stdout);
//			sdd_model_restart(mas, sdd_vtree_right(tree));
			sdd_get_iterator_rec(mas, mas->nodes[primeid].root, sdd_vtree_right(tree));
		}
		else if (it->root != 0 && sdd_vtree_of(it->root) == tree) {
			it->i++;
			sdd_mit_skip_false_prime(it);
			//printf("  [Sdd next model]  Node %li|%li's sub oom. Starting next sub: %u / %u.\n", tree_lo, tree_hi, it->i, sdd_node_size(it->root)); fflush(stdout);
			if (it->i < sdd_node_size(it->root)) {
				//printf("  [Sdd next model]  Node %li|%li Moved to next element.\n", tree_lo, tree_hi); fflush(stdout);
				sdd_mit_init_prime(mas, tree);
				//printf("  [Sdd next model]  Node %li|%li Initialised prime.\n", tree_lo, tree_hi); fflush(stdout);
				sdd_mit_init_sub(mas, tree);
				//printf("  [Sdd next model]  Node %li|%li Initialised sub.\n", tree_lo, tree_hi); fflush(stdout);
			}
			else {
				//printf("  [Sdd next model]  Node %li|%li's sub oom. That was the last sub. Finished.\n", tree_lo, tree_hi); fflush(stdout);
				it->finished = 1;
			}
		}
		else {
			//printf("  [Sdd next model]  Node %li|%li is not decision, and oom. Finished.\n", tree_lo, tree_hi);
			it->finished = 1;
		}
	}
}

/* Version 3.0
 */
void sdd_next_model(struct sdd_mit_master* mas) {
	clock_t before = clock();
	sdd_next_model_rec(mas, sdd_manager_vtree(sisyphus));
	mas->finished = mas->nodes[sdd_vtree_position(sdd_manager_vtree(sisyphus))].finished;
	sdd_enumerate_time += (double)(clock() - before);
}



//   ---   Function decomposition



SddNode* sdd_get_sub(SddNode* S, unsigned int i) {
	return sdd_node_elements(S)[2*i];
}

SddNode* sdd_get_prime(SddNode* S, unsigned int i) {
	return sdd_node_elements(S)[2*i+1];
}

// returns the first sub of s whose prime is not True and not False
SddNode* sdd_get_first_nontrivial_sub(SddNode* s) {
	SddNode* first_prime = sdd_get_prime(s,0);
	if (sdd_node_is_true(first_prime) || sdd_node_is_false(first_prime)) {
		return sdd_node_elements(s)[2];
	}
	return sdd_node_elements(s)[0];
}

SddNode* sdd_get_first_nontrivial_prime(SddNode* s) {
	SddNode* first_prime = sdd_get_prime(s,0);
	if (sdd_node_is_true(first_prime) || sdd_node_is_false(first_prime)) {
		return sdd_node_elements(s)[3];
	}
	return sdd_node_elements(s)[1];
}

// TODO
// Returns 1 iff a is the negation of b
unsigned int sdd_node_is_negation_of(SddNode* a, SddNode* b) {
	SddNode* suppress_warning = a;
	suppress_warning = b;
	a = suppress_warning;
	return 0;
}

unsigned int sdd_node_is_and(SddNode* s) {
	return sdd_node_is_decision(s) && sdd_node_size(s) == 2 &&
		   (sdd_node_is_false(sdd_node_elements(s)[1]) ||
				sdd_node_is_false(sdd_node_elements(s)[3]));
}

unsigned int sdd_node_is_or(SddNode* s) {
	return sdd_node_is_decision(s) && sdd_node_size(s) == 2 &&
		   (sdd_node_is_true(sdd_node_elements(s)[1]) ||
				sdd_node_is_true(sdd_node_elements(s)[3]));
}

// TODO refactor this function to use the sdd_node_is_negation_of function
//   This way manager does not have to be passed, and
//   No new SDD nodes need be created;
//   current implementation creates garbage SDD nodes.
unsigned int sdd_node_is_equals(SddNode* s, SddManager* manager) {
	return sdd_node_is_decision(s) && sdd_node_size(s) == 2 &&
			sdd_node_elements(s)[1] == sdd_negate(sdd_node_elements(s)[3], manager);
}

fn_arity_2 sdd_node_get_type(SddNode* s, SddManager* manager) {
	if (sdd_node_is_and(s)) return fn_and;
	if (sdd_node_is_or (s)) return fn_or;
	if (sdd_node_is_equals(s, manager)) return fn_equals;
	return fn_none;
}

fn_arity_2 sdd_node_is_composite(SddNode* s, SddManager* manager) {
	if (!sdd_node_is_decision(s)) return fn_none;
	// Check that all subs are decision with size 2
	// Check that all primes are decision with size 2
	SddNode* sub; SddNode* prime;
	fn_arity_2 t = sdd_node_get_type(sdd_node_elements(s)[1], manager); // get type of first prime
	if (t == fn_none) return fn_none;

	for (unsigned int i=0; i<sdd_node_size(s); i++) {
		sub = sdd_get_sub(s, i);
		prime = sdd_get_prime(s, i);
		if (!sdd_node_is_and(sub)) return fn_none;
		if (sdd_node_get_type(prime, manager) != t) return fn_none;
	}
	// Gather the sub's sub/prime partition
	// By collecting elements whose subs have the same prime
	// Likewise collect the elements whose subs have the same sub
	// check if the partition is consistent
	
	// Compute the number of elements of g
	SddNode* sub1 = sdd_get_first_nontrivial_sub(sdd_get_sub(s,0));
	unsigned int g = 0;
	for (unsigned int i=1; i<sdd_node_size(s); i++) {
		if (sdd_get_first_nontrivial_sub(sdd_get_sub(s,i)) == sub1) {
			g++;
		}
	}
	unsigned int h = sdd_node_size(s) / g;
	if (g * h != sdd_node_size(s)) return fn_none;
	

	return 0;
}

#endif
