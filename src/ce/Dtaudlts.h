
#ifndef TAUDLTS_H
#define TAUDLTS_H

#include "Ddlts.h"

#include <sys/types.h>
#include <mpi.h>

// in all types and functions
// i made the hypothesis that 
// the number of segments = the number of workers in _comm_

// most of the functions are collective operations

typedef struct taudlts {
 MPI_Comm comm;
 int N;
 int M;
 int* begin;
 int* w;
 int* o; 
} *taudlts_t;
//  the structure contains only fwd trans or only back trans.
// w[i], o[i] = the (worker, offset) pair for trans. i
// incoming trans: ordered by destination & srcworker
// outgoing trans: ordered by source & destworker


taudlts_t taudlts_create(MPI_Comm communicator);
void taudlts_free(taudlts_t t);
void taudlts_copy(taudlts_t tfrom, taudlts_t tto);
void taudlts_reset(taudlts_t t);
void taudlts_simple_join(taudlts_t t1, taudlts_t t2);
void taudlts_aux2normal(taudlts_t t);
void taudlts_normal2aux(taudlts_t t);
void taudlts_delete_transitions(taudlts_t t, char* deleted);

void taudlts_extract_from_dlts(taudlts_t t, dlts_t lts);
// extracts the FORWARD tau transitions from a given dlts
// and orders them 
// The resulted taults has ALL the states and ALL the tau transitions of the dlts.
// The resulte dlts has ONLY the visible actions of the input dlts.
// TO DO (?): renumber the states and give a map dlts->taudlts

/*
 DON'T KNOW IF REALLY NEEDED
void taudlts_is_dlts_without_labels(taudlts_t t, dlts_t lts);
*/


void taudlts_insert_to_dlts(taudlts_t t, dlts_t lts);


void taudlts_load(taudlts_t t, char* filename, int type);
// reads actually a dlts, but only remembers the tau transitions..

void taudlts_write(taudlts_t t, char* filename);
// dumps t

void taudlts_fwd2back(taudlts_t t);
// transform the fwd representation of the LTS
// (i.e.: for all states, know the outgoing transitions)
// to the backward representation
// and o.w.a
// States distribution to the workers remains unchanged.

void taudlts_elim_trivial(taudlts_t t, taudlts_t ta, int* oscc);
// topological sort
// input: t
// output: t without (some of the) transitions not belonging to a cycle
//         ta = (some) transitions of t not belonging to a cycle 
// All states are kept.


void taudlts_cleanup(taudlts_t t, int* wscc, int* oscc);
// transforms x->y in scc(x)-> scc(y)
// then sorts t
// then eliminates x->x and doubles

void taudlts_printinfo(taudlts_t t, int* oscc);


void taudlts_elim_small(taudlts_t t, int* wscc, int* oscc);

void taudlts_scc_local(taudlts_t t, int* oscc);
// input: t
// output: t without the transitions belonging to a LOCAL cycle
// output: map states -> their (local) scc head

void taudlts_local_collapse(taudlts_t t, int* map);
// input: t, map
// output: t where the transitions x-> y become map[x] -> map[y]

void taudlts_set_entry_exit(taudlts_t t, char* ee);
// input: t
// output: ee[x] = 3 if x is both entry and exit; 
//                 2, only exit, 1 only entry, 0 nothing

void taudlts_to_virtual(taudlts_t t, char* ee, taudlts_t tabs);
// input: t, ee
// output: tabs 
// a transition x->y in tabs means:
// x is entry, y is exit and there's a local path from x to y

void taudlts_scc_global(taudlts_t t, int* wscc, int* oscc);
// input: t
// output: t without the transitions belonging to a GLOBAL cycle
// output: map states -> their global scc head

void taudlts_to_real(taudlts_t tabs, char* ee, int* wscc, int* oscc, taudlts_t t);
// input: tabs, ee, t 
// input: wscc, oscc defined for the states in ee
// output: wscc, oscc defined for all states of t

void taudlts_global_collapse(taudlts_t t, int* wmap, int* omap);
// input: t, wmap, omap
// output: t where the transitions x-> y become wmap[x],omap[x] -> wmap[y], omap[y]


void taudlts_reduce_some(taudlts_t t, char* workers, int manager, 
												 int* wscc, int* oscc);
// input: all
//    - only "valid" states X with wscc[X]=me , oscc[X]=X  will be considered
//    - (I): the transitions in t must be only between valid states     
// output: 
//    - t modified (transitions found on cycles and doubles deleted)
//    - wscc, oscc modified to reflect the new SCC's found 
//    - t->w, t->o modified such that (I) still holds


void taudlts_scc_stabilize(taudlts_t t, int* wscc, int* oscc);



void dlts_shrink(dlts_t lts, int* wscc, int* oscc, int** weight);
// input: wscc, oscc
// output: lts only with nonempty states and RE-NUMBERED
//        weight, i.e. 0 for non-head states
//                      for head states: the number of states "owned"

void dlts_mark(dlts_t lts, int label, char* mark);
// input: a label 
// output: mark = 1 for all states with an outgoing transition "label"

void dlts_reachback(dlts_t lts, char* mark);
// input: mark =.. 0 for most states, 1 for a small set
// output: mark: 1 for all states reachable in t from the initial marked set



void dlts_shuffle(dlts_t lts, int* wmap, int* omap);

void dlts_clear_useless_transitions(dlts_t lts);




void print_status(taudlts_t t);
#endif
