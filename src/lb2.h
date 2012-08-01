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

#ifndef LB2_H_
#define LB2_H_

typedef struct lb2_s lb2_t;

typedef enum { LB2_Static,   //Statically partition results of an initial run
               LB2_SRP,      //Synchronized random polling
               LB2_Combined, //start static switch to SRP when unbalanced
               LB2_None      //Non-interfering with the search algorithm
} lb2_method_t;

typedef struct lb2_inlined_s {
    size_t             mask;
    int                stopped;
} lb2_inlined_t;

static const size_t LB2_MAX_THREADS = (sizeof (uint64_t) * 8);
static const size_t LB2_MAX_HANDOFF_DEFAULT = 100;

typedef size_t      (*lb2_split_problem_f) (void *source, void *target,
                                            size_t handoff);

extern lb2_t *lb2_create (size_t threads, size_t gran);
extern lb2_t *lb2_create_max (size_t threads, size_t gran, size_t max);

/**
 \brief Records thread local data and performs initial exploration for static
        load-balancing (also: functions as a barrier). 
 */
extern void lb2_local_init (lb2_t *lb, int id, void *arg);

extern void lb2_destroy (lb2_t *lb);

extern size_t lb2_internal (lb2_t *lb, int my_id, size_t my_load,
                            lb2_split_problem_f split);

#ifndef atomic_read
#define atomic_read(v)      (*(volatile typeof(*v) *)(v))
#endif

static inline int
lb2_is_stopped (lb2_t *lb)
{
    lb2_inlined_t *inlined = (lb2_inlined_t *)lb;
    return atomic_read (&inlined->stopped);
}

static inline size_t
lb2_balance (lb2_t *lb, int id, size_t my_load, lb2_split_problem_f split)
{
    if (lb2_is_stopped(lb))
        return 0;
    lb2_inlined_t *inlined = (lb2_inlined_t *)lb;
    if (my_load > 0 && (my_load & inlined->mask) != inlined->mask )
        return my_load;
    return lb2_internal (lb, id, my_load, split);
}

extern int lb2_stop (lb2_t *lb);

extern int lb2_is_stopped (lb2_t *lb);

/**
 * requires barrier before!
 */
extern void lb2_reinit (lb2_t *lb, size_t id);

typedef enum { LB2_BARRIER_MASTER, LB2_BARRIER_SLAVE } lb2_barrier_result_t;

extern lb2_barrier_result_t lb2_barrier (lb2_t *lb);

extern size_t lb2_reduce (lb2_t *lb, size_t val);

extern size_t lb2_max_load (lb2_t *lb);

#endif /* LB2_H_ */
