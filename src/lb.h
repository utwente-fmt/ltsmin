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

typedef struct lb_s lb_t;

typedef enum { LB_Static,   //Statically partition results of an initial run
               LB_SRP,      //Synchronized random polling
               LB_Combined, //start static switch to SRP when unbalanced
               LB_None      //Non-interfering with the search algorithm
} lb_method_t;

#define MAX_HANDOFF_DEFAULT 100

typedef void        (*algo_f) (void * ctx, size_t granularity);
typedef size_t      (*split_problem_f) (void *source, void *target,
                                        size_t handoff);


extern lb_t *lb_create (size_t threads, algo_f alg,
                        split_problem_f split, size_t gran, lb_method_t m);
extern lb_t *lb_create_max (size_t threads, algo_f alg,
                        split_problem_f split, size_t gran, lb_method_t method,
                        size_t max);

/**
 \brief Records thread local data and performs initial exploration for static
        load-balancing (also: functions as a barrier). 
 */
extern void lb_local_init (lb_t *lb, int id, void *arg, size_t *load);

extern void lb_destroy (lb_t *lb);

extern void lb_balance (lb_t *lb, int id);

extern int lb_stop(lb_t *lb);

extern int lb_is_stopped(lb_t *lb);

#endif /* LB_H_ */
