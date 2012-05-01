
/**
 * Statistics gatherer
 *
 * Gathers statistical data on the fly.
 * Algorithm from [1], I trust the guy that this avoids rounding errors.
 *
 * [1] Mark Hoemmen - "Computing the standard deviation efficiently"
 * (http://www.cs.berkeley.edu/~mhoemmen/cs194/Tutorials/variance.pdf)
 *
 * I added statistics_unrecord to roll back recorded samples.
 * And statistics_union to combine two stat records.
 *
 * Alfons
 */

#ifndef STATISTICS_H
#define STATISTICS_H

#include <stdio.h>


typedef struct statistics_s {
    size_t k;  // sample count
    double Mk; // mean
    double Qk; // a quantity
} statistics_t;

extern void statistics_record (statistics_t *stats, double x);
extern void statistics_unrecord (statistics_t *stats, double x);
/**
 * Unionizes two stats. Maybe inline: s1 = out \/ s2 = out
 */
extern void statistics_union (statistics_t *out, statistics_t *s1, statistics_t *s2);
extern double statistics_mean (statistics_t *stats);
extern double statistics_stdev (statistics_t *stats);
extern size_t statistics_nsamples (statistics_t *stats);
extern double statistics_variance (statistics_t *stats);
extern double statistics_stdvar (statistics_t *stats);
extern void statistics_init (statistics_t *stats);

#endif // STATISTICS_H
