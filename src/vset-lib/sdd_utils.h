/*
 * sdd_utils.h
 *
 *  Created on: Oct 25, 2019
 *      Author: lieuwe
 */

#ifndef SRC_VSET_LIB_SDD_UTILS_H_
#define SRC_VSET_LIB_SDD_UTILS_H_

#include <inttypes.h>
#include "sddapi.h"

#include <vset-lib/vector_set.h>


unsigned int sdd_literal_value(SddNode* x);
SddLiteral sdd_literal_index(SddNode* x);
SddNode* getLiteral(SddLiteral litId, unsigned int value, unsigned int primed);
SddNode* sdd_getCubeLiterals(int* literals, unsigned int nVars);
SddNode* sdd_getCube(int* vars, uint8_t* values, unsigned int nVars);

SddSize sdd_memory_live_footprint();
SddSize sdd_memory_dead_footprint();
SddNode* sdd_primes_zero();

//   ---   SDD Model Iterator


/*
 * Version 2, each node is normalised to a vtree node.
 *  We do look at the corresponding vset.
 *
 * iprime is for when the prime is the True node. In that case, iPrime goes from 0 to 2^m-1
 *   for some appropriate m
 *  Invariants:
 *  +  prime is not False
 */


// TODO none of the functions need mas by reference except sdd_next_model
struct sdd_mit_master {
	vset_t vset;
	struct sdd_model_iterator* nodes;
	int* e;
	unsigned int finished;
};

void sdd_next_model(struct sdd_mit_master* mas);



//   ---   Decompositionality utilities

typedef enum {
	fn_none,
	fn_and,
	fn_or,
	fn_equals
} fn_arity_2;


unsigned int sdd_node_is_and(SddNode*);
unsigned int sdd_node_is_or(SddNode*);
unsigned int sdd_node_is_equals(SddNode*, SddManager*);
fn_arity_2 sdd_node_get_type(SddNode*, SddManager*);
fn_arity_2 sdd_node_is_composite(SddNode*, SddManager*);
#endif /* SRC_VSET_LIB_SDD_UTILS_H_ */
