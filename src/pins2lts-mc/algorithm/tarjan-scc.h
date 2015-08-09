/**
 * Tarjan's sequential SCC algorithm (baseline for SCC algorithm comparison)
 *
 *
 * A depth-first search is performed with the search_stack, which is a stack
 * that uses stackframes to keep track of the search depth. The successors of a
 * state are stored in a new stackframe and explored in a DFS manner. When a
 * back-edge is found, the state will be transferred to the tarjan_stack.
 *
 * The tarjan_stack is used to keep track of already explored states that are
 * part of a LIVE SCC (meaning that at least one state from the SCC is still in
 * the search_stack). When backtracking from the root of the SCC, all states in
 * the tarjan_stack (with lowlink >= root.lowlink) are popped.
 *
 * The visited_states set is used to detect which states have already been
 * explored. This set also contains a pointer to the stack location for the
 * state (either to search_stack or tarjan_stack).
 *
 * The global state storage contains a color bit that indicates whether or not
 * the state is part of a completely explored SCC (with SCC_STATE).
 *
 */

#ifndef TARJAN_SCC_H
#define TARJAN_SCC_H

#include <pins2lts-mc/algorithm/algorithm.h>

#endif // TARJAN_SCC_H
