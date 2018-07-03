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

#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/lb.h>

typedef struct lb_status_s {
    int                 idle;           // poll local + write global
    uint32_t            seed;           // read+write local
    size_t  __attribute__ ((aligned(CACHE_LINE_SIZE))) requests; // read local + write global
    size_t  __attribute__ ((aligned(CACHE_LINE_SIZE))) received; // read local + write global
    size_t  __attribute__ ((aligned(CACHE_LINE_SIZE))) max_load; // read local
    void    __attribute__ ((aligned(CACHE_LINE_SIZE)))*arg;      // read local + read global
} lb_status_t;

/**
 * The base struct is read only, except for occasional (one-time) writes to
 * stopped/all_done.
 */
struct lb_s {
    size_t             mask;    //inlined: see lb.h
    int                stopped; //inlined: see lb.h
    int                all_done;
    size_t             threads;
    size_t             granularity;
    size_t             max_handoff;
    lb_status_t      **local;
    size_t  __attribute__ ((aligned(CACHE_LINE_SIZE))) barrier_count;
    size_t  __attribute__ ((aligned(CACHE_LINE_SIZE))) barrier_wait;
    size_t  __attribute__ ((aligned(CACHE_LINE_SIZE))) reduce_count;
    size_t  __attribute__ ((aligned(CACHE_LINE_SIZE))) reduce_result;
    size_t  __attribute__ ((aligned(CACHE_LINE_SIZE))) reduce_wait;
};


static inline int  all_done (lb_t *lb) {
    return atomic_read (&lb->all_done);
}
static inline void set_all_done (lb_t *lb) {
    atomic_write (&lb->all_done, 1);
}
static inline int  try_stop (lb_t *lb) {
    return cas (&lb->stopped, 0, 1);
}
static inline int  get_idle (lb_t *lb, int id) {
    return atomic_read(&lb->local[id]->idle);
}
static inline void set_idle (lb_t *lb, int id, int a) {
    atomic_write (&lb->local[id]->idle, a);
}

int
lb_stop (lb_t *lb)
{
    if (try_stop(lb)) {     // first set is_stopped!
        set_all_done (lb);  // second set all_done
        return true; // winner
    }
    return false;
}

void
lb_reinit (lb_t *lb, size_t id)
{
    atomic_write (&lb->local[id]->requests, 0);
    atomic_write (&lb->all_done, 0);
    set_idle (lb, id, 0);
    //lb_barrier (lb->threads);
    HREbarrier(HREglobal());
}

void
lb_local_init (lb_t *lb, int id, void *arg)
{
    lb_status_t        *loc = RTalign(CACHE_LINE_SIZE, sizeof(lb_status_t));
    size_t alloc_distance = (size_t)&loc->received - (size_t)&loc->requests;
    HREassert (alloc_distance == CACHE_LINE_SIZE, "Wrong alignment in allocation");
    loc->idle = 0;
    loc->requests = 0;
    loc->received = 0;
    loc->max_load = 0;
    loc->seed = (id + 1) * 32732678642;
    // record thread local data
    atomic_write (&(loc->arg), arg);
    lb->local[id] = loc;
    lb_reinit (lb, id);
}

static inline int
request_random (lb_t *lb, size_t id)
{
    size_t res = 0;
    do {
        res = rand_r (&lb->local[id]->seed) % lb->threads;
    } while (res == id);
    fetch_or (&lb->local[res]->requests, 1LL << id);
    Debug ("Requested %zu", res);
    return 1;
}

static inline void
handoff (lb_t *lb, int id, size_t requests, size_t *my_load,
         lb_split_problem_f split)
{
    int                 todo[ lb->threads ];

    // record requestors (SIMD optimized code)
    size_t j = 0;
    for (size_t oid = 0; oid < lb->threads; oid++) {
        todo[j] = oid;
        j += (0 != ((1LL<<oid) & requests));
    }
    
    // handle the requests
    for (size_t idx = 0; idx < j; idx++) {
        int oid = todo[idx];
        if (j - idx <= (lb->threads >> 1)) {
            HREassert (get_idle (lb, oid), "Thread reactiveated before handoff complete");
            size_t handoff = *my_load >> 1;
            handoff = handoff < lb->max_handoff ? handoff : lb->max_handoff;
            ssize_t load = split (lb->local[id]->arg, lb->local[oid]->arg, handoff);
            if (load < 0) // copied load
                load = -load;
            else
                *my_load -= load;
            atomic_write (&lb->local[oid]->received, load);
        }
        set_idle (lb, oid, 0);
    }
}

size_t
lb_internal (lb_t *lb, int id, size_t my_load, lb_split_problem_f split)
{
    lb_status_t        *status = lb->local[id];
    if (my_load > status->max_load)
        status->max_load = my_load;
    do {
        if ( all_done(lb) ) {
            set_idle (lb, id, 1);
            return 0;
        }
        int idle = (0 == my_load);
        if (idle != get_idle(lb, id))
            set_idle (lb, id, idle); // update only if necessary (others are watching)
        int             wait_reply = 0;
        if ( idle ) {
            size_t          all_idle = 1;
            for (size_t oid = 0; oid < lb->threads && all_idle; oid++)
                all_idle &= get_idle (lb, oid);
            Debug ("LBing with load: %zu, all_done: %zu", my_load, all_idle);
            if ( all_idle ) {
                set_all_done (lb);
                HREassert (my_load == 0, "Premature termination detection");
                break; // load == 0
            }
            wait_reply = request_random (lb, id); // lb->threads > 1
        }
        size_t          requests = fetch_and (&status->requests, 0);
        if ( requests )
            handoff (lb, id, requests, &my_load, split);
        if ( wait_reply ) {
            Debug ("Waiting for an answer");
            while ( get_idle(lb, id) && !all_done(lb) ) {}
            my_load += atomic_read (&lb->local[id]->received);
            atomic_write (&lb->local[id]->received, 0);
            Debug ("Received load: %zu", my_load);
        }
    } while (my_load == 0);
    return my_load;
}

lb_t                *
lb_create (size_t threads, size_t gran)
{
    return lb_create_max (threads, gran, lb_MAX_HANDOFF_DEFAULT);
}

lb_t                *
lb_create_max (size_t threads, size_t gran, size_t max)
{
    HREassert (threads <= lb_MAX_THREADS, "Only %zu threads allowed", lb_MAX_THREADS);
    lb_t               *lb = RTalign(CACHE_LINE_SIZE, sizeof(lb_t));
    lb->local = RTalign(CACHE_LINE_SIZE, sizeof(lb_status_t[lb_MAX_THREADS]));
    size_t alloc_distance = (size_t)&lb->barrier_wait - (size_t)&lb->barrier_count;
    HREassert (alloc_distance == CACHE_LINE_SIZE, "Wrong alignment in allocation");
    for (size_t i = 0; i < threads; i++)
        lb->local[i] = NULL;
    lb->threads = threads;
    lb->all_done = 0;
    lb->stopped = 0;
    lb->max_handoff = max;
    HREassert (gran < 32, "wrong granularity");
    lb->granularity = 1ULL << gran;
    lb->mask = lb->granularity - 1;
    return lb;
}

void
lb_destroy (lb_t *lb)
{
    for (size_t i = 0; i < lb->threads; i++)
        if (lb->local[i]) RTfree(lb->local[i]);
    RTalignedFree (lb->local);
    RTalignedFree (lb);
}

lb_barrier_result_t
lb_barrier (lb_t *lb)
{
    size_t W = lb->threads;
    size_t          flip = atomic_read (&lb->barrier_wait);
    size_t          count = add_fetch (&lb->barrier_count, 1);
    if (W == count) {
        atomic_write (&lb->barrier_count, 0);
        atomic_write (&lb->barrier_wait, 1 - flip); // flip wait
        return lb_BARRIER_MASTER;
    } else {
        while (flip == atomic_read(&lb->barrier_wait)) {}
        return lb_BARRIER_SLAVE;
    }
}

size_t
lb_reduce (lb_t *lb, size_t val)
{
#ifdef __x86_64__
#define SHIFT 58
#else
#define SHIFT 26
#endif
    size_t W = lb->threads;
    HRE_ASSERT ((val < (1ULL<<SHIFT)), "Overflow in reduce");
    size_t          flip = atomic_read (&lb->reduce_wait);
    size_t          count = add_fetch (&lb->reduce_count, val + (1ULL << SHIFT));
    if (count >> SHIFT == W) {
        atomic_write (&lb->reduce_count, 0);
        atomic_write (&lb->reduce_result, count - (W << SHIFT));
        atomic_write (&lb->reduce_wait, 1 - flip); // flip wait
    } else {
        while (flip == atomic_read(&lb->reduce_wait)) {}
    }
    return atomic_read (&lb->reduce_result);
}

size_t
lb_max_load (lb_t *lb)
{
    size_t              total = 0;
    for (size_t i = 0; i < lb->threads; i++) {
        total += lb->local[i]->max_load;
    }
    return total;
}
