/*
 * dfs-stack.c
 *
 *  Created on: May 7, 2009
 *  Author: laarman

 \brief a framestack build with two stacks
 */

#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include "runtime.h"
#include "state-buffer.h"
#include "dfs-stack.h"

char *
stack_to_string(dfs_stack_t stack)
{
	char* r = NULL;
	asprintf(&r, "FrameStack[%zu | %zu / %zu]", stack->states->el_size,
	         buffer_size_int(stack->states), buffer_size_int(stack->frames));
	return r;
}

dfs_stack_t
create_stack(size_t element_size)
{
	dfs_stack_t result = (dfs_stack_t) malloc(sizeof(struct dfs_stack));
	result->states = create_buffer(element_size);
	//we ll save the length of the states in an int for now
	//less is possible
	result->frames = create_buffer(1);
	result->frame_size = 0;
	result->nframes = 0;
	return result;
}

size_t
stack_frame_size(dfs_stack_t stack)
{
	return buffer_size_int(stack->frames);
}

void
destroy_stack(dfs_stack_t stack)
{
	destroy_buffer(stack->states);
	destroy_buffer(stack->frames);
	free(stack);
}

void
enter(dfs_stack_t stack)
{
	if (stack->frame_size == 0) Fatal(1, error, "Enter on empty frame")
    assert(stack->frame_size < INT_MAX);
	push_int(stack->frames, (int[]){stack->frame_size});
	stack->frame_size = 0;
	stack->nframes++;
}

void
leave(dfs_stack_t stack)
{
	if (stack->nframes == 0) Fatal(1, error, "Leave on empty stack")
	discard_int(stack->states, stack->frame_size);
	stack->frame_size = pop_int(stack->frames)[0];
	stack->nframes--;
}

void
push(dfs_stack_t stack, int *state)
{
	stack->frame_size++;
	push_int(stack->states, state);
}

int *
pop(dfs_stack_t stack)
{
	if (stack->frame_size == 0) Fatal(1, error, "Pop on empty stack")
	stack->frame_size--;
	return pop_int(stack->states);
}

int *
top(dfs_stack_t stack)
{
	if (stack->frame_size == 0) return NULL;
	return top_int(stack->states);
}

int *
peek_top(dfs_stack_t stack, size_t frame_offset)
{
	if (!frame_offset && !stack->frame_size) Fatal(1, error, "Peek top on empty frame")
	size_t offset = frame_offset ? stack->frame_size : 0;
	size_t x;
	for (x = 1; x < frame_offset; x++) {
		offset += peek_int(stack->frames, x-1)[0];
	}
	return peek_int(stack->states, offset);
}

