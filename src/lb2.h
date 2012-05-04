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

#define MAX_HANDOFF_DEFAULT 100

typedef size_t      (*lb2_split_problem_f) (void *source, void *target,
                                            size_t handoff);

extern lb2_t *lb2_create (size_t threads, lb2_split_problem_f split,
                          size_t gran, lb2_method_t m);
extern lb2_t *lb2_create_max (size_t threads, lb2_split_problem_f split,
                              size_t gran, lb2_method_t method, size_t max);

/**
 \brief Records thread local data and performs initial exploration for static
        load-balancing (also: functions as a barrier). 
 */
extern void lb2_local_init (lb2_t *lb, int id, void *arg);

extern void lb2_destroy (lb2_t *lb);

extern size_t lb2_balance (lb2_t *lb, int my_id, size_t my_load);

extern int lb2_stop (lb2_t *lb);

extern int lb2_is_stopped (lb2_t *lb);

extern void lb2_reinit (lb2_t *lb, size_t id);

extern size_t lb2_reduce (size_t val, size_t W);

#endif /* LB2_H_ */
