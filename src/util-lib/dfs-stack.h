#ifndef DFS_STACK_H_
#define DFS_STACK_H_

/*
 * dfs-stack.h
 *
 *  Created on: May 7, 2009
 *  Author: laarman

\brief A frame stack stores foleded states and has the following internal representation, by example:
                state_stack: root, s0, s1, s2, s10, s11
                frame_stack: 1, 3, 2 (uint16)

 */


#include <util-lib/is-balloc.h>

typedef struct dfs_stack *dfs_stack_t;



extern dfs_stack_t dfs_stack_create(size_t element_size);
extern void dfs_stack_destroy(dfs_stack_t stack);

extern char *dfs_stack_to_string(dfs_stack_t stack, char *, ssize_t *);

extern void dfs_stack_enter(dfs_stack_t stack);
extern void dfs_stack_leave(dfs_stack_t stack);

extern size_t dfs_stack_nframes (const dfs_stack_t stack);
extern size_t dfs_stack_frame_size(dfs_stack_t stack);
extern size_t dfs_stack_size(dfs_stack_t stack);

extern int *dfs_stack_push(dfs_stack_t stack, const int *state);
extern int *dfs_stack_pop(dfs_stack_t stack);
extern int *dfs_stack_top(dfs_stack_t stack);
extern int *dfs_stack_peek_top(dfs_stack_t stack, size_t frame_offset);
extern int *dfs_stack_peek_top2(dfs_stack_t stack, size_t frame_offset, size_t o);

extern int * dfs_stack_bottom (dfs_stack_t stack);
/**
 * Virtually pops some elements from the bottom of the bottom frame
 * The are lazily thrown away so memory usage increases
 */
extern int *dfs_stack_pop_bottom (dfs_stack_t stack);

/**
 * Discard n elements from the top, also clears frames
 */
extern void dfs_stack_discard (dfs_stack_t stack, size_t num);
extern int *dfs_stack_peek(dfs_stack_t stack, size_t offset);
extern int *dfs_stack_index(dfs_stack_t stack, size_t index);
extern void dfs_stack_clear (dfs_stack_t stack);

/**
 * Walk the stack to the top, for each element, call a callback
 */
extern void dfs_stack_walk_up(dfs_stack_t stack, int (*)(int*,void*),void*);
extern void dfs_stack_walk_down(dfs_stack_t stack, int (*)(int*,void*),void*);

#endif /* DFS_STACK_H_ */
