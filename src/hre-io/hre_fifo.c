// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre-io/stream_object.h>
#include <hre-io/user.h>

struct stream_s {
    struct stream_obj procs;
    size_t blocksize;
    size_t blocks;
    void*read_block;
    size_t read_idx;
    void*write_block;
    size_t write_idx;
};

static size_t fifo_read_max(stream_t fifo,void*buf,size_t count){
    if (fifo->blocks==1){
        if (count < (fifo->write_idx-fifo->read_idx)){
            // buffer will be non-empty after read.
            memcpy(buf,fifo->read_block+fifo->read_idx,count);
            fifo->read_idx+=count;
            return count;
        } else {
            // buffer will be empty after read.
            int len=fifo->write_idx-fifo->read_idx;
            memcpy(buf,fifo->read_block+fifo->read_idx,len);
            fifo->read_idx=sizeof(void*);
            fifo->write_idx=sizeof(void*);
            return len;
        }
    } else {
        if (count < (fifo->blocksize-fifo->read_idx)){
            // buffer will be non-empty after read.
            memcpy(buf,fifo->read_block+fifo->read_idx,count);
            fifo->read_idx+=count;
            return count;
        } else {
            // buffer will be empty after read.
            int len=fifo->blocksize-fifo->read_idx;
            memcpy(buf,fifo->read_block+fifo->read_idx,len);
            void*tmp=fifo->read_block;
            fifo->read_block=*((void**)tmp);
            RTfree(tmp);
            fifo->blocks--;
            fifo->read_idx=sizeof(void*);
            return len+fifo_read_max(fifo,buf+len,count-len);
        }
    }
}

static void fifo_write(stream_t fifo,void*buf,size_t count){
    if (count <= (fifo->blocksize-fifo->write_idx)){
        // write fits empty space in current block.
        memcpy(fifo->write_block+fifo->write_idx,buf,count);
        fifo->write_idx+=count;
    } else {
        // write to current block what we can.
        size_t len=fifo->blocksize-fifo->write_idx;
        memcpy(fifo->write_block+fifo->write_idx,buf,len);
        buf+=len;
        count-=len;
        len=fifo->blocksize-sizeof(void*);
        for(;;){
            // extend the fifo with one more block.
            void*new_blk=RTmalloc(fifo->blocksize);
            *((void**)new_blk)=NULL;
            *((void**)fifo->write_block)=new_blk;
            fifo->write_block=new_blk;
            fifo->blocks++;
            if (count < len){
                // The write fits the current buffer.
                memcpy(new_blk+sizeof(void*),buf,count);
                fifo->write_idx=sizeof(void*)+count;
                return;
            } else {
                // The write is more than the current buffer.
                memcpy(new_blk+sizeof(void*),buf,len);
                buf+=len;
                count-=len;
            }
        }
    }
}

static void fifo_flush(stream_t stream){
    (void)stream;
}
static int fifo_empty(stream_t stream){
    return (stream->blocks==1)&&(stream->read_idx==stream->write_idx);
}

static void FIFOdestroy(stream_t *fifo_p){
    stream_t fifo=*fifo_p;
    *fifo_p=NULL;
    void*tmp=fifo->read_block;
    while(tmp){
        void*next=*((void**)tmp);
        RTfree(tmp);
        tmp=next;
    }
    RTfree(fifo);
}

stream_t FIFOcreate(size_t blocksize){
    if(blocksize<=sizeof(void*)){
        Abort("block size must exceed pointer size");
    }
    stream_t fifo=RT_NEW(struct stream_s);
    stream_init(fifo);
    fifo->blocksize=blocksize;
    fifo->blocks=1;
    fifo->read_block=RTmalloc(blocksize);
    *((void**)fifo->read_block)=NULL;
    fifo->read_idx=sizeof(void*);
    fifo->write_block=fifo->read_block;
    fifo->write_idx=sizeof(void*);
    fifo->procs.read_max=fifo_read_max;
    fifo->procs.read=stream_default_read;
    fifo->procs.empty=fifo_empty;
    fifo->procs.close=FIFOdestroy;
    fifo->procs.write=fifo_write;
    fifo->procs.flush=fifo_flush;
    return fifo;
}

size_t FIFOsize(stream_t fifo){
    return (fifo->write_idx-fifo->read_idx)+((fifo->blocks-1)*(fifo->blocksize-sizeof(void*)));
}
