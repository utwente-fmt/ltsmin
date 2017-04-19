#ifndef MAXSUM_H
#define MAXSUM_H

#include <popt.h>

#include <ltsmin-lib/lts-type.h>
#include <vset-lib/vector_set.h>

extern struct poptOption maxsum_options[];

/**
 * Initialize maxsum subsystem, parses options,
 * create DDs etc.
 */
extern void init_maxsum(lts_type_t lts_type);

/**
 * Compute the maximum sum over variables in set.
 */
extern void compute_maxsum(vset_t set, vdom_t dom);

#endif

