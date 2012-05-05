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

#include <assert.h>
#include <stdlib.h>

#include <runtime.h>
#include <lb2.h>
#include <atomics.h>

struct lb2_s {
     lb2_method_t       method;
     size_t             threads;
     lb2_split_problem_f split;
     void             **args;
     size_t             granularity;
     size_t             mask;
     size_t             max_handoff;
     struct lb2_status_s {
         int                idle;
         uint32_t           seed;
         size_t             requests;
         size_t             received;
         char               pad[(2<<CACHE_LINE) - 2*sizeof(int) - 2*sizeof(size_t)];
     } __attribute__ ((packed)) *local;
     int                all_done;
     int                stopped;
};

typedef struct lb2_status_s lb2_status_t;

static inline int  stopped (lb2_t *lb) {
    return atomic_read (&lb->stopped);
}
static inline int  all_done (lb2_t *lb) {
    return atomic_read (&lb->all_done);
}
static inline void set_all_done (lb2_t *lb) {
    atomic_write (&lb->all_done, 1);
}
static inline int  try_all_done (lb2_t *lb) {
    return cas (&lb->all_done, 0, 1);
}

static inline int  get_idle (lb2_t *lb, int id) {
    return atomic_read(&lb->local[id].idle);
}
static inline void set_idle (lb2_t *lb, int id, int a) {
    atomic_write (&lb->local[id].idle, a);
}

int
lb2_stop (lb2_t *lb)
{
    atomic_write (&lb->stopped, 1);
    return try_all_done (lb);
}

int
lb2_is_stopped (lb2_t *lb)
{
    return stopped (lb);
}

void
lb2_reinit (lb2_t *lb, size_t id)
{
    atomic_write (&lb->local[id].requests, 0);
    atomic_write (&lb->all_done, 0);
    if (0 == id)
        set_idle (lb, id, 0);
    lb2_barrier (lb->threads);
}

void
lb2_local_init (lb2_t *lb, int id, void *arg)
{
    // record thread local data
    atomic_write (&(lb->args[id]), arg);
    lb2_reinit (lb, id);
}

static inline int
request_random (lb2_t *lb, size_t id)
{
     size_t res = 0;
     do {
         res = rand_r (&lb->local[id].seed) % lb->threads;
     } while (res == id);
     fetch_or (&lb->local[res].requests, 1L << id);
     Debug ("Requested %lld", res);
     return 1;
}

static inline void
handoff (lb2_t *lb, int id, size_t requests, size_t *my_load)
{
    int                 todo[ lb->threads ];

    // record requestors (SIMD optimized code)
    size_t j = 0;
    for (size_t oid = 0; oid < lb->threads; oid++) {
        todo[j] = oid;
        j += (0 != ((1L<<oid) & requests));
    }
    
    // handle the requests
    for (size_t idx = 0; idx < j; idx++) {
        int oid = todo[idx];
        if (j - idx <= (lb->threads >> 1)) {
            assert (get_idle (lb, oid));
            size_t handoff = *my_load >> 1;
            handoff = handoff < lb->max_handoff ? handoff : lb->max_handoff;
            size_t load = lb->split (lb->args[id], lb->args[oid], handoff );
            *my_load -= load;
            atomic_write (&lb->local[oid].received, load);
        }
        set_idle (lb, oid, 0);
    }
}

size_t
lb2_balance (lb2_t *lb, int id, size_t my_load)
{
    lb2_status_t        *status = &lb->local[id];

    if (lb2_is_stopped(lb))
        return 0;
    if (my_load > 0 && (my_load & lb->mask) != lb->mask )
        return my_load;

    do {
        if ( all_done(lb) ) {
            set_idle (lb, id, 1);
            return 0;
        }
        set_idle (lb, id, 0 == my_load);
        int             wait_reply = 0;
        if ( get_idle(lb, id) ) {
            size_t          all_idle = 1;
            for (size_t oid = 0; oid < lb->threads && all_idle; oid++)
                all_idle &= get_idle (lb, oid);
            Debug ("LBing with load: %zu, all_done: %zu", my_load, all_idle);
            if ( all_idle ) {
                set_all_done (lb);
                assert (my_load == 0);
                break; // load == 0
            }
            wait_reply = request_random (lb, id); // lb->threads > 1
        }
        size_t          requests = fetch_and (&status->requests, 0);
        if ( requests )
            handoff (lb, id, requests, &my_load);
        if ( wait_reply ) {
            Debug ("Waiting for an answer");
            while ( get_idle(lb, id) && !all_done(lb) ) {}
            my_load += atomic_read (&lb->local[id].received);
            atomic_write (&lb->local[id].received, 0);
            Debug ("Received load: %zu", my_load);
        }
    } while (my_load == 0);
    return my_load;
}

lb2_t                *
lb2_create (size_t threads, lb2_split_problem_f split,
           size_t gran, lb2_method_t m)
{
    return lb2_create_max (threads, split, gran, m, LB2_MAX_HANDOFF_DEFAULT);
}

lb2_t                *
lb2_create_max (size_t threads, lb2_split_problem_f split,
               size_t gran, lb2_method_t method, size_t max)
{
    assert (threads <= LB2_MAX_THREADS);
    lb2_t               *lb = RTalign(CACHE_LINE_SIZE, sizeof(lb2_t));
    void *args = RTalign (CACHE_LINE_SIZE, sizeof(void *[threads]));
    void *local = RTalign (CACHE_LINE_SIZE, sizeof(lb2_status_t[threads]));
    lb->threads = threads;
    lb->method = method;
    lb->all_done = 0;
    lb->stopped = 0;
    lb->split = split;
    lb->max_handoff = max;
    assert (gran < 32 && gran > 0 && "wrong granularity");
    lb->granularity = 1UL << gran;
    lb->mask = lb->granularity - 1;
    lb->args = args;
    lb->local = local;
    for (size_t i = 0; i < threads; i++) {
        lb->local[i].idle = 0;
        lb->local[i].requests = 0;
    }
    return lb;
}

void
lb2_destroy (lb2_t *lb)
{
    RTfree (lb->args);
    RTfree (lb->local);
    RTfree (lb);
}

static size_t       __attribute__ ((aligned(1UL<<CACHE_LINE))) barrier_count = 0;
static size_t       __attribute__ ((aligned(1UL<<CACHE_LINE))) barrier_wait = 0;

lb2_barrier_result_t
lb2_barrier (size_t W)
{
    //assert ((W <= LB2_MAX_THREADS));
    size_t          flip = atomic_read (&barrier_wait);
    size_t          count = add_fetch (&barrier_count, 1);
    if (W == count) {
        atomic_write (&barrier_count, 0);
        atomic_write (&barrier_wait, 1 - flip); // flip wait
        return LB2_BARRIER_MASTER;
    } else {
        while (flip == atomic_read(&barrier_wait)) {}
        return LB2_BARRIER_SLAVE;
    }
}

static size_t       __attribute__ ((aligned(1UL<<CACHE_LINE))) reduce_count = 0;
static size_t       __attribute__ ((aligned(1UL<<CACHE_LINE))) reduce_result = 0;
static size_t       __attribute__ ((aligned(1UL<<CACHE_LINE))) reduce_wait = 0;

size_t
lb2_reduce (size_t val, size_t W)
{
    //assert ((W <= LB2_MAX_THREADS) && (val < (1UL<<58)) && (sizeof(size_t) == 8));
    size_t          flip = atomic_read (&reduce_wait);
    size_t          count = add_fetch (&reduce_count, val + (1UL << 58));
    if (count >> 58 == W) {
        atomic_write (&reduce_count, 0);
        atomic_write (&reduce_result, count - (W << 58));
        atomic_write (&reduce_wait, 1 - flip); // flip wait
    } else {
        while (flip == atomic_read(&reduce_wait)) {}
    }
    return atomic_read (&reduce_result);
}
