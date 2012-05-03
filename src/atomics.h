#ifndef ATOMICS_H
#define ATOMICS_H

/**
\file atomics.h
\brief Some operations for atomic data access
*/

/**
\brief These constructs prevent the compiler from optimizing (reordering) reads
    and writes to memory location. Strong order execution has to be guaranteed
    by the CPU for this to work. It seems like x86 is going to for the
    foreseeable future.
*/
#define atomic_read(v)      (*(volatile typeof(*v) *)(v))
#define atomic_write(v,a)   (*(volatile typeof(*v) *)(v) = (a))

/**
\def cache line size to optimize memory allocations for
*/
#define CACHE_LINE 6 //TODO: to config script
static const size_t CACHE_LINE_SIZE = 1 << CACHE_LINE;
static const size_t CACHE_LINE_INT = (1 << CACHE_LINE) / sizeof (int);
static const size_t CACHE_LINE_INT_MASK =
    -((1 << CACHE_LINE) / sizeof (int));

#define cas(a, b, c) __sync_bool_compare_and_swap(a,b,c)
#define cas_ret(a, b, c) __sync_val_compare_and_swap(a,b,c)
#define fetch_or(a, b) __sync_fetch_and_or(a,b)
#define fetch_and(a, b) __sync_fetch_and_and(a,b)
#define fetch_add(a, b) __sync_fetch_and_add(a,b)
#define add_fetch(a, b) __sync_add_and_fetch(a,b)
#define fetch_sub(a, b) __sync_fetch_and_sub(a,b)
#define sub_fetch(a, b) __sync_sub_and_fetch(a,b)

#endif
