#ifndef ALG_LOCAL_H
#define ALG_LOCAL_H

#include <vset-lib/vector_set.h>


/**
 * Performs local exploration of the system's components (groups) without
 * constructing the exact reachable set. Only reachable sets for each
 * components read support are maintained. Their writes are then projected to
 * all reading component's. Taken together these component sets overestimate
 * the reachable state space.
 *
 * NOTE: This requires a proper next-state _function_.
 *
 * Uses:
 * --- Speedier PINS transition learning
 * --- CEGAR
 * --- Extraction POR relations (which can be overestimated)
 */

extern void run_local (vset_t I, vset_t V);

#endif //ALG_LOCAL_H
