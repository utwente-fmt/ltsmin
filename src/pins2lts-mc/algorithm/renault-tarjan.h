/*
 * Renault's parallel SCC algorithm (*) (for parallel SCC algorithm comparison)
 *
 * (*) We implemented a variant of Renault's algorithm for parallel emptiness
 * checks that solely explores SCCs and does not regard acceptance conditions.
 *
 * The implementation is very similar to that of tarjan-scc. The only
 * difference is that fully completed SCCs are communicated via a union-find
 * structure that globally marks a state DEAD (instead of using the SCC_STATE
 * color in the global state storage that the Tarjan implementation uses).
 * Multiple spawns of this algorithm (by utilizing permuted successor
 * orderings) can simultaneously aid each other by communicating completely
 * explored SCCs.
 *
 * One subtle difference with Renault's algorithm is that a DEAD state is
 * represented with a status value (instead of uniting an SCC with a DEAD
 * state). We chose for this implementation to reduce the number of cache line
 * bounces: if otherwise we unite all explored SCCs to a single set - with a
 * single state as the representative - all checks for DEAD SCCs will direct to
 * that state.
 *
 *
 * Parallel Explicit Model Checking for Generalized {B\"u}chi Automata
 *
 * @inproceedings{renault.15.tacas,
     author    = {Etienne Renault and Alexandre Duret-Lutz and Fabrice
                  Kordon and Denis Poitrenaud},
     title     = {{Parallel Explicit Model Checking for Generalized {B\"u}chi
                  Automata}},
     booktitle = {Proceedings of the 19th International Conference on Tools
                  and Algorithms for the Construction and Analysis of Systems
                  (TACAS'15)},
     editor    = {Christel Baier and Cesare Tinelli},
     year      = 2015,
     month     = apr,
     pages     = {613--627},
     publisher = {Springer},
     series    = {Lecture Notes in Computer Science},
     volume    = 9035,
  }
 *
 */

#ifndef RENAULT_TARJAN_SCC_H
#define RENAULT_TARJAN_SCC_H

#include <pins2lts-mc/algorithm/ltl.h>

#endif // RENAULT_TARJAN_SCC_H
