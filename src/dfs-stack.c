/*
 * dfs-stack.c
 *
 *  Created on: May 7, 2009
 *  Author: laarman

 \brief a framestack build with two stacks
 */

#include <config.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include "runtime.h"
#include "dfs-stack.h"

struct dfs_stack {
    isb_allocator_t states;
    isb_allocator_t frames;
    int frame_size;    /* XXX cannot be size_t b/c it is stored in frames */
    size_t nframes;
};

char *
dfs_stack_to_string (dfs_stack_t stack)
{
    char* r = NULL;
    asprintf(&r, "FrameStack[%zu | %zu / %zu]", isba_elt_size(stack->states),
             isba_size_int(stack->states), isba_size_int(stack->frames));
    return r;
}

dfs_stack_t
dfs_stack_create (size_t element_size)
{
    dfs_stack_t result = RTmalloc(sizeof *result);
    result->states = isba_create(element_size);
    //we'll save the length of the states in an int for now
    //less is possible
    result->frames = isba_create(1);
    result->frame_size = 0;
    result->nframes = 0;
    return result;
}

size_t
dfs_stack_nframes (const dfs_stack_t stack)
{
    return stack->nframes;
}

size_t
dfs_stack_frame_size (dfs_stack_t stack)
{
    return stack->frame_size;
}

void
dfs_stack_destroy (dfs_stack_t stack)
{
    isba_destroy(stack->states);
    isba_destroy(stack->frames);
    free(stack);
}

void
dfs_stack_enter (dfs_stack_t stack)
{
    if (stack->frame_size == 0) Fatal(1, error, "Enter on empty frame");
    assert(stack->frame_size < INT_MAX);
    isba_push_int(stack->frames, &stack->frame_size);
    stack->frame_size = 0;
    stack->nframes++;
}

void
dfs_stack_leave (dfs_stack_t stack)
{
    if (stack->nframes == 0) Fatal(1, error, "Leave on empty stack");
    isba_discard_int(stack->states, stack->frame_size);
    stack->frame_size = *isba_pop_int(stack->frames);
    stack->nframes--;
}

void
dfs_stack_push (dfs_stack_t stack, const int *state)
{
    stack->frame_size++;
    isba_push_int(stack->states, state);
}

int *
dfs_stack_pop (dfs_stack_t stack)
{
    if (stack->frame_size == 0) Fatal(1, error, "Pop on empty stack");
    stack->frame_size--;
    return isba_pop_int(stack->states);
}

int *
dfs_stack_top (dfs_stack_t stack)
{
    if (stack->frame_size == 0) return NULL;
    return isba_top_int(stack->states);
}

int *
dfs_stack_peek_top (dfs_stack_t stack, size_t frame_offset)
{
    if (!frame_offset && !stack->frame_size)
        Fatal(1, error, "Peek top on empty frame");
    size_t offset = frame_offset ? stack->frame_size : 0;
    size_t x;
    for (x = 1; x < frame_offset; x++) {
        offset += isba_peek_int(stack->frames, x-1)[0];
    }
    return isba_peek_int(stack->states, offset);
}
