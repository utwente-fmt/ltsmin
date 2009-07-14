#ifndef DFSSTACK_H_
#define DFSSTACK_H_

/*
 * dfs-stack.h
 *
 *  Created on: May 7, 2009
 *  Author: laarman

\brief A frame stack stores foleded states and has the following internal representation, by example:
		state_stack: root, s0, s1, s2, s10, s11
		frame_stack: 1, 3, 2 (uint16)

 */


#include "state-buffer.h"

typedef struct dfs_stack {
	state_buffer_t states;
	state_buffer_t frames;
	size_t frame_size;
	size_t nframes;
} *dfs_stack_t;



extern dfs_stack_t create_stack(size_t element_size);
extern void destroy_stack(dfs_stack_t stack);

extern char *stack_to_string(dfs_stack_t stack);

extern void enter(dfs_stack_t stack);
extern void leave(dfs_stack_t stack);

extern size_t stack_frame_size(dfs_stack_t stack);

extern void push(dfs_stack_t stack, int *state);
extern int *pop(dfs_stack_t stack);
extern int *top(dfs_stack_t stack);
extern int *peek_top(dfs_stack_t stack, size_t frame_offset);

#endif /* DFSSTACK_H_ */
