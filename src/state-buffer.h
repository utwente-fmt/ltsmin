#ifndef STATE_BUFFER_H
#define STATE_BUFFER_H

#include <unistd.h>

/**
 \file state-buffer.h

 Provides dynamically resizing arrays.
 */

/**
 \brief Opaque type state buffer.
 */
typedef struct state_buffer *state_buffer_t;

struct state_buffer {
    size_t el_size;
    size_t max_blocks;
    size_t num_block;
    size_t cur_index;
    int** blocks;
};

/**
 \brief Create a buffer
 */
extern state_buffer_t create_buffer(int element_size);

extern void destroy_buffer(state_buffer_t buffer);

extern char *buffer_to_string(state_buffer_t buffer);

extern void push_int(state_buffer_t buffer, int *element);

extern int *pop_int(state_buffer_t buffer);

extern void discard_int(state_buffer_t buffer, size_t amount);

extern int *top_int(state_buffer_t buffer);

extern int *peek_int(state_buffer_t buffer, size_t offset_top);

extern size_t buffer_size_int(state_buffer_t buffer);

#endif

