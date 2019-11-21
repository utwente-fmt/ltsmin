
/**
 * Leaping POR
 *
 * Using some POR search method, compute multiple independent stubborn sets and
 * schedule all their interleavings at once.
 *
 * Independent sets are found by search for disjoint stubborn sets. As
 * disjointness implies independence, by the fact that outside transitions
 * always commute with transitions inside the stubborn set.
 *
 * "Scheduling multiple interleavings at once" is done by taking the Cartesian
 * product of all disjoint sets, and atomically executing the resulting vectors
 * (i.e. not returning the intermediate steps).
 *
 */

#ifndef LEAP_POR
#define LEAP_POR


#include <pins-lib/por/pins2pins-por.h>


typedef struct leap_s leap_t;

extern leap_t      *leap_create_context (model_t por_model, model_t pre_por,
                                         next_method_black_t next_all);

extern bool         leap_is_stubborn (por_context *ctx, int group);

extern int          leap_search_all (model_t self, int *src, TransitionCB cb,
                                     void *user_context);

extern void         leap_add_leap_group (model_t por_model, model_t pre_por);

extern void         leap_stats (model_t model);

#endif
