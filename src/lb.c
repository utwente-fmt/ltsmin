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
     void             **args;
     size_t           **load;
     size_t             granularity;
     size_t             max_handoff;
     struct lb_status_s {
         int                idle;
         size_t             requests;
         char               pad[(1<<CACHE_LINE) - sizeof(int) - sizeof(size_t)];
     }                 *local;
     int                all_done;
     int                one_done;
     int                initialized;
};

typedef struct lb_status_s lb_status_t;

static const size_t LOAD_ZERO = 0;

static inline int  is_initialized (lb_t *lb) {
    return atomic32_read (&lb->initialized);
}
static inline void set_initialized (lb_t *lb) {
    atomic32_write (&lb->initialized, 1);
}

static inline int  one_done (lb_t *lb) {
    return atomic32_read (&lb->one_done);
}
static inline void set_one_done (lb_t *lb) {
    atomic32_write (&lb->one_done, 1);
}

static inline int  all_done (lb_t *lb) {
    return atomic32_read (&lb->all_done);
}
static inline void set_all_done (lb_t *lb) {
    atomic32_write (&lb->all_done, 1);
}
static inline int  try_all_done (lb_t *lb) {
    return cas (&lb->all_done, 0, 1);
}

static inline int  get_idle (lb_t *lb, int id) {
    return atomic32_read(&lb->local[id].idle);
}
static inline void set_idle (lb_t *lb, int id, int a) {
    atomic32_write (&lb->local[id].idle, a);
}

static inline size_t get_load (lb_t *lb, int id) {
    return atomic_read (lb->load[id]);
}
static inline void set_load (lb_t *lb, int id, size_t a) {
    atomic_write (lb->load[id], a);
}

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
    lb->one_done = 0;
    lb->initialized = 0;
    lb->split = split;
    lb->max_handoff = max;
    lb->granularity = gran;
    lb->args = RTalign (CACHE_LINE_SIZE, sizeof(void   *[threads]) );
    lb->load = RTalign (CACHE_LINE_SIZE, sizeof(size_t *[threads]) );
    lb->local = RTalign (CACHE_LINE_SIZE, sizeof(lb_status_t[threads]) );
    for (size_t i = 0; i < threads; i++) {
        lb->load[i] = (size_t *) &LOAD_ZERO;
        lb->local[i].idle = 0;
        lb->local[i].requests = 0;
    }
    return lb;
}

static void
wait_all (lb_t *lb)
{
    for (size_t oid = 0; oid< lb->threads; oid++) {
        while ( 0 == get_idle(lb, oid) ) {}
        set_idle (lb, oid, 0);
    }
}

void
lb_local_init (lb_t *lb, int id, void *arg, size_t *load)
{
    // record thread local data
    atomic_ptr_write (&(lb->args[id]), arg);
    atomic_ptr_write (&(lb->load[id]), load);
    
    // signal entry in lb_local_init
    set_idle (lb, id, 1);
    if (id != 0) { // wait
        while ( !is_initialized(lb) ) {}
        return;
    }

    if ( lb->method == LB_Static || lb->method == LB_Combined ) {
        // do initial exploration and divide results
            lb->algorithm (arg, lb->granularity);

        // wait for other's to call lb_local_init and divide initial problem
        wait_all (lb);
        size_t              initial = get_load (lb, id);
        size_t              handoff = initial / lb->threads;
        size_t              leftover = initial;
        size_t              real_handoff;
        for (int oid = 0; oid < (int)lb->threads; oid++) {
            if ( oid == id )
                continue;
            real_handoff = lb->split ( lb->args[id], lb->args[oid], handoff );
            leftover -= real_handoff;
            set_load (lb, oid, get_load(lb, oid) + real_handoff);
        }
        set_load (lb, id, leftover);
    } else {
        // wait for other's to call lb_local_init
        wait_all (lb);
    }
    
    // release all
    set_initialized (lb);
}

void
lb_destroy (lb_t *lb)
{
    RTfree (lb->args);
    RTfree (lb->local);
    RTfree (lb->load);
    RTfree (lb);
}

int
lb_stop (lb_t *lb)
{
    return try_all_done (lb);
}

int
lb_is_stopped (lb_t *lb)
{
    return all_done (lb);
}

static inline int
request_highest_load (lb_t *lb, size_t id)
{
    lb_status_t        *victim = NULL;
    size_t              max = 0;
    for (size_t oid = 0; oid < lb->threads; oid++) {
        size_t              load = get_load (lb, oid);
        if (load > max && oid != id) {
            victim = lb->local + oid;
            max = load;
        }
    }
    if (victim == NULL)
        return 0;
    fetch_or (&victim->requests, 1L << id);
    return 1;
}

static inline void
handoff (lb_t *lb, int id, size_t requests)
{
    int                 todo[ lb->threads ];

    // record requestors (SIMD optimized code)
    size_t j = 0;
    for (size_t oid = 0; oid < lb->threads; oid++) {
        todo[j] = oid;
        j += (0 != ((1L<<oid) & requests));
    }
    
    // set load to ZERO to scare other requestors off
    size_t old = get_load (lb, id);
    set_load (lb, id, 0);

    // handle the requests
    for (size_t idx = 0; idx < j; idx++) {
        int oid = todo[idx];
        if (j - idx <= (lb->threads >> 1)) {
            size_t handoff = old >> 1;
            handoff = handoff < lb->max_handoff ? handoff : lb->max_handoff;
            assert (get_idle (lb, oid));
            handoff = lb->split (lb->args[id], lb->args[oid], handoff);
            old -= handoff;
            set_load (lb, oid, get_load(lb, oid) + handoff);
        }
        set_idle (lb, oid, 0);
    }
    set_load (lb, id, old);
}

static void
lb_balance_SRP (lb_t *lb, int id)
{
    lb_status_t        *status = &lb->local[id];
    while ( !all_done(lb) ) {
        lb->algorithm ( lb->args[id], lb->granularity );
        set_idle (lb, id, 0 == get_load(lb, id));
        size_t          idle_count = 0;
        for (size_t oid = 0; oid < lb->threads; oid++)
            idle_count += get_idle (lb, oid);
        if ( lb->threads == idle_count )
            break;
        int             wait_reply = 0;
        if ( get_idle(lb, id) )
            wait_reply = request_highest_load (lb, id);
        size_t          requests = fetch_and (&status->requests, 0);
        if ( requests )
            handoff (lb, id, requests);
        if ( wait_reply )
            while ( get_idle(lb, id) && !all_done(lb) ) {}
    }
    set_all_done (lb);
    set_idle (lb, id, 1);
}

static void
lb_balance_static (lb_t *lb, int id)
{
    while ( 0 != get_load(lb, id) && !all_done(lb) )
        lb->algorithm ( lb->args[id], lb->granularity );
}

static void
lb_balance_none (lb_t *lb, int id)
{
    lb->algorithm ( lb->args[id], SIZE_MAX );
}

static void
lb_balance_combined (lb_t *lb, int id)
{
    while ( 0 != get_load(lb, id) && !one_done(lb) && !all_done(lb) )
        lb->algorithm ( lb->args[id], lb->granularity );
    set_one_done (lb);
    lb_balance_SRP ( lb, id );
}

void
lb_balance (lb_t *lb, int id)
{
    if ( !is_initialized(lb) )
        Fatal (1, error, "Load balancer is not initialized.")
    switch ( lb->method ) {
    case LB_None:
        lb_balance_none ( lb, id ); break;
    case LB_Static:
        lb_balance_static ( lb, id ); break;
    case LB_Combined:
        lb_balance_combined ( lb, id ); break;
    case LB_SRP:
        lb_balance_SRP ( lb, id ); break;
    }
}
