/*
 * Implementation of a multi-core Union-Find based SCC algorithm (UFSCC)
 *
 * @inproceedings{ppopp16,
        author = {Bloemen, Vincent and Laarman, Alfons and van de Pol, Jaco},
        title = {Multi-core On-the-fly SCC Decomposition},
        booktitle = {Proceedings of the 21st ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming},
        series = {PPoPP '16},
        year = {2016},
        isbn = {978-1-4503-4092-2},
        pages = {8:1--8:12},
        doi = {10.1145/2851141.2851161},
        publisher = {ACM},
    }
 *
 * Main differences from the implementation and the report are as follows:
 *
 * Instead of a recursive implementation, we make use of a search_stack to
 * track the search order. Since the algorithm is based on depth-first search,
 * we use stackframes to denote levels in the stack (successors of a state are
 * pushed on a new stackframe).
 *
 *
 */

#ifndef UFSCC_H
#define UFSCC_H

#include <pins2lts-mc/algorithm/algorithm.h>

#endif // UFSCC_H
