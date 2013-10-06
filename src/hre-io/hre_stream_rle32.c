// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <string.h>

#include <hre/unix.h>
#include <hre-io/stream_object.h>
#include <hre-io/user.h>

typedef int32_t diff_t ;
#define hton hton_32
#define ntoh ntoh_32

#define RLE_WINDOW_SIZE 32768

struct stream_s {
    struct stream_obj procs;
    stream_t s;
    diff_t rd_val;
    int rd_count;
    int rd_copy;

    uint64_t wr_orig;
    diff_t *wr_window;
    int wr_copy;
    int wr_count;
};


static size_t rle_read_max(stream_t stream,void*buf,size_t count){
    if (count%sizeof(diff_t)) {
        Abort("misaligned write");
    }
    int max=count/sizeof(diff_t);
    for(int i=0;i<max;i++){
        if (stream->rd_count==0 && stream->rd_copy==0){
            if (DSempty(stream->s)) return i*sizeof(diff_t);
            int count=DSreadU16(stream->s);
            if (count&1){
                stream->rd_count=count>>1;
                stream_read(stream->s,&stream->rd_val,sizeof(diff_t));
            } else {
                stream->rd_copy=count>>1;
            }
        }
        if (stream->rd_count){
            ((diff_t*)buf)[i]=stream->rd_val;
            stream->rd_count--;
        }
        if (stream->rd_copy){
            stream_read(stream->s,((diff_t*)buf)+i,sizeof(diff_t));
            stream->rd_copy--;
        }
    }
    return count;
}

static int rle_empty(stream_t stream){
    if (stream->rd_count||stream->rd_copy) return 0;
    return stream_empty(stream->s);
}

static void rle_flush_window(stream_t stream){
    if (stream->wr_count) {
        DSwriteU16(stream->s,(stream->wr_count<<1)+1);
        stream_write(stream->s,stream->wr_window,sizeof(diff_t));
        stream->wr_count=0;
    }
    if (stream->wr_copy) {
        DSwriteU16(stream->s,stream->wr_copy<<1);
        stream_write(stream->s,stream->wr_window,stream->wr_copy*sizeof(diff_t));
        stream->wr_copy=0;
    }
}

static void rle_write(stream_t stream,void*buf,size_t count){
    stream->wr_orig+=count;
    if (count%sizeof(diff_t)) {
        Abort("misaligned write");
    }
    int len=count/sizeof(diff_t);
    for(int i=0;i<len;i++){
        if(stream->wr_count){
            if (stream->wr_window[0]==((diff_t*)buf)[i]){
                stream->wr_count++;
            } else {
                rle_flush_window(stream);
                stream->wr_window[0]=((diff_t*)buf)[i];
                stream->wr_copy=1;
            }
        } else if (stream->wr_copy) {
            if (stream->wr_window[stream->wr_copy-1]==((diff_t*)buf)[i]){
                stream->wr_copy--;
                rle_flush_window(stream);
                stream->wr_window[0]=((diff_t*)buf)[i];
                stream->wr_count=2;
            } else {
                stream->wr_window[stream->wr_copy]=((diff_t*)buf)[i];
                stream->wr_copy++;
            }
        } else {
            stream->wr_window[0]=((diff_t*)buf)[i];
            stream->wr_copy=1;
        }
        if (stream->wr_count+1==RLE_WINDOW_SIZE) rle_flush_window(stream);
        if (stream->wr_copy+1==RLE_WINDOW_SIZE) rle_flush_window(stream);
    }
}

static void rle_close(stream_t *stream){
    if (stream_writable((*stream)->s)){
        rle_flush_window(*stream);
        stream_close_z(&((*stream)->s),(*stream)->wr_orig);
    } else {
        stream_close(&((*stream)->s));
    }
    RTfree(*stream);
    *stream=NULL;
}

static void rle_flush(stream_t stream){
    rle_flush_window(stream);
    stream_flush(stream->s);
}

stream_t stream_rle32(stream_t s){
    stream_t ds=(stream_t)HREmalloc(NULL,sizeof(struct stream_s));
    stream_init(ds);
    ds->s=s;
    if (stream_readable(s)) {
        ds->procs.read_max=rle_read_max;
        ds->procs.read=stream_default_read;
        ds->procs.empty=rle_empty;
        ds->rd_count=0;
        ds->rd_copy=0;
    }
    if (stream_writable(s)){
        ds->procs.write=rle_write;
        ds->procs.flush=rle_flush;
        ds->wr_orig=0;
        ds->wr_count=0;
        ds->wr_copy=0;
        ds->wr_window=(diff_t*)HREmalloc(NULL,RLE_WINDOW_SIZE*sizeof(diff_t));
    }
    ds->procs.close=rle_close;
    return ds;
}

stream_t stream_unrle32(stream_t s){
    (void)s;
    Abort("Sorry, unrle32 is not implemented yet.");
}


