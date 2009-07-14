#include "state-buffer.h"
#include <stdlib.h>
#include "runtime.h"
#include <unistd.h>

#define MAX_MEM 128ULL*1024*1024*1024

/**
 \brief Create a buffer

 Lazy block allocation:
 Block allocation is done before operation execution, thus the amount
 of blocks allocated is not invariant wrt the number of elements on the stack.
 Making this buffers block configuration indeteministic.
 The operation is still correct since 0%BLOCK_SIZE==BLOCK_SIZE%BLOCK_SIZE,
 preserving the invariants:
 - the top-block always contains cur_index amount of elements.
 - there are always cur_block+1 blocks
 Therefore, the amount of elements on the stack==cur_block*BLOCK_SIZE+cur_index

 Sensible index calculations should thus be done wrt the the stack size
 (see peek_int).

 */

static const size_t BLOCK_ELT_SIZE = 10000; //blocksize in el_size

void
add_block(state_buffer_t buf)
{
    Debug("adding block %zu", buf->num_block-1);
    int size = buf->el_size*sizeof(int)*BLOCK_ELT_SIZE;
    int *block = malloc(size);
    if (!block)
        Fatal(1, error, "alloc of %d, for %zu", size, buf->num_block);
    buf->blocks[buf->num_block] = block;
    buf->num_block++;
    buf->cur_index = 0;
}


/*!
 * @param buf the buffer to remove a memory block from
 */
void
remove_block(state_buffer_t buf)
{
    Debug("freeing block %zu", buf->num_block-1);
    buf->num_block--;
    free(buf->blocks[buf->num_block]);
}

state_buffer_t
create_buffer(int element_size)
{
    state_buffer_t res = malloc(sizeof(struct state_buffer));
    res->el_size = element_size;
    res->max_blocks = MAX_MEM/(BLOCK_ELT_SIZE*element_size);
    res->blocks = malloc(res->max_blocks*sizeof(int*));
    res->num_block = 0;
    add_block(res);
    return res;
}

void
destroy_buffer(state_buffer_t buf)
{
    do {remove_block(buf);} while (buf->num_block > 0);
    free(buf->blocks);
    free(buf);
}

/*
 \brief pushes an array of ints on this stack as one operation
 */
void
push_int(state_buffer_t buf, int* element)
{
    if (buf->cur_index == BLOCK_ELT_SIZE) {
        if (buf->num_block == buf->max_blocks)
            Fatal(1, error, "state-buffer mem size of %zu exceeded", MAX_MEM/1024/1024/1024);
        add_block(buf);
    }
    size_t x;
    for (x = 0; x < buf->el_size; x++) {
        buf->blocks[buf->num_block-1][buf->cur_index*buf->el_size+x] = element[x];
    }
    buf->cur_index++;
}

/*
 \brief virtually pops the top from the stack.
 it is not physically popped of, and a reference is provided to the top element
 of the stack this element is ensured to live until the next pop call and may
 live longer
 */
int *
pop_int(state_buffer_t buf)
{
    if (buf->cur_index == 0) {
        if (buf->num_block == 1) Fatal(1, error, "Pop on empty buffer");
        remove_block(buf);
        buf->cur_index = BLOCK_ELT_SIZE;
    }
    buf->cur_index--;
    return &buf->blocks[buf->num_block-1][buf->cur_index*buf->el_size];
}

/**
 * \return use free
 */
char *
buffer_to_string(state_buffer_t buf)
{
    char* res;
    int ar[1];
    ar[0] = buffer_size_int(buf) ? top_int(buf)[0] : -1;
    asprintf(&res, "LIFOBuffer[%zu | %zu * %zu + %zu | top1(%d)]", buf->el_size,
             buf->num_block, BLOCK_ELT_SIZE, buf->cur_index, ar[0]);
    return res;
}

/**
 \brief pops and discards a number of elements on the stack
 */
void
discard_int(state_buffer_t buf, size_t amount)
{
    if (buf->cur_index < amount) {
        size_t blocks = amount/BLOCK_ELT_SIZE;
        if (buf->num_block == 1 || buf->num_block <= blocks)
            Fatal(1, error, "Discard %zu on buffer of size %zu elements", amount, buffer_size_int(buf));
        size_t x;
        for (x = 0; x <= blocks; x++) remove_block(buf);
        buf->cur_index = BLOCK_ELT_SIZE-(amount%BLOCK_ELT_SIZE)+buf->cur_index;
    } else {
        buf->cur_index -= amount;
    }
}

size_t
buffer_size_int(state_buffer_t buf)
{
    return (buf->num_block-1) * BLOCK_ELT_SIZE + buf->cur_index;
}

/**
 \brief returns a pointer to the top element
 */
int *
top_int(state_buffer_t buf)
{
    if (buf->cur_index == 0) {
        if (buf->num_block == 1) Fatal(1, error, "Top on empty buffer");
        return &buf->blocks[buf->num_block-2][(BLOCK_ELT_SIZE-1)*buf->el_size];
    } else {
        return &buf->blocks[buf->num_block-1][(buf->cur_index-1)*buf->el_size];
    }
}

int *
peek_int(state_buffer_t buf, size_t offset_top)
{
    size_t size = buffer_size_int(buf);
    if (offset_top >= size)
        Fatal(1, error, "peeks offset %zu is too large for a buffer with size %zu", offset_top, size);
    size_t newsize = size - (offset_top+1);
    size_t block = newsize/BLOCK_ELT_SIZE;
    size_t rest = newsize%BLOCK_ELT_SIZE;
    return &buf->blocks[block][rest * buf->el_size];
}
