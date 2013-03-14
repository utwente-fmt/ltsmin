// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#include <hre/config.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <hre/unix.h>
#include <hre/user.h>
#include <util-lib/is-balloc.h>

#define INIT_MAX_BLOCKS (1024*64)

struct isb_allocator {
    size_t el_size;
    size_t max_blocks;
    size_t num_block;
    size_t cur_index;
    int** blocks;
};

/**
 \brief Create a block allocator

 Lazy block allocation:
 Block allocation is done before operation execution, thus the amount
 of blocks allocated is not invariant wrt the number of elements on the stack.
 Making this buffers block configuration nondeteministic.
 The operation is still correct since 0%BLOCK_SIZE==BLOCK_SIZE%BLOCK_SIZE,
 preserving the invariants:
 - the top-block always contains cur_index amount of elements.
 - there are always cur_block+1 blocks
 Therefore, the amount of elements on the stack==cur_block*BLOCK_SIZE+cur_index

 Sensible index calculations should thus be done wrt the the stack size
 (see peek_int).

NOTE: shrinkage of blocks pointer array is not implemented

 */

static const size_t BLOCK_ELT_POW = 20;
static const size_t BLOCK_ELT_SIZE = 1<<20; //blocksize in el_size

static void
add_block(isb_allocator_t buf)
{
    Debug("adding block %zu", buf->num_block-1);
    size_t size = ((buf->el_size)*sizeof(int))<<BLOCK_ELT_POW;
    int *block = RTmalloc(size);
    buf->blocks[buf->num_block] = block;
    buf->num_block++;
    buf->cur_index = 0;
}


/*!
 * @param buf the buffer to remove a memory block from
 */
static void
remove_block(isb_allocator_t buf)
{
    Debug("freeing block %zu", buf->num_block-1);
    buf->num_block--;
    RTfree(buf->blocks[buf->num_block]);
}

isb_allocator_t
isba_create(int element_size)
{
    isb_allocator_t res = RTmalloc(sizeof *res);
    res->el_size = element_size;
    res->max_blocks = INIT_MAX_BLOCKS;
    res->blocks = RTmalloc(res->max_blocks*sizeof(int*));
    res->num_block = 0;
    add_block(res);
    return res;
}

size_t
isba_elt_size (const isb_allocator_t buf)
{
    return buf->el_size;
}

void
isba_destroy(isb_allocator_t buf)
{
    do {remove_block(buf);} while (buf->num_block > 0);
    RTfree(buf->blocks);
    RTfree(buf);
}

/*
 \brief pushes an array of ints on this stack as one operation
 */
int *
isba_push_int(isb_allocator_t buf, const int *element)
{
    if (buf->cur_index == BLOCK_ELT_SIZE) {
        if (buf->num_block == buf->max_blocks) {
            buf->max_blocks += INIT_MAX_BLOCKS;
            buf->blocks=RTrealloc(buf->blocks, buf->max_blocks);
        }
        add_block(buf);
    }
    //size_t x;
    int *b = &buf->blocks[buf->num_block-1][buf->cur_index*buf->el_size];
    if (element)
        memcpy(b, element, buf->el_size*sizeof(int));
    buf->cur_index++;
    return b;
}

/*
 \brief virtually pops the top from the stack.
 it is not physically popped of, and a reference is provided to the top element
 of the stack this element is ensured to live until the next pop call and may
 live longer
 */
int *
isba_pop_int(isb_allocator_t buf)
{
    if (buf->cur_index == 0) {
        if (buf->num_block == 1) return NULL;
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
isba_to_string(isb_allocator_t buf)
{
    char* res;
    int ar[1];
    ar[0] = isba_size_int(buf) ? isba_top_int(buf)[0] : -1;
    int status = asprintf(&res, "LIFOBuffer[%zu | %zu * %zu + %zu | top1(%d)]",
                          buf->el_size, buf->num_block, BLOCK_ELT_SIZE,
                          buf->cur_index, ar[0]);
    if (status == -1)
        Abort("Could not allocate string.");

    return res;
}

/**
 \brief pops and discards a number of elements on the stack
 */
void
isba_discard_int(isb_allocator_t buf, size_t amount)
{
   if (amount > isba_size_int ( buf ) )
       Warning(info, "too high discard: %zu > %zu", amount, isba_size_int( buf ) );
   if (buf->cur_index < amount) {
        size_t blocks = amount>>BLOCK_ELT_POW;
        if (buf->num_block == 1 || buf->num_block <= blocks)
            Abort("Discard %zu on buffer of size %zu elements", amount, isba_size_int(buf));
        size_t x;
        for (x = 0; x <= blocks; x++) remove_block(buf);
        buf->cur_index = BLOCK_ELT_SIZE-(amount&(BLOCK_ELT_SIZE-1))+buf->cur_index;
    } else {
        buf->cur_index -= amount;
    }
}

size_t
isba_size_int(isb_allocator_t buf)
{
    return ((buf->num_block-1) << BLOCK_ELT_POW) + buf->cur_index;
}

/**
 \brief returns a pointer to the top element
 */
int *
isba_top_int(isb_allocator_t buf)
{
    if (buf->cur_index == 0) {
        if (buf->num_block == 1) Abort("Top on empty buffer");
        return &buf->blocks[buf->num_block-2][(BLOCK_ELT_SIZE-1)*buf->el_size];
    } else {
        return &buf->blocks[buf->num_block-1][(buf->cur_index-1)*buf->el_size];
    }
}

int *
isba_peek_int(isb_allocator_t buf, size_t offset_top)
{
    size_t size = isba_size_int(buf);
    if (offset_top >= size)
        return NULL;
        //Abort("peeks offset %zu is too large for a buffer with size %zu", offset_top, size);
    size_t newsize = size - (offset_top+1);
    size_t block = newsize>>BLOCK_ELT_POW;
    size_t rest = newsize&(BLOCK_ELT_SIZE-1);
    return &buf->blocks[block][rest * buf->el_size];
}

int *
isba_index(isb_allocator_t buf, size_t index)
{
    size_t size = isba_size_int(buf);
    if (index >= size)
        return NULL;
    size_t block = index >> BLOCK_ELT_POW;
    size_t rest = index & (BLOCK_ELT_SIZE-1);
    return &buf->blocks[block][rest * buf->el_size];
}
