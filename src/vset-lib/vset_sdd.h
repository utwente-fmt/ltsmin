/*
 * vset_sdd.h
 *
 *  Created on: Jun 20, 2019
 *      Author: Lieuwe Vinkhuijzen <l.t.vinkhuijzen@liacs.leidenuniv.nl
 */

#ifndef SRC_VSET_LIB_VSET_SDD_H_
#define SRC_VSET_LIB_VSET_SDD_H_

#include <vset-lib/sdd_utils.h>
#include <vset-lib/vdom_object.h>
#include <vset-lib/vtree_utils.h>

#include "sddapi.h"


struct vector_domain
{
    struct vector_domain_shared shared;

    int vectorsize;             // number of integers for each full state
    int *statebits;             // number of bits for each state variable
    int actionbits;             // number of bits for the action label

    /* The following are automatically generated from the above */

    int totalbits;              // number of bits for each full state
    int* state_variables;       // array of variable indices, representing the set of variables of the set
    int* prime_variables;       // array of variable indices, representing the set of primed variables
};

struct vector_set {
	vdom_t dom;

	unsigned int n; // Placeholder variable for testing, Replace with SDD
	SddNode* sdd;

	int k;         // number of variables in this set, or -1 if the set contains all state variables
	               //   (is this the number of variables on which this set depends?)
	int* proj;     // array of indices of program variables used by this set

	int* state_variables;  // array of variable indices of bits, used to represent the variables of proj
	                       //   The arrays state_variables and proj may be different when a program variable is an integer.
	                       //   In that case, state_variables will contain xstatebits (=16) different variables to represent this integer
	unsigned int nstate_variables; // length of state_variables array

	unsigned int id;       // unique ID of this set
};

struct vector_relation {
	vdom_t dom;

	int r_k;     // number of read variables
	int w_k;      // number of write variables
	int* r_proj; // array  of read variable indices
	int* w_proj; // array of write variable indices

	int* state_variables; // array of bit-variable indices, the unprimed variables
	int* prime_variables; // array of bit-variable indices, the primed variables

	unsigned int id;      // unique ID of this relation

	SddNode* sdd;
};

extern double exists_time;  // Amount of time used by sdd_exists (Existential Quantification)
extern double union_time;  // Amount of time used by sdd_disjoin
extern double conjoin_time;  // Amount of time used by sdd_conjoin
extern double debug_time;  // Amount of time spent on safety checks and sanity checks
extern double rel_update_time; // Amount of time spent on rel_update and model enumeration
extern double rel_increment_time; // Amount of time spent, within rel_update, on adding a single model to rel
extern double sdd_enumerate_time; // Amount of time spent enumerating models with SDD
extern int xstatebits;  // bits per integer



extern SddManager* sisyphus;

void sdd_set_vtree_value_rec(const Vtree* tree, const unsigned int value, int* e, const unsigned int varsDone);

// Takes a tree node with <= 32 variables, and a 32-bit integer v
// Sets the e[var] = v[var] for every variable in the vtree
void sdd_set_vtree_value(const Vtree* tree, unsigned int v, int* e);

void sdd_get_iterator_rec(struct sdd_mit_master*, SddNode*, Vtree*);

struct sdd_mit_master sdd_get_iterator(vset_t);

void sdd_model_restart(struct sdd_mit_master*, Vtree*);

void sdd_mit_free_old(struct sdd_model_iterator* it);

void sdd_mit_free(struct sdd_mit_master);

/* Increments to the next model, if possible.
 * Otherwise, sets   it->finished = 1
 */
void sdd_next_model(struct sdd_mit_master*);

void small_enum(vset_t src);

void set_add(vset_t src, const int* e);

#endif /* SRC_VSET_LIB_VSET_SDD_H_ */
