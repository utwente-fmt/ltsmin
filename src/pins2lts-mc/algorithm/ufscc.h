/*
 * Implementation of a multi-core Union-Find based SCC algorithm
 *
 * For a high-level description of the algorithm, we refer to REPORT.
 *
 * Main differences from the implementation and the report are as follows:
 *
 * Instead of a recursive implementation, we make use of a search_stack to
 * track the search order. Since the algorithm is based on depth-first search,
 * we use stackframes to denote levels in the stack (successors of a state are
 * pushed on a new stackframe).
 *
 *
 * TODO: add REPORT
 *
 */

#ifndef UFSCC_H
#define UFSCC_H

#include <pins2lts-mc/algorithm/algorithm.h>

#endif // UFSCC_H
