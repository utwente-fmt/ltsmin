// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef LTS_H
#define LTS_H

#include <sys/types.h>

#include <lts-io/user.h>
#include <ltsmin-lib/lts-type.h>
#include <util-lib/bitset.h>
#include <util-lib/treedbs.h>

/**
\file lts-lib/lts.h
\brief User interface to the LTS manipulation library.

This library provides functionality for manipulating an LTS,
which has been loaded into memory.

*/

/**
The possible memory layout of the LTS.
 */
typedef enum {
    /** The list layout stores the transitions of the LTS
        in the arrays src, dest and optionally label.
        This layout allows easy enumeration over all
        transitions.
     */
    LTS_LIST,
    /** The block layout stores the transitions of the LTS
        in the arrays begin, dest and optionally label.
        This layout allows easy enumeration of the successors
        of a state.
     */
    LTS_BLOCK,
    /** The inverse block layout stores the transitions of the LTS
        in the arrays begin, src and optionally label.
        This layout allows easy enumeration of the predecessors
        of a state.
     */
    LTS_BLOCK_INV} LTS_TYPE;

typedef struct lts {
    lts_type_t ltstype; //@< contains the signature of the LTS.
    value_table_t *values; //@< value tables for all types.
    LTS_TYPE type;	
    uint32_t root_count; //@< number of initial states
    uint32_t *root_list; //@< array of initial states
    uint32_t transitions;
    uint32_t states;
    treedbs_t state_db; // used if state vector length is positive.
    uint32_t *properties; // contains state labels
    treedbs_t prop_idx; // used if there is more than one state label
    uint32_t *begin;
    uint32_t *src;
    uint32_t *label; // contains edge labels
    treedbs_t edge_idx; // used if there is more than one edge label
    uint32_t *dest;
    int32_t tau;
} *lts_t;

/**
 Set the signature or LTS type of the lts.
 This function creates value tables for each of the chunk types in the LTS type.
 */
extern void lts_set_sig(lts_t lts,lts_type_t type);

/**
 Set the signature or LTS type of the lts.
 This function uses the given value tables.
 */
extern void lts_set_sig_given(lts_t lts,lts_type_t type,value_table_t *values);

/**
 Create a new uninitialized LTS.
 */
extern lts_t lts_create();

/**
 Destroy the given LTS.
 Note that the stored LTS type and value table are not destroyed.
 */
extern void lts_free(lts_t lts);

/**
 Set the layout of the LTS.
 This function will recompute a new layout if necessary.
 The order of the transitions may be changed.
 The state numbers remain the same.
 */
extern void lts_set_type(lts_t lts,LTS_TYPE type);

/**
 Change the allocated size of the given lts.
 */
extern void lts_set_size(lts_t lts,uint32_t roots,uint32_t states,uint32_t transitions);

/**
 Sort the transitions in the given LTS and remove duplicate transitions.
 This function is currently not kripke frame proof.
 */
extern void lts_uniq(lts_t lts);

/**
 Sort the transitions in the given LTS.
 The order is lexicographic (label,dest).
 Ensures that the LTS is in block layout.
 */
extern void lts_sort(lts_t lts);

/**
 Sort the transitions in the given LTS.
 The order is lexicographic (dest,label).
 Ensures that the LTS is in block layout.
 */
extern void lts_sort_dest(lts_t lts);

/**
 Renumbers the states in the order in which they are found during
 a BFS search.
 */
extern void lts_bfs_reorder(lts_t lts);

/**
 Renumbers the states in random order.
 */
extern void lts_randomize(lts_t lts);

/**
\brief Predicate type for determining silent steps.
 */
typedef int(*silent_predicate)(void*context,lts_t lts,uint32_t src,uint32_t edge,uint32_t dest);

/**
\brief Eliminate silent cycles from the given LTS.
 */
extern void lts_silent_cycle_elim(lts_t lts,silent_predicate silent,void*context,bitset_t diverging);

/**
\brief Tau edge predicate.
*/
extern int tau_step(void*context,lts_t lts,uint32_t src,uint32_t edge,uint32_t dest);

#define lts_tau_cycle_elim(lts) lts_silent_cycle_elim(lts,tau_step,NULL,NULL)

/**
\brief Stuttering step predicate.
 */
extern int stutter_step(void*context,lts_t lts,uint32_t src,uint32_t edge,uint32_t dest);

/**
\brief Compress silent steps in LTS.

This is tau*a equialence for an edge label LTS.
 */
extern void lts_silent_compress(lts_t lts,silent_predicate silent,void*silent_context);

/**
\brief Reduce the given lts modulo weak bisimulation.

This is a port of the mCRL 1 minimization.
 */
extern void setbased_weak_reduce(lts_t lts);

/**
 Find the set of divergent states.
 */
extern void lts_find_divergent(lts_t lts,silent_predicate silent,void*silent_context,bitset_t divergent);

/**
 Compute the set of deadlocks of the LTS.
 */
extern void lts_find_deadlocks(lts_t lts,bitset_t deadlocks);


/**
\brief Determinize the given LTS.
*/
extern void lts_mkdet(lts_t lts);

/**
 Open the given file and write the results to the given lts.
 The given LTS must be uninitialized.
 */
extern void lts_read(char *name,lts_t lts);

/**
 Write the given LTS to the given file. The file will
 have the given number of segments.
 */
extern void lts_write(char *name,lts_t lts,string_set_t filter,int segments);

/**
 Read a Markov chain in TRA/LAB format given the name of the TRA file.
 */
extern void lts_read_tra(const char*tra,lts_t lts);

/**
 Write a Markov chain in TRA/LAB format given the name of the TRA file.
 */
extern void lts_write_tra(const char*tra,lts_t lts);


/**
 Write a Markov Automaton in IMCA format.
 */
extern void lts_write_imca(const char*imca,lts_t lts);

 
/**
 Merge the second LTS into the first and destroy the second LTS.
 */
extern void lts_merge(lts_t lts1,lts_t lts2);

/**
 Encode arbitrary LTS as a single root, single edge label LTS.
 This step prepares arbitrary LTSs to be written in AUT format.
 */
extern lts_t lts_encode_edge(lts_t lts);

/**
\brief Create a write interface for an LTS.

This allows initializing an LTS using the LTS-IO API.
*/
lts_file_t lts_writer(lts_t lts,int segments,lts_file_t settings);

/**
\brief Create a read interface for an LTS.

This allows reading an LTS using the LTS-IO API.
*/
lts_file_t lts_reader(lts_t lts,int segments,lts_file_t settings);


/**
\brief Get the value of a state label.
*/
extern uint32_t lts_state_get_label(lts_t lts,uint32_t state_no,uint32_t label_no);


/**
\brief Create a copy of a LTS.
*/
extern lts_t lts_copy(lts_t orig);

#endif

