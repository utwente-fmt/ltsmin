#ifndef SET_LL_H
#define SET_LL_H

/**
\file dbs-ll.h
\brief Lockless hash set for strings with internal allocation.
 This class maintains a set of strings and projects this down to a dense range
 of integers. The operations are thread-safe. The quality of the density is
 dependent on an equal workload (number of inserts) among the different threads.
*/

#include <stdbool.h>

/**
\typedef The string set.
*/
typedef struct set_ll_s set_ll_t;

/**
\typedef The global allocator for the set.
*/
typedef struct set_ll_allocator_s set_ll_allocator_t;

/**
 \brief Initializes internal allocator. Call once. Uses HRE.
 */
extern set_ll_allocator_t *set_ll_init_allocator (bool shared);

extern size_t              set_ll_print_alloc_stats(log_t log, set_ll_allocator_t *);

extern char        *set_ll_get      (set_ll_t *set, int idx, int *len);

extern int          set_ll_put      (set_ll_t *set, char *str, int len);

extern int          set_ll_count    (set_ll_t *set);

/**
\Brief binds a key to a specific value. NOT THREAD-SAFE!
 */
void                set_ll_install  (set_ll_t *set, char *name, int len, int idx);

extern set_ll_t    *set_ll_create   (set_ll_allocator_t *alloc, int typeno);

extern size_t       set_ll_print_stats(log_t log, set_ll_t *set, char *name);

extern void         set_ll_destroy  (set_ll_t *set);

typedef struct set_ll_iterator_s set_ll_iterator_t;

extern char *set_ll_iterator_next (set_ll_iterator_t *it, int *len);

extern int set_ll_iterator_has_next (set_ll_iterator_t *it);

extern set_ll_iterator_t *set_ll_iterator (set_ll_t *set);

#endif
