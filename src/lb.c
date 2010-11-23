/*
 * lb.c
 * A Load-balancer. See lb.h.
 *
 * TODO: document possible difference in metrics used for
 *       granularity (transitions) and load (states).
 *
 *  Created on: Jun 10, 2010
 *      Author: laarman
 */

#include <sys/times.h>
#include <sys/time.h>
#include <assert.h>

#include "runtime.h"
#include "lb.h"
#include "tls.h"

struct lb_s {
     lb_method_t        method;
     size_t             threads;
     algo_f             algorithm;
     split_problem_f    split;
     size_t           **load;
     size_t             granularity;
     size_t             max_handoff;
     struct lb_status_s {
         volatile int       idle;
         uint64_t           requests;
         char               padding[64];
     }                 *local;
     volatile int       all_done;
};

typedef struct lb_status_s lb_status_t;

static const size_t LOAD_ZERO = 0;

lb_t                *
lb_create (size_t threads, algo_f alg, split_problem_f split,
           size_t gran, lb_method_t m)
{
    return lb_create_max (threads, alg, split, gran, m, MAX_HANDOFF_DEFAULT);
}

lb_t                *
lb_create_max (size_t threads, algo_f alg, split_problem_f split,
               size_t gran, lb_method_t method, size_t max)
{
    assert (threads <= sizeof (uint64_t) * 8);
    lb_t               *lb = RTmalloc( sizeof(lb_t) );
    lb->threads = threads;
    lb->method = method;
    lb->algorithm = alg;
    lb->all_done = 0;
    lb->split = split;
    lb->max_handoff = max;
    lb->granularity = gran;
    lb->load = RTmalloc( sizeof(size_t *[threads]) );
    lb->local = RTmalloc( sizeof(lb_status_t[threads]) );
    for (size_t i = 0; i < threads; i++) {
        lb->load[i] = (size_t *) &LOAD_ZERO;
        lb->local[i].idle = 0;
        lb->local[i].requests = 0;
    }
    return lb;
}

void
lb_local_init(lb_t *lb, int id, void *arg, size_t *load)
{
    if ( lb->method == LB_SRP || lb->method == LB_None )
        return;
    if (id != 0) {
        //wait
        while ( !atomic_read(&lb->all_done) ) {}
        lb->local[id].idle = 1;
        return;
    }

    while ( *load < lb->max_handoff ) //
        lb->algorithm (arg, lb->max_handoff); // do initial exploration
    size_t handoff = *load / lb->threads;
    for (int i = 0; i< (int)lb->threads; i++) {
        if ( i == id )
            continue;
        *load -= lb->split( id, i, handoff );
    }
    //release all
    atomic_write(&lb->all_done, 1);
    lb->local[id].idle = 1;
    for (size_t i = 0; i< lb->threads; i++) {
        while ( !atomic_read(&lb->local[i].idle) ) {}
        lb->local[i].idle = 0;
    }
    atomic_write(&lb->all_done, 0);
}

void
lb_destroy(lb_t *lb)
{
    RTfree(lb->local);
    RTfree(lb->load);
    RTfree(lb);
}

int
lb_stop(lb_t *lb)
{
    return cas (&lb->all_done, 0, 1);
}

int
lb_is_stopped (lb_t *lb)
{
    return lb->all_done;
}

static inline           size_t
get_highest_load (lb_t *lb, size_t me)
{
    size_t              idx = 0;
    size_t              max = 0;
    for (size_t i = 0; i < lb->threads; i++) {
        size_t              last = *lb->load[i];
        if (i != me && last > max) {
            idx = i;
            max = last;
        }
    }
    return idx;
}

static inline           size_t
get_random (lb_t *lb, size_t me)
{
    struct timeval      t;
    gettimeofday (&t, NULL);
    size_t              ran_idx = (size_t) t.tv_usec;
    ran_idx %= lb->threads;
    return ran_idx == me ? (ran_idx ? ran_idx - 1 : lb->threads - 1) : ran_idx;
}

void
handoff(lb_t *lb, int id, uint64_t requests)
{
    int                 todo[ lb->threads ];
    size_t j = 0;
    for(size_t i = 0;i < lb->threads;i++){
        todo[j] = i;
        j += 0 != ((1 << i) & requests);
    }
    size_t old = *lb->load[id];
    *lb->load[id] = LOAD_ZERO;
    for(size_t i = 0;i < j;i++){
        int idx = todo[i];
        if(j - i <= (lb->threads >> 1)){
            size_t handoff = old >> 1;
            handoff = old <= lb->max_handoff ? old : lb->max_handoff;
            old -= lb->split(id, idx, handoff);
        }
        (lb->local + idx)->idle = 0;
    }
    *lb->load[id] = old;
}


void
lb_balance_static (lb_t *lb, int id, void *arg, size_t *load)
{
    lb->algorithm ( arg, SIZE_MAX );
    (void) load; (void) id;
}

void
lb_balance_SRP (lb_t *lb, int id, void *arg, size_t *load)
{
    lb_status_t        *status = &lb->local[id];
    lb->load[id] = load;
    while ( !lb->all_done ) {
        lb->algorithm ( arg, lb->granularity );
        status->idle = (LOAD_ZERO == *load);
        size_t          idle_count = 0;
        for (size_t i = 0; i < lb->threads; i++)
            idle_count += (lb->local+i)->idle;
        if (lb->threads == idle_count)
            break;
        if ( status->idle ) {
            size_t      high_idx = get_highest_load (lb, id);
            fetch_or (&lb->local[high_idx].requests, 1 << id);
        }
        uint64_t        requests = fetch_and (&status->requests, 0);
        if ( requests )
            handoff(lb, id, requests);
        while (status->idle && !lb->all_done) {}
    }
    lb->all_done = 1;
    status->idle = 1;
}

void
lb_balance (lb_t *lb, int id, void *arg, size_t *load)
{
    switch ( lb->method ) {
    case LB_None:
    case LB_Static:
        lb_balance_static( lb, id, arg, load ); break;
    case LB_Combined:
        Warning (info, "Combined load balancing (static+SRP) is not implemented");
    case LB_SRP:
        lb_balance_SRP( lb, id, arg, load ); break;
    }
}
