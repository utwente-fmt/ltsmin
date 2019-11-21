/*
 * dfs-stack.c
 *
 *  Created on: May 7, 2009
 *  Author: laarman

 \brief a framestack build with two stacks
 */

#include <hre/config.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <hre/user.h>
#include <util-lib/dfs-stack.h>


struct dfs_stack {
    isb_allocator_t states;
    isb_allocator_t frames;
    int frame_size; //size of current stack frame
    int frame_bottom;   
    size_t nframes;
};

char *
dfs_stack_to_string (dfs_stack_t stack, char *r, ssize_t *sz_)
{
    ssize_t sz = *sz_;
    *sz_ = snprintf(r, sz, "FrameStack[%zu | %zu / %zu]",
                    isba_elt_size(stack->states),
                    isba_size_int(stack->states), isba_size_int(stack->frames));
    return r;
}

dfs_stack_t
dfs_stack_create (size_t element_size)
{
    dfs_stack_t result = RTmalloc(sizeof(struct dfs_stack));
    result->states = isba_create(element_size);
    result->frames = isba_create(2);
    result->frame_size = 0;
    result->frame_bottom = 0;
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
    return stack->frame_size - stack->frame_bottom;
}

size_t
dfs_stack_size (dfs_stack_t stack)
{
    return isba_size_int ( stack->states );
}

static void
clean_frame(dfs_stack_t stack)
{
    if (stack->frame_bottom){
        isba_discard_int(stack->states, stack->frame_bottom);
        stack->frame_bottom = 0;
        stack->frame_size = 0;
    }
}

void
dfs_stack_destroy (dfs_stack_t stack)
{
    isba_destroy(stack->states);
    isba_destroy(stack->frames);
    RTfree(stack);
}

void
dfs_stack_enter (dfs_stack_t stack)
{
    if (stack->frame_size == stack->frame_bottom) Abort("Enter on empty frame %d == %d ", stack->frame_size, stack->frame_bottom);
    int t[2] = {stack->frame_size, stack->frame_bottom};
    isba_push_int(stack->frames, t);
    stack->frame_size = 0;
    stack->frame_bottom = 0;
    stack->nframes++;
}

void
dfs_stack_leave (dfs_stack_t stack)
{
    if (stack->nframes == 0) Abort("Leave on empty stack");
    isba_discard_int(stack->states, stack->frame_size);
    int         *top = isba_top_int(stack->frames);
    stack->frame_size = top[0];
    stack->frame_bottom = top[1];
    isba_pop_int(stack->frames);
    stack->nframes--;
}

int *
dfs_stack_push (dfs_stack_t stack, const int *state)
{
    stack->frame_size++;
    return isba_push_int(stack->states, state);
}

int *
dfs_stack_pop (dfs_stack_t stack)
{
    if (stack->frame_size == stack->frame_bottom) {
        clean_frame(stack);
        return NULL;
    }
    stack->frame_size--;
    return isba_pop_int(stack->states);
}

int *
dfs_stack_top (dfs_stack_t stack)
{
    if (stack->frame_size == stack->frame_bottom) {
        clean_frame(stack);
        return NULL;
    }
    return isba_top_int(stack->states);
}

int *
dfs_stack_peek_top2 (dfs_stack_t stack, size_t frame_offset, size_t o)
{
    if (!frame_offset && stack->frame_size == stack->frame_bottom)
        Abort("Peek top on empty frame");
    size_t offset = frame_offset ? stack->frame_size : 0;
    size_t x;
    for (x = 1; x < frame_offset; x++) {
        offset += isba_peek_int(stack->frames, x-1)[0];
    }
    return isba_peek_int(stack->states, offset + o);
}

int *
dfs_stack_peek_top (dfs_stack_t stack, size_t frame_offset)
{
    return dfs_stack_peek_top2 (stack, frame_offset, 0);
}

int *
dfs_stack_peek (dfs_stack_t stack, size_t offset)
{
    return isba_peek_int(stack->states, offset);
}

int *
dfs_stack_index (dfs_stack_t stack, size_t index)
{
    return isba_index(stack->states, index);
}

void
dfs_stack_discard (dfs_stack_t stack, size_t num)
{
    size_t frame_size;
    while ( (frame_size = dfs_stack_frame_size (stack)) < num ) {
        dfs_stack_leave( stack );
        num -= frame_size;
    }
    isba_discard_int(stack->states, num);
    stack->frame_size -= num;
}

void
dfs_stack_clear (dfs_stack_t stack)
{
    isba_discard_int (stack->states, isba_size_int(stack->states));
    isba_discard_int (stack->frames, isba_size_int(stack->frames));
    stack->nframes = 0;
    stack->frame_size = 0;
    stack->frame_bottom = 0;
}

int *
dfs_stack_bottom (dfs_stack_t stack)
{
    if (stack->frame_size == stack->frame_bottom) {
        clean_frame(stack);
        return NULL;
    }
    return isba_peek_int(stack->states, stack->frame_size - stack->frame_bottom - 1);
}

int *
dfs_stack_pop_bottom (dfs_stack_t stack)
{
    if (stack->frame_size == stack->frame_bottom)
        Abort("pop bottom empty frame");
    stack->frame_bottom++;
    return isba_peek_int(stack->states, stack->frame_size - stack->frame_bottom);
}

/* stack:
 * [root] [frame 1  ..... ] [frame 2 .....  ]    .. [ last frame = stack->frame_size  .....
 * [ 0  ] [1 ] ...      [n] [ n+1 ] ..        [m-1] [ m + 1] ...... [l-1] [l]
 * result of walk up:
 * l, m-1, n, ... , root
 * down is the other way around..
 */
void
dfs_stack_walk_up(dfs_stack_t stack, int(*cb)(int*, void*), void* ctx)
{
    int offset = 0;
    int res = cb( isba_peek_int(stack->states, offset), ctx );
    if (res && stack->frame_size != 0) {
        offset += stack->frame_size;
        res = cb( isba_peek_int(stack->states, offset), ctx );
    }
    for(size_t i=0; i < stack->nframes-1 &&  res; i++) {
        offset += isba_peek_int(stack->frames, i)[0];
        res = cb( isba_peek_int(stack->states, offset), ctx );
    }
}

void
dfs_stack_walk_down(dfs_stack_t stack, int(*cb)(int*, void*), void* ctx)
{
    // set offset of the root node
    int offset = isba_size_int ( stack->states );
    int res = 1;

    for(size_t i=stack->nframes-1 ; i > 0 &&  res; i--) {
        offset -= isba_peek_int(stack->frames, i)[0];
        res = cb( isba_peek_int(stack->states, offset), ctx );
    }
    if (res && stack->frame_size != 0) {
        offset = stack->frame_size;
        res = cb( isba_peek_int(stack->states, offset), ctx );
    }
    if (res) res = cb( isba_peek_int(stack->states, 0), ctx );
}
