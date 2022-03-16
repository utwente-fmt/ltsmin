#include <config.h>

/* SDD API for the LTSmin library
 *
 * Author: Lieuwe Vinkhuijzen <l.t.vinkhuijzen@liacs.leidenuniv.nl>
 *
 * Keep in mind that this library numbers its variables from 0...n-1,
 *   whereas SDDs number their variables 1...n
 *   This conversion is "hidden" from the calling LTSmin library
 *
 */

#include <alloca.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <hre/user.h>
#include <hre-io/user.h>
#include <vset-lib/vdom_object.h>
#include <vset-lib/sdd_utils.h>
#include <vset-lib/vtree_utils.h>
#include <vset-lib/vset_sdd_utils.h>
#include <vset-lib/vset_sdd.h>


//   ---   Time profile
double exists_time        = 0;  // Amount of time used by sdd_exists (Existential Quantification)
double union_time         = 0;  // Amount of time used by sdd_disjoin
double conjoin_time       = 0;  // Amount of time used by sdd_conjoin
double count_time         = 0;  // Amount of time used by sdd_model_count
double debug_time         = 0;  // Amount of time spent on safety checks and sanity checks
double reference_time     = 0;  // Amount of time spent on referencing and dereferencing SDD nodes
double vtree_setup_time   = 0;  // Amount of time spent on static vtree search, i.e., at the beginning of the program
double vtree_search_time  = 0;  // Amount of time spent on dynamic vtree search, i.e., during execution
double rel_update_time    = 0;  // Amount of time spent on rel_update and model enumeration
double rel_increment_time = 0;  // Amount of time spent, within rel_update, on adding a single model to rel
double sdd_enumerate_time = 0;  // Amount of time spent enumerating models with SDD
double nscb_time          = 0;  // Amount of time spent on Next State callbacks
unsigned int nNextState_cb= 0;  // Number of nextState callbacks performed TODO keep track in rel_update
clock_t clock_before_nscb;      // Clock time at time of callback; global variable because we detect a callback "between functions"
int xstatebits = 16;  // bits per integer


//   ---   Command line options
static int static_vtree_search = 2;    // Default: Search for a static vtree before execution, starting from right-linear
static int vtree_integer_config = 1;   // Default: Augmented right-linear
static int vtree_increment_config = 8; // Default: Use cache, traverse literals backward, integer by integer
static int sdd_exist_config = 0;       // Default: Make no attempt to improve existential quantification
static int sdd_separate_rw = 1;        // Default: Separate read-write-copy dependencies
static int vtree_penalty_fn = 3;       // Default: sum Vtree-distances, penalizing write->read dependencies
static int dynamic_vtree_search = 0;   // Default: No dynamic Vtree minimization
static int sdd_verbose = 0;            // Default: not verbose, no comments

struct poptOption sdd_options[] = {
	{ "vtree-search-init", 0, POPT_ARG_INT, &static_vtree_search, 0, " The initial vtree when searching heuristically: 0 (right-linear) 1 (balanced tree, but each integer's variables are grouped underneath one common ancestor) 2 (like 1, but right-linear) 3 (like 1, but left-linear).", "<0|1|2|3>"},
	{ "vtree-integer", 0, POPT_ARG_INT, &vtree_integer_config, 0, "Variable Tree corresponding to integers. 0 (balanced) 1 (augmened right-linear) 2 (right-linear) 3 (intermediate wrt balanced / RL)", "<0|1|2>"},
	{ "vtree-increment", 0, POPT_ARG_INT, &vtree_increment_config, 0, "Way to add a relation element: 0 (naive) 1 (batch) 2 (variables left-to-right) 3 (faithful to Vtree)", "<0|1|2|3>"},
	{ "sdd-exist", 0, POPT_ARG_INT, &sdd_exist_config, 0, "SDD Existential Operator: 0 (defeault, simple) 1 (per integer) 2 (reverse order)", "<0|1|2>"},
	{ "vtree-pen", 0, POPT_ARG_INT, &vtree_penalty_fn, 0, "Choice of Vtree penalty function heuristic during static Vtree search: 0 (no heuristic search) 1 (sum max distances) 2 (sum read-write) 3 (weighted read-write) 4 (inverse weighted read-write)", "<0|1|2|3|4>"},
	{ "dynamic-vtree", 0, POPT_ARG_INT, &dynamic_vtree_search, 0, "Turn dynamic Vtree minimization off (0) or on (1)", "<0|1>"},
	{ "sdd-verbose", 0, POPT_ARG_NONE, &sdd_verbose, 0, "Display time and memory usage statistics", "<0|1>"},
    POPT_TABLEEND
};

const SddSize sdd_gc_threshold_abs = 100000000; // Absolute threshold: 100MB
unsigned int enum_error = 0; // flag whether enum has gone wrong yet
const double vtree_search_timeout = 30.0; // 30 seconds for Vtree search
unsigned int gc_count = 0; // Number of garbage collections
unsigned int dynMin_count = 0; // Number of calls to dynamic Vtree minimization
unsigned int exploration_started = 0; // Whether the first element has been added yet
clock_t sdd_exploration_start;
void* dummy; int dummy_int; // Dummy variables that we use to suppress compiler warnings "Warning: unused parameter"
const double base_wait_dm = 1.0;  // Base amount of time to wait between dynamic minimization runs. Default: 1 second

SddNode* rel_update_smart_temp;
const unsigned int rel_update_smart_cache_size = 64;
SddNode** rel_update_smart_cache;
unsigned int rel_update_smart_i = 0;

unsigned int peak_footprint = 0;


// Do and undo functionality
struct vtree_rotation {
	SddLiteral dissection_literal;
	unsigned int direction;
};

// Our approach uses one SDD manager, which we will call Sisyphus,
// after the Greek mythological figure, condemned to endlessly roll a boulder up a hill,
// only to see all his work undone, and to start over again from the bottom
SddManager* sisyphus = NULL;

void print_footprint() {
    double time_elapsed = (double)(clock() - sdd_exploration_start);
    Printf(infoLong, "\n\tExist\tUnion\tIntesc\tRel\tRi\tenum\tcount\trefrnc\tvstatc\tvtsearc\tnscb\tnnscb\telems k\tnodes\tMem kB\tgc\tdyn\n"
"Profile\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.4f\t%u\t%lu\t%lu\t%lu\t%u\t%u\n\t%.1f\%\t%.1f\%\t%.1f\%\t%.1f\%\t%.1f\%\t%.1f\%\n",
            exists_time / CLOCKS_PER_SEC, union_time / CLOCKS_PER_SEC,
            conjoin_time / CLOCKS_PER_SEC, rel_update_time / CLOCKS_PER_SEC, rel_increment_time / CLOCKS_PER_SEC,
            sdd_enumerate_time / CLOCKS_PER_SEC, count_time / CLOCKS_PER_SEC, reference_time / CLOCKS_PER_SEC, vtree_setup_time / CLOCKS_PER_SEC,
            vtree_search_time / CLOCKS_PER_SEC, nscb_time / CLOCKS_PER_SEC,
            nNextState_cb,
            sdd_manager_live_size(sisyphus)/1000, sdd_manager_live_count(sisyphus), sdd_memory_live_footprint(), gc_count, dynMin_count,
            100.0f * exists_time / time_elapsed, 100.0f * union_time / time_elapsed,
            100.0f * conjoin_time / time_elapsed, 100.0f * rel_update_time / time_elapsed, 100.0f* rel_increment_time / time_elapsed,
            100.0f * sdd_enumerate_time / time_elapsed);
}

/* Make the specified target into an integer,
 * using the user-specified way (option vtree-integer)
 */
void vtree_make_integer(Vtree* target) {
	switch (vtree_integer_config) {
	case 1:
		vtree_make_augmented_right_linear(target, sisyphus); break;
	case 2:
		vtree_make_right_linear(target, sisyphus); break;
	case 3:
		vtree_make_right_balanced(target, 15, sisyphus); break;
	case 0:
	default:
		vtree_make_balanced(target, sisyphus); break;
	}
}

/* Given a Vtree whose leaves represent the integers of the program,
 *   "fold out" each integer in the tree, to obtain a "bit-blasted" tree,
 *   representing all bits of the program
 * Input:  A Vtree over dom->vectorsize variables
 * Output: A Vtree over dom->vectorsize * xstatebits variables
 *
 * Thm. Rightmost literal of (left child) * 2 - 1 == sdd_vtree_position(tree)
 *
 *    ***   Usage of Vtree rotations   ***
 *    Rotating a node RIGHT will diminish the set of variables which it spans
 *    Rotating a node LEFt  will increase the set of variables which it spans
 *    In particular
 *    Let Span(x,t) be the span of node x at time t.
 *    From t=0 to t=1, the node x is rotated right. Then
 *    Span(x,1) = Span(right(left(x)),0) + Span(right(x),0)
 *    If, instead, the node x is rotated left, then
 *    Span(x,1) = Span(parent(x),0)
 *    This is done so that left/right rotating are each other's inverse
 */
void vtree_from_integer_tree(Vtree* tree_int, SddManager* manager_int) {
	Vtree* root = sdd_manager_vtree(sisyphus);
	SddLiteral dissection_lit_root = 2 * xstatebits * vtree_dissection_literal(tree_int);
	Vtree** root_location = sdd_vtree_location(root, sisyphus);
	Vtree* parent;

	if (sdd_vtree_is_leaf(tree_int)) {
		// First, find the right Vtree node.
		Vtree* int_parent = sdd_vtree_parent(tree_int);
		SddLiteral int_parent_dis = 2 * xstatebits * vtree_dissection_literal(int_parent);
		parent = get_vtree_by_dissection_literal(root, int_parent_dis);
		Vtree* node;
		if (tree_int == sdd_vtree_left(int_parent)) {
			node = sdd_vtree_left(parent);
		} else {
			node = sdd_vtree_right(parent);
		}
		// Now the root of the integer in the bit-tree is known:  node
		vtree_make_integer(node);
	} else if (tree_int == sdd_manager_vtree(manager_int)) { // tree_int is the root
		// Find the intended root
//		printf("Root of tree: %li. Getting intended root.\n", sdd_vtree_position(root)); fflush(stdout);
		Vtree* intended_root = get_vtree_by_dissection_literal(root, dissection_lit_root);
//		printf("Got intended root: %li.\n", sdd_vtree_position(intended_root)); fflush(stdout);
		while (root != intended_root) {
			sdd_vtree_rotate_up(intended_root, sisyphus);
			root = *root_location;
//			printf("New root: %li. Getting intended root pointer.\n", sdd_vtree_position(root));
			intended_root = get_vtree_by_dissection_literal(root, dissection_lit_root);
//			printf("Got intended root pointer: %li.\n", sdd_vtree_position(intended_root));
		}

		vtree_from_integer_tree(sdd_vtree_left (tree_int), manager_int);
		vtree_from_integer_tree(sdd_vtree_right(tree_int), manager_int);
//		sdd_vtree_save_as_dot("after-rotation.dot", root);
	}
	else {
		// tree_int is not the root of the integer template tree
		Vtree* intended_node = get_vtree_by_dissection_literal(root, dissection_lit_root);
		parent            = sdd_vtree_parent(intended_node);
		SddLiteral parent_dis     = vtree_dissection_literal(parent);
		SddLiteral intended_parent_dis = vtree_dissection_literal(sdd_vtree_parent(tree_int)) * 2 * xstatebits;
//		SddLiteral low, high;
		while (parent_dis != intended_parent_dis) {
			sdd_vtree_rotate_up(intended_node, sisyphus);
			intended_node = get_vtree_by_dissection_literal(root, dissection_lit_root);
			parent = sdd_vtree_parent(intended_node);
			parent_dis = vtree_dissection_literal(parent);
		}
		vtree_from_integer_tree(sdd_vtree_left (tree_int), manager_int);
		vtree_from_integer_tree(sdd_vtree_right(tree_int), manager_int);
	}
}

/* Penalty: Sum of distances between read variables
 */
unsigned int vtree_penalty_0(Vtree* tree) {
	unsigned int penalty = 0;
	Vtree* v1_vtree; Vtree* v2_vtree;
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		for (int v1=0; v1<rel->r_k-1; v1++) {
		for (int v2=v1+1; v2<rel->r_k; v2++) {
			v1_vtree = get_vtree_literal(tree, rel->r_proj[v1] + 1);
			v2_vtree = get_vtree_literal(tree, rel->r_proj[v2] + 1);
			penalty += vtree_distance(v1_vtree, v2_vtree);
		}}
	}
	return penalty;
}

/* Penalty: sum of relations' longest path
 */
unsigned int vtree_penalty_1(Vtree* tree) {
	unsigned int pen = 0;
	Vtree* lca; Vtree* v1_vtree; Vtree* v2_vtree;
	unsigned int v1_dist, v2_dist, vdist_max;
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		// Compute the penalty
		vdist_max = 0;
		for (int v1=0; v1<rel->r_k-1; v1++) {
		for (int v2=v1+1; v2<rel->r_k; v2++) {
//			printf("Getting Vtree node of literal %lu.\n", rel->r_proj[v1] + 1); fflush(stdout);
			v1_vtree = get_vtree_literal(tree, rel->r_proj[v1] + 1);
//			printf("Getting Vtree node of literal %lu.\n", rel->r_proj[v2] + 1); fflush(stdout);
			v2_vtree = get_vtree_literal(tree, rel->r_proj[v2] + 1);
//			printf("Getting lca.\n"); fflush(stdout);
			lca = sdd_vtree_lca(v1_vtree, v2_vtree, tree);
//			printf("Got lca. Getting vertical distance.\n"); fflush(stdout);
			v1_dist = vtree_vertical_distance(lca, v1_vtree);
//			printf("Got one vertical distance.\n"); fflush(stdout);
			v2_dist = vtree_vertical_distance(lca, v2_vtree);
			if (v1_dist > vdist_max) vdist_max = v1_dist;
			if (v2_dist > vdist_max) vdist_max = v2_dist;
		}}
		pen += vdist_max;
	}
	return pen;
}

// Flow-neutral Vtrees:
// Penalty: Sum over relations: sum of pairwise distances
unsigned int vtree_penalty_2(Vtree* tree) {
	unsigned int penalty = 0;
	Vtree* v1_vtree; Vtree* v2_vtree;
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		for (int v1=0; v1<rel->r_k; v1++) {
		for (int v2=0; v2<rel->w_k; v2++) {
			v1_vtree = get_vtree_literal(tree, rel->r_proj[v1] + 1);
			v2_vtree = get_vtree_literal(tree, rel->w_proj[v2] + 1);
			penalty += vtree_distance(v1_vtree, v2_vtree);
		}}
	}

	return penalty;
}

// Right-flowing Vtrees:
// Penalty for write->read dependencies is worse than for read->write dependencies
unsigned int vtree_penalty_3(Vtree* tree) {
	unsigned int penalty = 0;
	Vtree* v1_vtree; Vtree* v2_vtree;
	unsigned int vdist;
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		for (int v1=0; v1<rel->r_k; v1++) {
		for (int v2=0; v2<rel->w_k; v2++) {
			v1_vtree = get_vtree_literal(tree, rel->r_proj[v1] + 1);
			v2_vtree = get_vtree_literal(tree, rel->w_proj[v2] + 1);
			vdist = vtree_distance(v1_vtree, v2_vtree);
			if (rel->r_proj[v1] <= rel->w_proj[v2])  {
				penalty += vdist;
			} else {
				penalty += 3 * vdist;
			}
		}
		}
	}

	return penalty;
}

// Left-flowing Vtrees:
// Penalty for read->write dependencies is worse than for write->read dependencies
unsigned int vtree_penalty_4(Vtree* tree) {
	unsigned int penalty = 0;
	Vtree* v1_vtree; Vtree* v2_vtree;
	unsigned int vdist;
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		for (int v1=0; v1<rel->r_k; v1++) {
		for (int v2=0; v2<rel->w_k; v2++) {
			v1_vtree = get_vtree_literal(tree, rel->r_proj[v1] + 1);
			v2_vtree = get_vtree_literal(tree, rel->w_proj[v2] + 1);
			vdist = vtree_distance(v1_vtree, v2_vtree);
			if (rel->r_proj[v1] <= rel->w_proj[v2])  {
				penalty += 3 * vdist;
			} else {
				penalty += vdist;
			}
		}}
	}
	return penalty;
}

int min(int a, int b) {
	return (a<b)? a : b;
}

// Penalty: Sum over distances between all variables in relation, oblivious to read/write roles
unsigned int vtree_penalty_5(Vtree* tree) {
	unsigned int penalty = 0;
	Vtree* v1_vtree; Vtree* v2_vtree;
	int u, v = 0;
	int u_r, u_w, v_r, v_w;
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		// Choose first pair
		u_r = 0; u_w = 0;
		if (rel->r_proj[0] <= rel->w_proj[0]) {
			v_r = 1; v_w = 0;
		} else {
			v_r = 0; v_w = 1;
		}
		while (u_r < rel->r_k || u_w < rel->w_k) {
			// Set a value for u
			if (u_r < rel->r_k) {
				if (u_w < rel->w_k) {
					u = min(rel->r_proj[u_r], rel->w_proj[u_w]);
				} else {
					u = rel->rel->r_proj[u_r];
				}
			} else {
				u = rel->w_proj[u_w];
			}
			// Choose v the value after r
			if (u_r + 1 < rel->r_k && u == rel->r_proj[u_r]) {
				v_r = u_r + 1;
				if (u_w + 1 < rel->w_k && u == rel->w_proj[u_w]) {
					v_w = u_w + 1;
				} else {
					v_r = u_r + 1;
					v_w = u_w;
				}
			} else if (u_w + 1 < rel->w_k && u == rel->w_proj[u_w]) {
				v_w = u_w+1;
				v_r = u_r;
			} else {
				break;
			}

			while (v_r < rel->r_k || v_w < rel->w_k) {
				if (v_r < rel->r_k) {
					if (v_w < rel->w_k) {
						v = min(rel->r_proj[v_r], rel->w_proj[v_w]);
					} else {
						v = rel->r_proj[v_r];
					}
				} else if (v_r < rel->w_k) {
					v = rel->r_proj[v_w];
				}
				// Do the summation
				v1_vtree = get_vtree_literal(tree, u + 1);
				v2_vtree = get_vtree_literal(tree, v + 1);
				penalty += vtree_distance(v1_vtree, v2_vtree);
				// Find next v
				if (v_r < rel->r_k && v == rel->r_proj[v_r]) {
					v_r++;
				}
				if (v_w < rel->w_k && v == rel->w_proj[v_w]) {
					v_w++;
				}
			}
			// Find next u
			if (u_r < rel->r_k && u == rel->r_proj[u_r]) {
				u_r++;
			}
			if (u_w < rel->w_k && u == rel->w_proj[u_w]) {
				u_w++;
			}
		}
	}
	return penalty;
}

// Penalty: Sum over harmonic mean of order distance and tree distance
unsigned int vtree_penalty_6(Vtree* tree) {
	unsigned int penalty = 0;
	Vtree* v1_vtree; Vtree* v2_vtree;
	unsigned int vdist;
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		for (int v1=0; v1<rel->r_k; v1++) {
		for (int v2=0; v2<rel->w_k; v2++) {
			v1_vtree = get_vtree_literal(tree, rel->r_proj[v1] + 1);
			v2_vtree = get_vtree_literal(tree, rel->w_proj[v2] + 1);
			vdist = vtree_distance(v1_vtree, v2_vtree);
			penalty += sqrt(vdist * abs(rel->r_proj[v1] - rel->w_proj[v2]));
		}}
	}
	return penalty;
}

// Penalty: Maximum distance between two variables in relation
unsigned int vtree_penalty_7(Vtree* tree) {
	unsigned int penalty = 0;
	Vtree* v1_vtree; Vtree* v2_vtree;
	unsigned int dist, maxdist;
	// Initialize lca
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		maxdist = 0;
		for (int v1=0; v1<rel->r_k; v1++) {
		for (int v2=0; v2<rel->w_k; v2++) {
			v1_vtree = get_vtree_literal(tree, rel->r_proj[v1]+1);
			v2_vtree = get_vtree_literal(tree, rel->r_proj[v2]+1);
			dist = vtree_distance(v1_vtree, v2_vtree);
			if (dist > maxdist) {
				maxdist = dist;
			}
		}}
		penalty += maxdist;
	}
	return penalty;
}

// Penalty: Number of literals under the relation's lca
unsigned int vtree_penalty_8(Vtree* tree) {
	unsigned int penalty = 0;
	Vtree* v1_vtree;
	Vtree* lca;
	// Initialize lca
	if (first_vrel->r_k > 0) {
		lca = get_vtree_literal(tree, first_vrel->r_proj[0]);
	} else {
		lca = get_vtree_literal(tree, first_vrel->w_proj[0]);
	}
	for (vrel_ll_t rel = first_vrel; rel != 0 && rel->next != 0; rel = rel->next) {
		for (int v=0; v<rel->r_k; v++) {
			v1_vtree = get_vtree_literal(tree, rel->r_proj[v] + 1);
			lca = sdd_vtree_lca(lca, v1_vtree, tree);
		}
		for (int v=0; v<rel->w_k; v++) {
			v1_vtree = get_vtree_literal(tree, rel->w_proj[v]+1);
			lca = sdd_vtree_lca(lca, v1_vtree, tree);
		}
		penalty += vtree_variables_count(lca);
	}
	return penalty;
}

unsigned int vtree_penalty(Vtree* tree) {
	switch (vtree_penalty_fn) {
	case 1:  return vtree_penalty_1(tree);
	case 2:  return vtree_penalty_2(tree);
	case 3:  return vtree_penalty_3(tree);
	case 4:  return vtree_penalty_4(tree);
	default: return 0;
	}
}

Vtree* initial_integer_tree() {
	switch (static_vtree_search) {
		case 1: return sdd_vtree_new(first_vrel->rel->dom->vectorsize, "balanced"); break;
		case 3: return sdd_vtree_new(first_vrel->rel->dom->vectorsize, "left"); break;
		case 2:
		default: return sdd_vtree_new(first_vrel->rel->dom->vectorsize, "right"); break;
	}
}

void heuristic_vtree_search(Vtree* tree_int, SddManager* manager_int) {
	clock_t before = clock();
	// Greedy search for best vtree
	const unsigned int budget = 3*sdd_manager_var_count(manager_int);
	unsigned int penalty_current, penalty_min, penalty;
	struct sdd_vtree_rotation rotation;
	SddLiteral dis_min = 0;
	Vtree* target; Vtree* parent;
	penalty_current = vtree_penalty(tree_int);
	penalty_min = penalty_current;
	for (unsigned int i=0; i<budget; i++) {
		if ((clock() - before) / CLOCKS_PER_SEC > vtree_search_timeout) break;
		penalty_min = penalty_current;
		for (SddLiteral v=1; v < sdd_manager_var_count(manager_int); v++) {
			if ((clock() - before) / CLOCKS_PER_SEC > vtree_search_timeout) break;
//			printf("Start search %i %li\n", i, v); fflush(stdout);
			target = get_vtree_by_dissection_literal(sdd_manager_vtree(manager_int), v);
//			printf("Got the target: %lu.\n", sdd_vtree_position(target)); fflush(stdout);
//			tree_int = sdd_manager_vtree(manager_int);
//			printf("Got the root: %lu.\n", sdd_vtree_position(tree_int)); fflush(stdout);
			parent = sdd_vtree_parent(target);
			if (parent == 0) {
//				printf("This is the root, skipping.\n"); fflush(stdout);
				continue;
			}
			rotation = sdd_vtree_rotate_up(target, manager_int);
//			printf("Rotated up.\n"); fflush(stdout);
			penalty = vtree_penalty(sdd_manager_vtree(manager_int));
//			printf("Penalty: %u.\n", penalty);  fflush(stdout);
			sdd_vtree_undo_rotation(rotation);
//			printf("Undid rotation.\n"); fflush(stdout);
			if (penalty < penalty_min) {
				dis_min = v;
				penalty_min = penalty;
//				printf("Found a better Vtree by rotating %lu yielding %u\n", dis_min, penalty); fflush(stdout);
			}
			// Save the stats
			if (penalty < penalty_min) {
//					printf("Found a better Vtree by rotating %lu yielding %u\n", dis_min, penalty); fflush(stdout);
				dis_min = v;
				penalty_min = penalty;
			}
		}
		// If an improvement is found
		if (penalty_min < penalty_current) {
			// Perform the best operation
//			printf("Found an improvement: Rotate %li up for %u penalty. Implementing improvement.\n", dis_min, penalty_min); fflush(stdout);
			tree_int = sdd_manager_vtree(manager_int);
			target = get_vtree_by_dissection_literal(tree_int, dis_min);
			sdd_vtree_rotate_up(target, manager_int);
//			printf("Implemented improvement.\n"); fflush(stdout);
			tree_int = sdd_manager_vtree(manager_int);
			penalty_current = penalty_min;
		} else {
            Printf(infoLong, "No improvement is possible with local rotations.\n");
			break; // No improvement possible; terminate
		}
	}
}

void find_static_vtree() {
    Printf(infoLong, "Finding static vtree.\n");
//	clock_t before = clock();
	Vtree* tree_int = initial_integer_tree();
	SddManager* manager_int = sdd_manager_new(tree_int);
	// This manager contains one node for each integer variable of the program.
	// It is a "draft" tree, called the "abstract program variable tree" (AVT)

	if (vtree_penalty_fn) {
		heuristic_vtree_search(tree_int, manager_int);
	}

	// Convert the "draft" tree with integer-labeled leaves to
	//   a larger one with bit-labeled leaves
	vtree_from_integer_tree(sdd_manager_vtree(manager_int), manager_int);
//	vtree_setup_time = (double)(clock() - before);
}

/* This function is called once, when the exploration is about to begin
 */
void sdd_initialise_given_rels() {
    Printf(infoLong, "Initializing SDD data structures.\n");  fflush(stdout);
	if (exploration_started) return;
	exploration_started = 1;
	Vtree* tree = sdd_vtree_new(2 * xstatebits * first_vset->set->dom->vectorsize, "right");
	sisyphus = sdd_manager_new(tree);
	if (static_vtree_search || vtree_penalty_fn) {
		find_static_vtree();
	}
	// Give all the vsets and rels empty SDDs relative to sisyphus
	for (vset_ll_t set = first_vset; set != 0; set = set->next) {
		set->set->sdd = sdd_manager_false(sisyphus);
	}
	for (vrel_ll_t rel = first_vrel; rel != 0; rel = rel->next) {
		rel->rel->sdd = sdd_manager_false(sisyphus);
	}
    Printf(infoLong, "SDD Variable Tree and Manager are initialised..\n"); fflush(stdout);
}

static SddModelCount set_count_exact(vset_t set) {
	SddNode* sur = set->sdd;
	// Set all prime variables to True
	for (SddLiteral v = 2; v<=sdd_manager_var_count(sisyphus); v+=2) {
		sur = sdd_conjoin(sur, sdd_manager_literal(v, sisyphus), sisyphus);
	}
	/* Set all unused variables to True */
	vset_ll_t set_ll = get_vset(set->id);
	if (set_ll->k != -1) {
		int set_var = 0;
		int sdd_var = 1;
		for (int i=0; i<set->dom->vectorsize; i++) {
			if (set_var < set_ll->k && set_ll->proj[set_var] == i) {
					// Ok, these variables are used
					sdd_var += 2 * xstatebits;
					set_var += 1;
			}
			else {
				// These variables are not used, so set them to True
				for (int j=0; j<xstatebits; j++) {
					sur = sdd_conjoin(sur, sdd_manager_literal(sdd_var, sisyphus), sisyphus);
					sdd_var += 2;
				}
			}
		}
	}
	SddModelCount mc;
	mc = sdd_global_model_count(sur, sisyphus);
	return mc;
}

// Collect garbage and minimize Vtree if conditions are met
void sdd_minimize_maybe() {
	if (sdd_manager_dead_size(sisyphus) > sdd_gc_threshold_abs) {
		sdd_manager_garbage_collect(sisyphus);
		//printf("Sdd Garbage Collect.\n");
		gc_count++;
	}
	static clock_t last_dynamic_minimization = 0;
	if (last_dynamic_minimization == 0) last_dynamic_minimization = clock();

	if (dynamic_vtree_search) {
		// Employ a scale-free minimization strategy
		double time_since_ldm = (double)(clock() - last_dynamic_minimization);
		// Get nr of zeroes
		unsigned int k=1;
		while (!((dynMin_count+1) & k)) {
			k = (k << 1);
		}
		double time_threshold = ((double)k) * base_wait_dm;
		// Is the time since last Dynamic Minimization greater than
		// the time threshold?
		if (time_since_ldm > time_threshold * CLOCKS_PER_SEC) {
			//set Time budget. Ensure that no more than 50% of time is spent minimizing vtrees

			double search_time_budget = time_since_ldm / CLOCKS_PER_SEC;

			// Keep default time limit ratios
			sdd_manager_set_vtree_search_time_limit(search_time_budget, sisyphus);
			sdd_manager_set_vtree_fragment_time_limit(search_time_budget, sisyphus);
			sdd_manager_set_vtree_operation_time_limit(search_time_budget, sisyphus);
			sdd_manager_set_vtree_apply_time_limit(search_time_budget, sisyphus);
			Printf(infoLong, "[Dyn min] Minimizing SDD with time limit %f\n", search_time_budget);
			clock_t before = clock();
			sdd_manager_minimize_limited(sisyphus);
			double elapsed = (double)(clock() - before);
			vtree_search_time += elapsed;
			Printf(infoLong, "[Dyn min] Minimization time: %.1f (%.1f more than expected)\n", elapsed / CLOCKS_PER_SEC, search_time_budget - elapsed / CLOCKS_PER_SEC);
			dynMin_count++;
			last_dynamic_minimization = clock();
		}
	}
}

void sdd_set_and_ref(vset_t set, SddNode* S) {
//	printf("[sdd set and ref] start  set %u  S=%p\n", set->id, S); fflush(stdout);
//	clock_t before = clock();
	sdd_ref(S, sisyphus);
//	printf("[sdd set and ref] Referenced.\n"); fflush(stdout);
	sdd_deref(set->sdd, sisyphus);
//	reference_time += (double)(clock() - before);
//	printf("[sdd set and ref] Dereferenced.\n"); fflush(stdout);
	set->sdd = S;
	unsigned int fp = sdd_memory_live_footprint();
	if (fp > peak_footprint) peak_footprint = fp;
	sdd_minimize_maybe();
}

void sdd_set_rel_and_ref(vrel_t rel, SddNode* R) {
//	clock_t before = clock();
	sdd_ref(R, sisyphus);
	sdd_deref(rel->sdd, sisyphus);
//	reference_time += (double)(clock() - before);
	rel->sdd = R;
	unsigned int fp = sdd_memory_live_footprint();
	if (fp > peak_footprint) peak_footprint = fp;
	sdd_minimize_maybe();
}

static vset_t set_create(vdom_t dom, int k, int* proj) {
	static unsigned int id = 0;
//	printf("[Sdd set create] id=%u  k = %i.\n", id, k); fflush(stdout);
	vset_t set = (vset_t) malloc(sizeof(struct vector_set));
	set->dom = dom;
	if (exploration_started) {
		set->sdd = sdd_manager_false(sisyphus);
	}
	else {
		set->sdd = 0;
	}
	set->k = k;
	set->id = id;
	id++;

	if (k == -1) {
		set->proj = NULL;
		set->state_variables = dom->state_variables;
		set->nstate_variables = dom->totalbits;
	}
	else {
		set->proj = malloc(sizeof(int[k]));
		memcpy(set->proj, proj, sizeof(int[k]));
		set->nstate_variables = xstatebits * k;
		set->state_variables = malloc(sizeof(uint32_t[set->nstate_variables]));
		uint32_t curvar = 0;
		int j = 0;
		int i_var = 0;
		for (int i=0; i<dom->vectorsize && j<k; i++) {
			if (i == proj[j]) {
				for (int x=0; x<dom->statebits[i]; x++) {
					set->state_variables[i_var] = curvar;
					i_var++;
					curvar += 2;
				}
				j++;
			}
			else {
				curvar += 2 * dom->statebits[i];
			}
		}

	}
	add_vset(set);

	return set;
}

static void set_destroy(vset_t set) {
	set->sdd = sdd_manager_false(sisyphus);
}

static vrel_t rel_create_rw(vdom_t dom, int r_k, int* r_proj, int w_k, int* w_proj) {
	static unsigned int id = 0;

	vrel_t rel = (vrel_t) RTmalloc(sizeof(struct vector_relation));
	rel->dom = dom;
	if (exploration_started) {
		rel->sdd = sdd_manager_false(sisyphus);
	} else {
		rel->sdd = 0;
	}
	rel->r_k = r_k;
	rel->w_k = w_k;
	rel->id = id;
	id++;
	/*
	if (rel->sdd == 0) {
		printf("  [Sdd relation create (id=%u)] sdd=0 :-(\n", rel->id);
	}
	else {
		printf("  [Sdd relation create (id=%u)] sdd=%p adr=%p :-)\n", rel->id, rel->sdd, rel);
	}
	 */
	rel->r_proj = malloc(sizeof(int) * r_k);
	rel->w_proj = malloc(sizeof(int) * w_k);
	memcpy(rel->r_proj, r_proj, sizeof(int) * r_k);
	memcpy(rel->w_proj, w_proj, sizeof(int) * w_k);

	// Compute a_proj (accessed projection), the union of the sets of read and write variables

	// Compute ro_proj (read-only): a_proj \ w_proj

	// Initialise the state variables (the unprimed variables, i.e. the bits that are read
	int state_vars[xstatebits * r_k];
	int curvar = 0; //
	int i=0, j=0, n=0;
	for (; i<dom->vectorsize && j<r_k; i++) {
		if (i == r_proj[j]) {
			for (int k=0; k<dom->statebits[i]; k++) {
				state_vars[n] = curvar;
				n++;
				curvar += 2;
			}
			j++;
		}
		else {
			curvar += 2 * dom->statebits[i];
		}
	}
	rel->state_variables = malloc(sizeof(int) * n);
	memcpy(rel->state_variables, state_vars, sizeof(int) * n);

	int prime_vars[xstatebits * w_k];
	curvar = 0;
	j = 0; n = 0;
	for (i=0; i<dom->vectorsize && j<w_k; i++) {
		if (i == w_proj[j]) {
			for (int k=0; k<dom->statebits[i]; k++) {
				prime_vars[n] = curvar + 1;
				n++;
				curvar += 2;
			}
			j++;
		}
		else {
			curvar += 2 * dom->statebits[i];
		}
	}
	rel->prime_variables = malloc(sizeof(int) * n);
	memcpy(rel->prime_variables, prime_vars, sizeof(int) * n);
	add_vrel(rel);
	//vrel_exposition(rel);

	return rel;
}

static void rel_destroy(vrel_t rel) {
	sdd_deref(rel->sdd, sisyphus);
	rel->sdd = sdd_manager_false(sisyphus);
}

static void state_to_cube(vdom_t dom, int k, const int* proj, const int* state, uint8_t* arr) {

	/* A re-write for SDD, of Sylvan's state_to_cube method	 */
	if (k == -1) {
		int sb;
		int i_arr = 0;
		int i_state = 0;
		for (int i=0; i<dom->vectorsize; i++) {
			sb = dom->statebits[i];
			for (int n=0; n<sb; n++) {
				arr[i_arr] = ( state[i_state] & (1 << (n)) ) ? 1 : 0;
				i_arr++;
			}
			i_state++;
		}
	}
	else {
		int j=0;
		int sb;
		int i_arr = 0; // index in arr
		for (int i=0; i<dom->vectorsize && j<k; i++) {
			if (i == proj[j]) {
				sb = dom->statebits[i];
				for (int n=0; n<sb; n++) {
					arr[i_arr] = (state[j] & (1 << (sb-n-1))) ? 1 : 0;
					i_arr++;
				}
				j++;
			}
		}
	}
}

// Adds the state vector e to the set
// e has length dom->vectorsize and each 4-byte integer contains xstatebits bits
void set_add(vset_t set, const int* e) {
	// This function is the first function that is called when exploration starts
	if (!exploration_started) {
		sdd_initialise_given_rels();
	}
	if (set->k == -1) {
		uint8_t cube[set->dom->vectorsize * xstatebits];
		state_to_cube(set->dom, set->k, set->proj, e, cube);
		SddNode* cube_sdd = sdd_getCube(set->state_variables, cube, set->nstate_variables);
		SddNode* disjoined = sdd_disjoin(set->sdd, cube_sdd, sisyphus);
		sdd_set_and_ref(set, disjoined);
	}
	else {
        // e is a full state vector, but this set only "cares" about a subset of the variables.
		// Collect the relevant variables into f
        int f[set->k];
        for (int i=0, j=0; i<set->dom->vectorsize && j<set->k; i++) {
            if (i == set->proj[j]) {
            	f[j++] = e[i];
            }
        }
        // cube is an array of 16*k bits
        uint8_t cube[set->k * xstatebits];
        state_to_cube(set->dom, set->k, set->proj, f, cube);
        sdd_set_and_ref(set, sdd_disjoin(set->sdd, sdd_getCube(set->state_variables, cube, set->nstate_variables), sisyphus));
	}
}

static int set_member(vset_t set, const int* e) {
	SddNode* elem = sdd_manager_true(sisyphus);
	vset_ll_t set_ll = get_vset(set->id);
	if (set_ll->k == -1) {
		for (int i=0; i<set->dom->vectorsize; i++) {
			for (int b=0; b<xstatebits; b++) {
				elem = sdd_conjoin(elem, getLiteral(xstatebits * i + b + 1, (e[i] & (1 << b)) ? 1 : 0, 0), sisyphus);
			}
		}
	}
	else {
		for (int i=0; i<set_ll->k; i++) {
			for (int b=0; b<xstatebits; b++) {
				elem = sdd_conjoin(elem, getLiteral(xstatebits * set_ll->proj[i] + b + 1, (e[i] & (1<<b)) ? 1 : 0, 0), sisyphus);
			}
		}
	}
	elem = sdd_conjoin(elem, set->sdd, sisyphus);
	return sdd_node_is_false(elem);
}

static int set_is_empty(vset_t set) {
	print_footprint();
	unsigned int fp = sdd_memory_live_footprint();
	if (fp > peak_footprint) peak_footprint = fp;
	if (sdd_node_is_false(set->sdd)) {
		Printf(infoLong, "Peak %u\n", peak_footprint);
		return 1;
	}
	return 0;
}

static int set_equal(vset_t set1, vset_t set2) {
	return set1->sdd == set2->sdd;
}

static void set_clear(vset_t set) {
	sdd_deref(set->sdd, sisyphus);
	set->sdd = sdd_manager_false(sisyphus);
}

static void set_copy(vset_t dst, vset_t src) {
	sdd_set_and_ref(dst, src->sdd);

	// Save the fact that dst is now defined over the variables of src
	vset_set_domain(dst, src);
}

static void set_enum(vset_t set, vset_element_cb cb, void* context) {
	if (!sdd_node_is_false(set->sdd)) {
		int k = set->k == -1 ? set->dom->vectorsize : set->k;
		int vec[k]; // TODO finish this thought
		int d, var;
		SddModelCount i = 0;
		struct sdd_mit_master mas;
		for (mas = sdd_get_iterator(set); mas.finished == 0; sdd_next_model(&mas)) {
			// Refactor mas.e to a bit-array
			for (int i=0; i<k; i++) {
				vec[i] = 0; // TODO put this in the next loop
			}
			for (int i=0; i<k; i++) {
				var = set->k == -1 ? i : set->proj[i];
				for (int b=0; b<xstatebits; b++) {
					d = (mas.e[xstatebits*var+b]) << b;
					vec[i] |= d;
				}
			}
			cb(context, vec);
			i++;
		}
		sdd_mit_free(mas);
	}
}

static void set_update(vset_t dst, vset_t src, vset_update_cb cb, void* context) {
	dummy = dst;
	dummy = src;
	dummy = cb;
	dummy = context;
}

static void set_example(vset_t set, int* e) {
	Warning(info, "[Sdd set example] WARNING: NOT IMPLEMENTED\n");
	dummy = set;
	dummy = e;
}

static void set_count(vset_t set, long* nodes, double* elements) {
	if (nodes != NULL) {
		// we use 8 in sdd_size(set->sdd) * 8 because that is the size of a (sub,prime) element
		//*nodes = sdd_count(set->sdd) * 18 * sizeof(unsigned int) + sdd_size(set->sdd) * 8;
//		*nodes = sdd_manager_live_size(sisyphus) * 18 * sizeof(unsigned int) + sdd_manager_live_count(sisyphus) * 2 * sizeof(unsigned int);
		unsigned int fp = sdd_memory_live_footprint();
		if (fp > peak_footprint) peak_footprint = fp;
		*nodes = peak_footprint;
	}
	if (elements != NULL) {
//		clock_t before = clock();
		SddModelCount mc = set_count_exact(set);
//		count_time += (double)(clock() - before);

		*elements = (double) mc;
	}
	set = set; // to defuse the warning "unused parameter 'set'"
}

static void rel_count(vrel_t set, long* nodes, double* elements) {
//	printf("[Sdd rel count]rel %u\n", set->id);
	if (nodes != NULL) {
		*nodes = sdd_count(set->sdd);
	}
	if (elements != NULL) {
		*elements = (double) sdd_model_count(set->sdd, sisyphus);
	}
}

static void set_union(vset_t dst, vset_t src) {
	static unsigned int ncalls = 0; ncalls++;
	if (sdd_node_is_false(dst->sdd)) {
		sdd_set_and_ref(dst, src->sdd);
	}
	else if (sdd_node_is_false(src->sdd)) {
		// Do nothing.
	}
	else {
		if (vset_domains_are_disjoint(dst, src)) {
//			clock_t before = clock();
			SddNode* conjoin = sdd_conjoin(dst->sdd, src->sdd, sisyphus);
//			conjoin_time += (double)(clock() - before);
			sdd_set_and_ref(dst, conjoin);
			vset_add_to_domain(dst, src);
		}
		else {
//			printf("  [Sdd set union] Computations tells us the vset domains are not disjoint:\n");
//			vset_exposition(src);
//			vset_exposition(dst);
//			clock_t before = clock();
			SddNode* sdd_union = sdd_disjoin(dst->sdd, src->sdd, sisyphus);
//			union_time += (double)(clock() - before);
			sdd_set_and_ref(dst, sdd_union);
		}
	}
}

static void set_minus(vset_t dst, vset_t src) {
	if (dst->sdd != src->sdd) {
//		clock_t before = clock();
		SddNode* diff = sdd_conjoin(dst->sdd, sdd_negate(src->sdd, sisyphus), sisyphus);
//		conjoin_time += (double) (clock() - before);
		sdd_set_and_ref(dst, diff);
	}
	else {
		sdd_deref(dst->sdd, sisyphus);
		dst->sdd = sdd_manager_false(sisyphus);
//		printf("  [Sdd set minus] Sdds were equal, so %u := False (@%p)\n", dst->id, dst->sdd);
	}
}

static void set_intersect(vset_t dst, vset_t src) {
	if (dst->sdd != src->sdd) {
//		clock_t before = clock();
		SddNode* conjoined = sdd_conjoin(dst->sdd, src->sdd, sisyphus);
//		conjoin_time += (double)(clock() - before);
		sdd_set_and_ref(dst, conjoined);
//		dst->sdd = sdd_conjoin(dst->sdd, src->sdd, sisyphus);
	}
}

static void set_next(vset_t dst, vset_t src, vrel_t rel) {
	static unsigned int ncalls = 0; ncalls++;
	if (sdd_node_is_false(rel->sdd)) {
		sdd_deref(dst->sdd, sisyphus);
		dst->sdd = sdd_manager_false(sisyphus);
		return;
	}

	//printf("  [Sdd set next] Conjoining...\n");
//	clock_t before = clock();
	SddNode* conj = sdd_conjoin(src->sdd, rel->sdd, sisyphus);
//	conjoin_time += (double)(clock() - before);
//	mcSrc = sdd_model_count(conj, sisyphus);
//	printf("  [Sdd set next]  Conjoined! #conj=%llu\n", mcSrc);
	if (sdd_node_is_false(conj)) {
//		printf("  [Sdd set next] No transitions, so exit.\n");
		sdd_deref(dst->sdd, sisyphus);
		dst->sdd = sdd_manager_false(sisyphus);
		return;
	}
//	Perform the Existential Quantification

	vrel_ll_t rel_ll = get_vrel(rel->id);
	int n = sdd_manager_var_count(sisyphus);
	// $$> Static allocation update
	static int* exists_map = 0;
	if (exists_map == 0) {
		exists_map = malloc(sizeof(int) * (n + 1));
	}
	unsigned int sdd_var;
	SddNode* existed = 0;
	// Initialise map to zero
	for (int i=0; i <= n; i++) {
		exists_map[i] = 0;
	}
	switch( sdd_exist_config ) {
	case 0:
		// Mark variables read by rel
		for (int v=0; v<rel_ll->r_k; v++) {
			//printf(" %i:", rel_ll->r_proj[v]);
			for (int i=0; i<xstatebits; i++) {
				sdd_var = 2*(xstatebits*rel_ll->r_proj[v] + i)+1;
				//printf(" %u", sdd_var);
				exists_map[sdd_var] = 1;
			}
		}
		// Mark variables written by rel
		for (int v=0; v<rel_ll->w_k; v++) {
			for (int i=0; i<xstatebits; i++) {
				sdd_var = 2*(xstatebits*rel_ll->w_proj[v] + i)+1;
				exists_map[sdd_var] = 1;
			}
		}
//		before = clock();
		existed = sdd_exists_multiple(exists_map, conj, sisyphus);
//		exists_time += (double)(clock() - before);
		break;
	case 1: // integer by integer
		existed = conj;
		for (int v=0; v<rel_ll->r_k; v++) {
			// This iteration, exist away the v-th integer
			for (int i=0; i<xstatebits; i++) {
				sdd_var = 2*(xstatebits*rel_ll->r_proj[v] + i)+1;
				exists_map[sdd_var] = 1;
			}
//			before = clock();
			existed = sdd_exists_multiple(exists_map, existed, sisyphus);
//			exists_time += (double)(clock() - before);
			for (int i=0; i<xstatebits; i++) {
				sdd_var = 2*(xstatebits*rel_ll->r_proj[v] + i)+1;
				exists_map[sdd_var] = 0;
			}
		}
		break;
	case 2:
		for (int v=rel_ll->r_k-1; v >= 0; v -= 2) {
			// This iteration, exist away the v-th integer
			existed = conj;
			for (int w=0; w<2; w++) {
				for (int i=0; i<xstatebits; i++) {
					sdd_var = 2*(xstatebits*rel_ll->r_proj[v+w] + i)+1;
					exists_map[sdd_var] = 1;
				}
			}
//			before = clock();
			existed = sdd_exists_multiple(exists_map, existed, sisyphus);
//			exists_time += (double)(clock() - before);
			for (int w=0; w<2; w++) {
				for (int i=0; i<xstatebits; i++) {
					sdd_var = 2*(xstatebits*rel_ll->r_proj[v+w] + i)+1;
					exists_map[sdd_var] = 0;
				}
			}
		}
		break;
	}

	if (sdd_node_is_false(existed)) {
		sdd_deref(dst->sdd, sisyphus);
		dst->sdd = sdd_manager_false(sisyphus);
		return;
	}

	// Rename the primed variables to unprimed variables
	SddLiteral var_map[n+1];
	for (int i=0; i<=n; i++) {
		var_map[i] = i;
	}
	for (int v=0; v<rel_ll->r_k; v++) {
		for (int i=0; i<xstatebits; i++) {
			sdd_var = 2*(xstatebits*rel_ll->r_proj[v]+i) + 2;
			var_map[sdd_var] = sdd_var-1;
		}
	}
	for (int v=0; v<rel_ll->w_k; v++) {
		for (int i=0; i<xstatebits; i++) {
			sdd_var = 2*(xstatebits*rel_ll->w_proj[v]+i) + 2;
			var_map[sdd_var] = sdd_var - 1;
		}
	}


	SddNode* renamed = sdd_rename_variables(existed, var_map, sisyphus);
	sdd_set_and_ref(dst, renamed);
}

static void set_prev(vset_t dst, vset_t src, vrel_t rel, vset_t univ) {
	Warning(info, "[Sdd set_prev] WARNING: not implemented\n");
	dummy = dst;
	dummy = src;
	dummy = rel;
	dummy = univ;
}

static void set_project(vset_t dst, vset_t src) {
	// Static allocation update
	static int* exists_map = 0;
	if (!vset_domains_are_equal(dst, src)) {
		if (src->k == -1) {
			if (exists_map == 0) {
				exists_map = malloc((sdd_manager_var_count(sisyphus)+1) * sizeof(int)); // TODO allocate statically (done)
			}
			exists_map[0] = 0;
			// Set the primed variables to non-quantified
			for (int v=0; v<sdd_manager_var_count(sisyphus)/2; v++) {
				exists_map[2*v+2] = 0;
			}
			// Quantify away any state variables that do not appear in this set
			int var=0;
			for (unsigned int i=0; i<dst->nstate_variables; i++) {
				while (var < dst->state_variables[i]) {
					exists_map[var+1] = 1;
					var += 2;
				}
				exists_map[var+1] = 0;
				var += 2;
			}
			while (var < sdd_manager_var_count(sisyphus)) {
				exists_map[var+1] = 1;
				var += 2;
			}
//			clock_t before = clock();
			SddNode* proj = sdd_exists_multiple(exists_map, src->sdd, sisyphus); // Replaced by sdd_set_and_ref
			//exists_time += (double)(clock() - before);
			sdd_set_and_ref(dst, proj);

		}
		else {
			Abort("Error. in SDD set_project, src and dst are defined over different variables.\n");
			vset_exposition(src);
			vset_exposition(dst);
		}
	}
	else {
		sdd_set_and_ref(dst, src->sdd);
	}
}

static void set_zip(vset_t dst, vset_t src) {
	Warning(info, "[set_zip] Warning: NOT IMPLEMENTED.\n");
	dummy = dst;
	dummy = src;
	// TODO
}

static void rel_add_cpy(vrel_t rel, const int* src, const int* dst, const int* cpy) {
//	nscb_time += (double)(clock() - clock_before_nscb);
	vrel_ll_t rel_ll = get_vrel(rel->id);
//	clock_t before = clock();
	SddNode* srcSdd = sdd_manager_true(sisyphus);
	SddNode* dstSdd = sdd_manager_true(sisyphus);
	SddNode* src_and_dst;
	SddLiteral var;
	SddNode* x_and_prime;
	SddNode* integer_sdd;
	SddNode* copy_literal;
	int w, v_r, v_w;

	switch (vtree_increment_config) {
	case 0:
		// Prepare srcSdd
		for (int v=0; v<rel_ll->r_k; v++) {
	//		printf("  [Sdd rel add copy] Read variable %i: %i\n", v, rel_ll->r_proj[v]);
			for (int i=0; i<xstatebits; i++) {
				var = xstatebits*rel_ll->r_proj[v] + i + 1;
				srcSdd = sdd_conjoin(srcSdd, getLiteral(var, src[v] & (1 << i), 0), sisyphus);
			}
		}
		// Prepare dstSdd
		//printf("  [Sdd rel add copy] Finished src, dst in cases k != -1"); fflush(stdout);
		for (int v=0; v<rel_ll->w_k; v++) {
//			printf("  [Sdd rel add copy] Write variable %i: %i\n", v, rel_ll->w_proj[v]); fflush(stdout);
			if (cpy && cpy[v]) {
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->w_proj[v] + i + 1;
					copy_literal = sdd_disjoin(sdd_conjoin(getLiteral(var, 0, 0), getLiteral(var, 0, 1), sisyphus),
											   sdd_conjoin(getLiteral(var, 1, 0), getLiteral(var, 1, 1), sisyphus), sisyphus);
					dstSdd = sdd_conjoin(dstSdd, copy_literal, sisyphus);
				}
			} else {
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->w_proj[v] + i + 1;
					dstSdd = sdd_conjoin    (dstSdd, getLiteral(var, dst[v] & (1 << i), 1), sisyphus);
				}
			}
		}
		w = 0;
		for (int v=0; v<rel_ll->r_k; v++) {
			// Invariant: w_proj[w] >= r_proj[v]
			while (w < rel_ll->w_k && rel_ll->w_proj[w] < rel_ll->r_proj[v]) w++;
			if (w > rel_ll->w_k || rel_ll->w_proj[w] != rel_ll->r_proj[v]) {
				// Variable is read but not written, so cover it
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v] + i + 1;
					dstSdd = sdd_conjoin(dstSdd, getLiteral(var, src[v] & (1 << i), 1), sisyphus);
				}
			}
		}

		src_and_dst = sdd_conjoin(srcSdd, dstSdd, sisyphus);
		SddNode* disjoin = sdd_disjoin(rel->sdd, src_and_dst, sisyphus);
		sdd_set_rel_and_ref(rel, disjoin);
		break;
	case 1:
		// Prepare srcSdd
		for (int v=0; v<rel_ll->r_k; v++) {
			for (int i=0; i<xstatebits; i++) {
				var = xstatebits*rel_ll->r_proj[v] + i + 1;
				srcSdd = sdd_conjoin(srcSdd, getLiteral(var, src[v] & (1 << i), 0), sisyphus);
			}
		}
		// Prepare dstSdd
		for (int v=0; v<rel_ll->w_k; v++) {
			if (cpy && cpy[v]) {
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->w_proj[v] + i + 1;
					copy_literal = sdd_disjoin(sdd_conjoin(getLiteral(var, 0, 0), getLiteral(var, 0, 1), sisyphus),
											   sdd_conjoin(getLiteral(var, 1, 0), getLiteral(var, 1, 1), sisyphus), sisyphus);
					dstSdd = sdd_conjoin(dstSdd, copy_literal, sisyphus);
				}
			} else {
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->w_proj[v] + i + 1;
					dstSdd = sdd_conjoin    (dstSdd, getLiteral(var, dst[v] & (1 << i), 1), sisyphus);
				}
			}
		}
		w = 0;
		for (int v=0; v<rel_ll->r_k; v++) {
			// Invariant: w_proj[w] >= r_proj[v]
			while (w < rel_ll->w_k && rel_ll->w_proj[w] < rel_ll->r_proj[v]) w++;
			if (w > rel_ll->w_k || rel_ll->w_proj[w] != rel_ll->r_proj[v]) {
				// Variable is read but not written, so cover it
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v] + i + 1;
					dstSdd = sdd_conjoin(dstSdd, getLiteral(var, src[v] & (1 << i), 1), sisyphus);
				}
			}
		}

		src_and_dst = sdd_conjoin(srcSdd, dstSdd, sisyphus);
		rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, src_and_dst, sisyphus);
		break;
	case 2: // left-to-right.
		src_and_dst = sdd_manager_true(sisyphus);
		v_r = 0;
		v_w = 0;
		while (v_r < rel_ll->r_k || v_w < rel_ll->w_k) {
//			printf("[rel add cpy] v_r = %i  v_w = %i\n", v_r, v_w);fflush(stdout);
			if (v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] == rel_ll->w_proj[v_w]) {
//				printf("  Var is both read and written.\n");fflush(stdout);
				// Variable is both read and written
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					src_and_dst = sdd_conjoin(src_and_dst, getLiteral(var, src[v_r] & (1 << i), 0), sisyphus);
					src_and_dst = sdd_conjoin(src_and_dst, getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
				}
				v_r++;
				v_w++;
			} else if ((v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] < rel_ll->w_proj[v_w]) ||
					    v_w >= rel_ll->w_k) {
//				printf(" variable is read but not written.\n");fflush(stdout);
				// Variable is read but not written
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					src_and_dst = sdd_conjoin(src_and_dst, getLiteral(var, src[v_r] & (1 << i), 0), sisyphus);
					src_and_dst = sdd_conjoin(src_and_dst, getLiteral(var, src[v_r] & (1 << i), 1), sisyphus);
				}
				v_r++;
			} else {
				// Variable is written but not read
//				printf("Variable is written but not read.\n");fflush(stdout);
				if (cpy && cpy[v_w]) {
					for (int i=0; i<xstatebits; i++) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						copy_literal = sdd_disjoin(sdd_conjoin(getLiteral(var, 0, 0), getLiteral(var, 0, 1), sisyphus),
												   sdd_conjoin(getLiteral(var, 1, 0), getLiteral(var, 1, 1), sisyphus), sisyphus);
						src_and_dst = sdd_conjoin(src_and_dst, copy_literal, sisyphus);
					}
				} else {
					for (int i=0; i<xstatebits; i++) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						src_and_dst = sdd_conjoin(src_and_dst, getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					}
				}
				v_w++;
			}
		}
		rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, src_and_dst, sisyphus);
		break;
	case 3: // left-to-right, but conjoin two variables first
		src_and_dst = sdd_manager_true(sisyphus);
		v_r = 0;
		v_w = 0;
		while (v_r < rel_ll->r_k || v_w < rel_ll->w_k) {
			if (v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] == rel_ll->w_proj[v_w]) {
				// Variable is both read and written
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					src_and_dst = sdd_conjoin(src_and_dst, x_and_prime, sisyphus);
				}
				v_r++;
				v_w++;
			} else if ((v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] < rel_ll->w_proj[v_w]) ||
					    v_w >= rel_ll->w_k) {
				// Variable is read but not written
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, src[v_r] & (1 << i), 1), sisyphus);
					src_and_dst = sdd_conjoin(src_and_dst, x_and_prime, sisyphus);
				}
				v_r++;
			} else {
				// Variable is written but not read
				if (cpy && cpy[v_w]) {
					for (int i=0; i<xstatebits; i++) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						copy_literal = sdd_disjoin(sdd_conjoin(getLiteral(var, 0, 0), getLiteral(var, 0, 1), sisyphus),
												   sdd_conjoin(getLiteral(var, 1, 0), getLiteral(var, 1, 1), sisyphus), sisyphus);
						src_and_dst = sdd_conjoin(src_and_dst, copy_literal, sisyphus);
					}
				} else {
					for (int i=0; i<xstatebits; i++) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						src_and_dst = sdd_conjoin(src_and_dst, getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					}
				}
				v_w++;
			}
		}
		rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, src_and_dst, sisyphus);
		break;
	case 4: //left to right, integer by integer
		src_and_dst = sdd_manager_true(sisyphus);
		v_r = 0;
		v_w = 0;
		while (v_r < rel_ll->r_k || v_w < rel_ll->w_k) {
			integer_sdd = sdd_manager_true(sisyphus);
			if (v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] == rel_ll->w_proj[v_w]) {
				// Variable is both read and written
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					integer_sdd = sdd_conjoin(integer_sdd, x_and_prime, sisyphus);
				}
				v_r++;
				v_w++;
			} else if ((v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] < rel_ll->w_proj[v_w]) ||
					    v_w >= rel_ll->w_k) {
				// Variable is read but not written
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, src[v_r] & (1 << i), 1), sisyphus);
					integer_sdd = sdd_conjoin(integer_sdd, x_and_prime, sisyphus);
				}
				v_r++;
			} else {
				// Variable is written but not read
				if (cpy && cpy[v_w]) {
					for (int i=0; i<xstatebits; i++) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						copy_literal = sdd_disjoin(sdd_conjoin(getLiteral(var, 0, 0), getLiteral(var, 0, 1), sisyphus),
												   sdd_conjoin(getLiteral(var, 1, 0), getLiteral(var, 1, 1), sisyphus), sisyphus);
						integer_sdd = sdd_conjoin(integer_sdd, copy_literal, sisyphus);
					}
				} else {
					for (int i=0; i<xstatebits; i++) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						integer_sdd= sdd_conjoin(integer_sdd, getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					}
				}
				v_w++;
			}
			src_and_dst = sdd_conjoin(src_and_dst, integer_sdd, sisyphus);
		}
		rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, src_and_dst, sisyphus);
		break;
	case 6: // same as 4, but add this SDD to a fractionally-bookkept cache
		src_and_dst = sdd_manager_true(sisyphus);
		v_r = 0;
		v_w = 0;
		while (v_r < rel_ll->r_k || v_w < rel_ll->w_k) {
			integer_sdd = sdd_manager_true(sisyphus);
			if (v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] == rel_ll->w_proj[v_w]) {
				// Variable is both read and written
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					integer_sdd = sdd_conjoin(integer_sdd, x_and_prime, sisyphus);
				}
				v_r++;
				v_w++;
			} else if ((v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] < rel_ll->w_proj[v_w]) ||
					    v_w >= rel_ll->w_k) {
				// Variable is read but not written
				for (int i=0; i<xstatebits; i++) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, src[v_r] & (1 << i), 1), sisyphus);
					integer_sdd = sdd_conjoin(integer_sdd, x_and_prime, sisyphus);
				}
				v_r++;
			} else {
				// Variable is written but not read
				if (cpy && cpy[v_w]) {
					for (int i=0; i<xstatebits; i++) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						copy_literal = sdd_disjoin(sdd_conjoin(getLiteral(var, 0, 0), getLiteral(var, 0, 1), sisyphus),
												   sdd_conjoin(getLiteral(var, 1, 0), getLiteral(var, 1, 1), sisyphus), sisyphus);
						integer_sdd = sdd_conjoin(integer_sdd, copy_literal, sisyphus);
					}
				} else {
					for (int i=0; i<xstatebits; i++) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						integer_sdd= sdd_conjoin(integer_sdd, getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					}
				}
				v_w++;
			}
			src_and_dst = sdd_conjoin(src_and_dst, integer_sdd, sisyphus);
		}
		if (rel_update_smart_i + 1 == rel_update_smart_cache_size) {
//			 add the list to rel_update_smart_temp
			for (int b=0; b<6; b++) {
				for (unsigned int i=0; i<rel_update_smart_cache_size; i += (1 << (b+1))) {
					rel_update_smart_cache[i] = sdd_disjoin(rel_update_smart_cache[i], rel_update_smart_cache[i+(1<<b)], sisyphus);
				}
			}
			rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, rel_update_smart_cache[0], sisyphus);
			rel_update_smart_i = 0;
		}
		rel_update_smart_cache[rel_update_smart_i] = src_and_dst;
		rel_update_smart_i++;
		break;
	case 7: // Same as 6, but add the bits in reverse order
		src_and_dst = sdd_manager_true(sisyphus);
		v_r = 0;
		v_w = 0;
		while (v_r < rel_ll->r_k || v_w < rel_ll->w_k) {
			integer_sdd = sdd_manager_true(sisyphus);
			if (v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] == rel_ll->w_proj[v_w]) {
				// Variable is both read and written
				for (int i=xstatebits - 1; i>=0; i--) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					integer_sdd = sdd_conjoin(integer_sdd, x_and_prime, sisyphus);
				}
				v_r++;
				v_w++;
			} else if ((v_r < rel_ll->r_k && v_w < rel_ll->w_k && rel_ll->r_proj[v_r] < rel_ll->w_proj[v_w]) ||
					    v_w >= rel_ll->w_k) {
				// Variable is read but not written
				for (int i=xstatebits - 1; i>=0; i--) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, src[v_r] & (1 << i), 1), sisyphus);
					integer_sdd = sdd_conjoin(integer_sdd, x_and_prime, sisyphus);
				}
				v_r++;
			} else {
				// Variable is written but not read
				if (cpy && cpy[v_w]) {
					for (int i=xstatebits - 1; i>=0; i--) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						copy_literal = sdd_disjoin(sdd_conjoin(getLiteral(var, 0, 0), getLiteral(var, 0, 1), sisyphus),
												   sdd_conjoin(getLiteral(var, 1, 0), getLiteral(var, 1, 1), sisyphus), sisyphus);
						integer_sdd = sdd_conjoin(integer_sdd, copy_literal, sisyphus);
					}
				} else {
					for (int i=xstatebits - 1; i>=0; i--) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						integer_sdd= sdd_conjoin(integer_sdd, getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					}
				}
				v_w++;
			}
			src_and_dst = sdd_conjoin(src_and_dst, integer_sdd, sisyphus);
		}
		if (rel_update_smart_i + 1 == rel_update_smart_cache_size) {
//			 add the list to rel_update_smart_temp
			for (int b=0; b<6; b++) {
				for (unsigned int i=0; i<rel_update_smart_cache_size; i += (1 << (b+1))) {
					rel_update_smart_cache[i] = sdd_disjoin(rel_update_smart_cache[i], rel_update_smart_cache[i+(1<<b)], sisyphus);
				}
			}
			rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, rel_update_smart_cache[0], sisyphus);
			rel_update_smart_i = 0;
		}
		rel_update_smart_cache[rel_update_smart_i] = src_and_dst;
		rel_update_smart_i++;
		break;
	case 8: // Same as 7, but add the integers in reverse order
		src_and_dst = sdd_manager_true(sisyphus);
		v_r = rel_ll->r_k-1;
		v_w = rel_ll->w_k-1;
		while (v_r >= 0 || v_w >= 0) {
			integer_sdd = sdd_manager_true(sisyphus);
			if (v_r >= 0 && v_w >= 0 && rel_ll->r_proj[v_r] == rel_ll->w_proj[v_w]) {
				// Variable is both read and written
				for (int i=xstatebits - 1; i>=0; i--) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					integer_sdd = sdd_conjoin(integer_sdd, x_and_prime, sisyphus);
				}
				v_r--;
				v_w--;
			} else if ((v_r >= 0 && v_w >= 0 && rel_ll->r_proj[v_r] > rel_ll->w_proj[v_w]) ||
					    v_w < 0) {
				// Variable is read but not written
				for (int i=xstatebits - 1; i>=0; i--) {
					var = xstatebits*rel_ll->r_proj[v_r] + i + 1;
					x_and_prime = sdd_conjoin(getLiteral(var, src[v_r] & (1 << i), 0), getLiteral(var, src[v_r] & (1 << i), 1), sisyphus);
					integer_sdd = sdd_conjoin(integer_sdd, x_and_prime, sisyphus);
				}
				v_r--;
			} else {
				// Variable is written but not read
				if (cpy && cpy[v_w]) {
					for (int i=xstatebits - 1; i>=0; i--) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						copy_literal = sdd_disjoin(sdd_conjoin(getLiteral(var, 0, 0), getLiteral(var, 0, 1), sisyphus),
												   sdd_conjoin(getLiteral(var, 1, 0), getLiteral(var, 1, 1), sisyphus), sisyphus);
						integer_sdd = sdd_conjoin(integer_sdd, copy_literal, sisyphus);
					}
				} else {
					for (int i=xstatebits - 1; i>=0; i--) {
						var = xstatebits*rel_ll->w_proj[v_w] + i + 1;
						integer_sdd= sdd_conjoin(integer_sdd, getLiteral(var, dst[v_w] & (1 << i), 1), sisyphus);
					}
				}
				v_w--;
			}
			src_and_dst = sdd_conjoin(src_and_dst, integer_sdd, sisyphus);
		}
		if (rel_update_smart_i + 1 == rel_update_smart_cache_size) {
//			 add the list to rel_update_smart_temp
			for (int b=0; b<6; b++) {
				for (unsigned int i=0; i<rel_update_smart_cache_size; i += (1 << (b+1))) {
					rel_update_smart_cache[i] = sdd_disjoin(rel_update_smart_cache[i], rel_update_smart_cache[i+(1<<b)], sisyphus);
				}
			}
			rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, rel_update_smart_cache[0], sisyphus);
			rel_update_smart_i = 0;
		}
		rel_update_smart_cache[rel_update_smart_i] = src_and_dst;
		rel_update_smart_i++;
		break;
	default:
		Warning(info ,"Warning: feature vtree-increment=%u is not supported. Please use a number in 1..8.\n", vtree_increment_config);
		break;
	}
//	rel_increment_time += (double)(clock() - before);
}

static void rel_add_act(vrel_t rel, const int* src, const int* dst, const int* cpy, const int act) {
	rel_add_cpy(rel, src, dst, cpy);
	dummy_int = act;
}

static void rel_add(vrel_t rel, const int* src, const int* dst) {
	rel_add_cpy(rel, src, dst, 0);
}


// A compact SDD set enumerator
void small_enum(vset_t src) {
    Printf(infoLong, "{");
	if (sdd_node_is_false(src->sdd)) {
		Printf(infoLong, " }");
		return;
	}
	// print in blocks of 5x5
	SddModelCount mc = set_count_exact(src);
	if (mc > 5) {
	    Printf(infoLong, "\n    ");
	}
	SddModelCount n = 0;
	unsigned int x;
	vset_ll_t src_ll = get_vset(src->id);
	unsigned int k = (src_ll->k == -1 ) ? src->dom->vectorsize : src_ll->k;
	struct sdd_mit_master mas;
	for (mas = sdd_get_iterator(src); mas.finished == 0; sdd_next_model(&mas)) {
		n++;
		Printf(infoLong, "    set_e(e,");
		for (unsigned int i=0; i<k; i++) {
			x = 0;
			for (int b=0; b<xstatebits; b++) {
				x |= (mas.e[i*xstatebits + b]) << b;
			}
			Printf(infoLong, "%u", x);
			if (i + 1 < k) {
			    Printf(infoLong, ",");
			}
		}
		Printf(infoLong, ")\n    ");
	}
	sdd_mit_free(mas);
	Printf(infoLong, " }");
	if (n != mc) {
	    Printf(infoLong, "\n\n  That's strange. Enumerated %llu models out of %llu.\n", n, mc);
		sdd_save_as_dot("small_enum_error.dot", src->sdd);
		enum_error = 1;
	}
}

/* src and dst are arrays of integer values
 */
static void rel_update(vrel_t dst, vset_t src, vrel_update_cb cb, void* context) {
	static unsigned int ncalls = 0; ncalls++;
	if (sdd_node_is_false(src->sdd)) {
//		Printf(info, "  [Sdd rel update] Source has no models. Abort.\n");
		return;
	}
	if (dst != 0 && dst->sdd == 0) {
		Warning(info, "Error in SDD rel_update. dst->sdd == 0. &dst == %p\n", dst->sdd);
		dst->sdd = sdd_manager_false(sisyphus);
	}
	else if (dst == 0) {
		Warning(info, "[rel update] ERROR    dst == 0\n");
	}
	vrel_ll_t rel_ll = get_vrel(dst->id);
	SddNode* root = src->sdd;
	// Static allocation
	static int* e = 0;
	if (e == 0) {
		e = malloc(sizeof(int) * dst->dom->vectorsize);
	}
	if (vtree_increment_config == 6 || vtree_increment_config == 7 || vtree_increment_config == 8) {
		for (unsigned int i=0; i<rel_update_smart_cache_size; i++) {
			rel_update_smart_cache[i] = sdd_manager_false(sisyphus);
		}
	}
	// For each model of src
	//     Call the callback to find the new states
	//     Add the new states to dst
	if (sdd_node_is_decision(root)) {
		//printf("  [rel update] Node is decision node.\n");
//		clock_t before = clock();
		int d;
		rel_update_smart_temp = sdd_manager_false(sisyphus);
		struct sdd_mit_master mas;
		for (mas = sdd_get_iterator(src); mas.finished == 0; sdd_next_model(&mas)) {
			// Refactor mas.e to a bit-array
			for (int i=0; i<rel_ll->r_k; i++) {
				e[i] = 0; // TODO put this in the next loop
			}
			for (int i=0; i<rel_ll->r_k; i++) {
				for (int b=0; b<xstatebits; b++) {
					d = (mas.e[xstatebits*(rel_ll->r_proj[i])+b]) << b;
					//printf("  e[%i] |= %i\n", i, d);
					e[i] |= d;
				}
			}
			nNextState_cb++;
//			clock_before_nscb = clock();
			cb(dst, context, e);
//			printf("  [rel update] Did the callback. Now relation has %llu models.\n", sdd_model_count(dst->sdd, sisyphus));
		}
		sdd_mit_free(mas);
		if (vtree_increment_config == 6 || vtree_increment_config == 7 || vtree_increment_config == 8) {
			if (rel_update_smart_i != 0) {
				// An update has occurred
				if (rel_update_smart_i == 1) {
//					clock_t before_inc = clock();
					rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, rel_update_smart_cache[0], sisyphus);
					SddNode* disjoin = sdd_disjoin(dst->sdd, rel_update_smart_temp, sisyphus);
//					clock_t after_inc = clock();
//					rel_increment_time += (double)(after_inc - before_inc);
//					rel_update_time += (double)(after_inc - before);
					sdd_set_rel_and_ref(dst, disjoin);
				} else {
//					 There is still stuff left over in the cache
//					 add the list to rel_update_smart_temp
//					clock_t before_inc = clock();
					for (int b=0; b<6; b++) {
						for (unsigned int i=0; i<rel_update_smart_cache_size; i += (1 << (b+1))) {
							if (sdd_node_is_false(rel_update_smart_cache[i+(1<<b)])) {
								continue;
							}
							rel_update_smart_cache[i] = sdd_disjoin(rel_update_smart_cache[i], rel_update_smart_cache[i+(1<<b)], sisyphus);
						}
					}
					rel_update_smart_temp = sdd_disjoin(rel_update_smart_temp, rel_update_smart_cache[0], sisyphus);
					SddNode* disjoin = sdd_disjoin(dst->sdd, rel_update_smart_temp, sisyphus);
//					rel_increment_time += (double)(clock() - before_inc);
//					rel_update_time += (double)(clock() - before);
					sdd_set_rel_and_ref(dst, disjoin);
				}
			}
		}
		else if (!sdd_node_is_false(rel_update_smart_temp)) {
//			clock_t before_inc = clock();
			SddNode* disjoin = sdd_disjoin(dst->sdd, rel_update_smart_temp, sisyphus);
//			rel_increment_time += (double)(clock() - before_inc);
//			rel_update_time += (double)(clock() - before);
			sdd_set_rel_and_ref(dst, disjoin);
		}
		else {
//			rel_update_time += (double)(clock() - before);
		}
//		printf("  [rel update] End of models.\n");
	}
	else if (sdd_node_is_false(root)) {
		Warning(info, "Unexpected event in SDD rel update. Relation is False.\n");
		return;
	}
	else if (sdd_node_is_true(root)) {
		Warning(info, "Unexpected event in SDD rel update. Relation is True.\n");
		return;
	}
	else {
		Warning(info, "Unexpected event in SDD rel update. Node is literal. Skipping.\n");
	}

}

static void set_least_fixpoint(vset_t dst, vset_t src, vrel_t _rels[], int rel_count) {
	Warning(info, "set_least_fixpoint Warning: Not implemented.\n");
	dummy = dst;
	dummy = src;
	dummy = _rels;
	dummy_int = rel_count;
}

static void set_least_fixpoint_par(vset_t dst, vset_t src, vrel_t _rels[], int rel_count) {
	Warning(info, "set_least_fixpoint_par Warning: Not implemented.\n");
	dummy = dst;
	dummy = src;
	dummy = _rels;
	dummy_int = rel_count;
}

static void set_reorder() {
	// We use our own reorder schedule
}

/* Indicates that our package distinguishes read operations from write operations
 */
static int separates_rw() {
	return sdd_separate_rw; // cmd line paramter
}

static void dom_set_function_pointers(vdom_t dom) {
    Printf(infoLong, "[Sdd]  Setting function pointers.\n");
	//   Sets
	dom->shared.set_create = set_create;
	dom->shared.set_destroy = set_destroy;
	dom->shared.set_add = set_add;
	dom->shared.set_update = set_update;
	dom->shared.set_member = set_member;
	dom->shared.set_is_empty = set_is_empty;
	dom->shared.set_equal = set_equal;
	dom->shared.set_clear = set_clear;
	dom->shared.set_enum = set_enum;
	//dom->shared.set_enum_match = set_enum_match
	dom->shared.set_copy = set_copy;
	//dom->shared.set_copy_match
	dom->shared.set_example = set_example;
	dom->shared.set_union = set_union;
	dom->shared.set_intersect = set_intersect;
	dom->shared.set_minus = set_minus;
	dom->shared.set_zip = set_zip;
	dom->shared.set_project = set_project;
	dom->shared.set_count = set_count;

	//   Relations
	dom->shared.rel_create_rw = rel_create_rw;
	dom->shared.rel_add_act = rel_add_act;
	dom->shared.rel_add_cpy = rel_add_cpy;
	dom->shared.rel_add = rel_add;
	dom->shared.rel_update = rel_update;
	dom->shared.rel_count = rel_count;
	dom->shared.set_next = set_next;
	dom->shared.set_prev = set_prev;
	dom->shared.set_least_fixpoint = set_least_fixpoint;
	dom->shared.set_least_fixpoint_par = set_least_fixpoint_par;

	dom->shared.reorder = set_reorder;
	//dom->shared.set_dot = set_dot;
	//dom->shared.rel_dot = rel_dot;

	//dom->shared.dom_save = dom_save;
	//dom->shared.set_save = set_save;
	//dom->shared.set_load = set_load;
	//dom->shared.rel_save_proj = rel_save_proj;
	//dom->shared.rel_save = rel_save;
	//dom->shared.rel_load_proj = rel_load_proj;
	//dom->shared.rel_load = rel_load;
	dom->shared.rel_destroy = rel_destroy;

	dom->shared.separates_rw = separates_rw;
}

void ltsmin_initialise_sdd() {
	// Initialising sisyphus is done in sdd_initialise_given_rels()
	sdd_exploration_start = clock();
	if (vtree_increment_config == 6 || vtree_increment_config == 7 || vtree_increment_config == 8) {
		rel_update_smart_cache = malloc(sizeof(SddNode*) * rel_update_smart_cache_size);
	}
}

vdom_t dom_create(int vectorsize, int* _statebits, int actionbits) {
//	printf("[dom create]  Creating domain.\n");
	ltsmin_initialise_sdd();

	// Create the domain data structure
	vdom_t dom = (vdom_t) malloc(sizeof(struct vector_domain));
	vdom_init_shared(dom, vectorsize);
	dom_set_function_pointers(dom);

	// Initialize domain
	dom->vectorsize = vectorsize;
	dom->statebits = (int*) malloc(sizeof(int[vectorsize]));
	memcpy(dom->statebits, _statebits, sizeof(int[vectorsize]));
	dom->actionbits = actionbits;

	dom->totalbits = 0;
	for (int i=0; i<vectorsize; i++) {
		dom->totalbits += _statebits[i];
	}

	// Create state variables and prime variables
	dom->state_variables = malloc(sizeof(int[vectorsize * xstatebits]));
	dom->prime_variables = malloc(sizeof(int[vectorsize * xstatebits]));
    Printf(infoLong, "  Allocated memory for state and prime variables.\n");
	int i_var = 0;
	uint32_t curvar = 0;
	for (int i=0; i<vectorsize; i++) {
		for (int j=0; j<_statebits[i]; j++) {
			dom->state_variables[i_var] = curvar;
			curvar++;
			dom->prime_variables[i_var] = curvar;
			i_var++;
			curvar ++;
		}
	}

	return dom;
}

vdom_t vdom_create_sdd(int n) {
	int _statebits[n];
	for (int i=0; i<n; i++) {
		_statebits[i] = xstatebits;
	}
	return dom_create(n, _statebits, 0);
}

