/*
 * lb.h
 *
 * A Load-balancer based on Synchronous Random Polling (SRP) as described by
 * Peter Sanders in "Lastkraftverteilungsalgorithmen".
 * Added optimizations for shared-memory systems:
 * - Random polling changed to informed polling (highest load)
 * - N-ary balancing with a back-off policy to avoid useless waiting
 *
 *  Created on: Jun 10, 2010
 *      Author: laarman
 */

#ifndef LB_H_
#define LB_H_

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>


typedef struct lb_s lb_t;

typedef enum { lb_Static,   //Statically partition results of an initial run
               lb_SRP,      //Synchronized random polling
               lb_Combined, //start static switch to SRP when unbalanced
               lb_None      //Non-interfering with the search algorithm
} lb_method_t;

typedef struct lb_inlined_s {
    size_t             mask;
    int                stopped;
} lb_inlined_t;

static const size_t lb_MAX_THREADS = (sizeof (uint64_t) * 8);
static const size_t lb_MAX_HANDOFF_DEFAULT = 100;

// return negative number when handoff is instead copied
typedef ssize_t      (*lb_split_problem_f) (void *source, void *target,
                                            size_t handoff);

extern lb_t *lb_create (size_t threads, size_t gran);
extern lb_t *lb_create_max (size_t threads, size_t gran, size_t max);

/**
 \brief Records thread local data and performs initial exploration for static
        load-balancing (also: functions as a barrier). 
 */
extern void lb_local_init (lb_t *lb, int id, void *arg);

extern void lb_destroy (lb_t *lb);

extern size_t lb_internal (lb_t *lb, int my_id, size_t my_load,
                           lb_split_problem_f split);

#ifndef atomic_read
#define atomic_read(v)      (*(volatile typeof(*v) *)(v))
#endif

static inline int
lb_is_stopped (lb_t *lb)
{
    lb_inlined_t *inlined = (lb_inlined_t *)lb;
    return atomic_read (&inlined->stopped);
}

static inline size_t
lb_balance (lb_t *lb, int id, size_t my_load, lb_split_problem_f split)
{
    if (lb_is_stopped(lb))
        return 0;
    lb_inlined_t *inlined = (lb_inlined_t *)lb;
    if (my_load > 0 && (my_load & inlined->mask) != inlined->mask )
        return my_load;
    return lb_internal (lb, id, my_load, split);
}

extern int lb_stop (lb_t *lb);

/**
 * requires barrier before!
 */
extern void lb_reinit (lb_t *lb, size_t id);

typedef enum { lb_BARRIER_MASTER, lb_BARRIER_SLAVE } lb_barrier_result_t;

extern lb_barrier_result_t lb_barrier (lb_t *lb);

extern size_t lb_reduce (lb_t *lb, size_t val);

extern size_t lb_max_load (lb_t *lb);

#endif /* LB_H_ */
