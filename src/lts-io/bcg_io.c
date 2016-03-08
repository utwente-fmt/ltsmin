// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <lts-io/internal.h>
#include <ltsmin-lib/ltsmin-standard.h>

#ifndef HAVE_BCG_USER_H

lts_file_t bcg_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    (void)name;(void)ltstype;(void)segments;(void)settings;
    Abort("BCG support not available");
}

lts_file_t bcg_file_open(const char* name){
    (void)name;
    Abort("BCG support not available");
}

#else

#include <pthread.h>
#include <bcg_user.h>

struct lts_file_s{
    value_table_t labels;
    long long unsigned int root;
    BCG_TYPE_OBJECT_TRANSITION graph;
    char*name;
};

static pthread_once_t bcg_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t bcg_write_lock;
static void bcg_init(){
    pthread_mutex_init(&bcg_write_lock, NULL);
    BCG_INIT();
}
static int bcg_write_busy=0;

static void bcg_file_push(lts_file_t src,lts_file_t dst){
    int type_no=lts_type_get_edge_label_typeno(lts_file_get_type(src),0);
    value_table_t labels=lts_file_get_table(src,type_no);
    unsigned int label_count=BCG_OT_NB_LABELS (src->graph);
    uint32_t label_map[label_count];
    for(unsigned int i=0;i<label_count;i++){
        char *lbl=BCG_OT_LABEL_STRING (src->graph,i);
        if (!BCG_OT_LABEL_VISIBLE (src->graph,i)){
            lbl=LTSMIN_EDGE_VALUE_TAU;
        }
        int lbl_len=strlen(lbl);
        if ((lbl[0]=='#' && lbl[lbl_len-1]=='#')||
            (lbl[0]=='"' && lbl[lbl_len-1]=='"'))
        {
            char tmp_data[lbl_len];
            chunk tmp_chunk=chunk_ld(lbl_len,tmp_data);
            string2chunk(lbl,&tmp_chunk);
            label_map[i]=VTputChunk(labels,tmp_chunk);
        } else {
            label_map[i]=VTputChunk(labels,chunk_ld(lbl_len,lbl));
        }
    }
    uint32_t root=src->root;
    lts_write_init(dst,0,&root);
    BCG_TYPE_STATE_NUMBER bcg_s1, bcg_s2;
    BCG_TYPE_LABEL_NUMBER bcg_label_number;
    BCG_OT_ITERATE_PLN (src->graph, bcg_s1, bcg_label_number, bcg_s2) {
        uint32_t src_ofs=bcg_s1;
        uint32_t dst_ofs=bcg_s2;
        uint32_t lbl=label_map[bcg_label_number];
        lts_write_edge(dst,0,&src_ofs,0,&dst_ofs,&lbl);
    } BCG_OT_END_ITERATE;
}

static void bcg_read_close(lts_file_t file){
    BCG_OT_READ_BCG_END (&file->graph);
}

lts_file_t bcg_file_open(const char* name){
    if (pthread_once(&bcg_once, bcg_init)){
        Abort("bcg init once");
    }
    lts_type_t ltstype=single_action_type();
    lts_file_t file=lts_file_bare(name,ltstype,1,NULL,sizeof(struct lts_file_s));
    lts_file_set_push(file,bcg_file_push);
    lts_file_set_close(file,bcg_read_close);
    lts_file_complete(file);
    Debug("bcg_read_begin %s",name);
    BCG_OT_READ_BCG_BEGIN ((char*)name, &file->graph, 0);
    BCG_TYPE_C_STRING bcg_comment;
    BCG_READ_COMMENT (BCG_OT_GET_FILE (file->graph), &bcg_comment);
    Print(infoLong,"comment is: %s",bcg_comment);
    file->root=BCG_OT_INITIAL_STATE (file->graph);
    return file;
}

static void bcg_write_init(lts_file_t file,int seg,void* state){
    uint32_t root=*((uint32_t*)state);
    if (seg!=0) Abort("bad initial state %u/%u",seg,root);
    if(file->name) {
        file->root=root;
        BCG_IO_WRITE_BCG_BEGIN(file->name,root,1,"LTSmin",0);
        RTfree(file->name);
        file->name=NULL;
    } else {
        if (file->root!=root) Abort("bad initial state %u/%u",seg,root);
    }
}

static void bcg_write_edge(lts_file_t file,int src_seg,void* src_state,
                           int dst_seg,void*dst_state,void* labels){
    (void)src_seg;(void)dst_seg;
    if(file->name) {
        BCG_IO_WRITE_BCG_BEGIN(file->name,0,1,"LTSmin",0);
        RTfree(file->name);
        file->name=NULL;
    }
    uint32_t src=*((uint32_t*)src_state);
    uint32_t dst=*((uint32_t*)dst_state);
    if (file->root) {
        if (src==file->root) {
            src=0;
        } else if (src==0) {
            src=file->root;
        }
        if (dst==file->root) {
            dst=0;
        } else if (dst==0) {
            dst=file->root;
        }
    }
    uint32_t lbl=*((uint32_t*)labels);
    if (file->labels==NULL){
        int type_no=lts_type_get_edge_label_typeno(lts_file_get_type(file),0);
        file->labels=lts_file_get_table(file,type_no);
    }
    chunk label_c;
    if (file->labels==NULL){
      label_c.len=6;
    } else {
      label_c=VTgetChunk(file->labels,lbl);
    }
    char label_s[label_c.len*2+6];
    if (file->labels==NULL){
        sprintf(label_s,"%u",lbl);
    } else {
        chunk2string(label_c,sizeof label_s,label_s);
    }
    char *bcg_label;
    if (strcmp(label_s,LTSMIN_EDGE_VALUE_TAU) && strcmp(label_s,"\"" LTSMIN_EDGE_VALUE_TAU "\"")){
        bcg_label=label_s;
    } else {
        bcg_label="i";
    }
    BCG_IO_WRITE_BCG_EDGE(src,bcg_label,dst);
}

static void bcg_write_close(lts_file_t file){
    (void)file;
    BCG_IO_WRITE_BCG_END();
    if (pthread_mutex_lock(&bcg_write_lock)){
        Abort("setting bcg write lock");
    }
    bcg_write_busy=0;
    if (pthread_mutex_unlock(&bcg_write_lock)){
        Abort("releasing bcg write lock");
    }
}

lts_file_t bcg_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    if (pthread_once(&bcg_once, bcg_init)){
        Abort("bcg init once");
    }
    if (lts_type_get_state_length(ltstype)) {
        Abort("cannot write state to BCG file");
    }
    if (lts_type_get_state_label_count(ltstype)) {
        Abort("cannot write state labels to BCG file");
    }
    if (lts_type_get_edge_label_count(ltstype)!=1) {
        Abort("BCG files contain precisely one edge label");
    }
    if (segments!=1) Abort("BCG files contain precisely 1 segment");
    if (pthread_once(&bcg_once, bcg_init)){
        Abort("bcg init once");
    }
    if (pthread_mutex_lock(&bcg_write_lock)){
        Abort("setting bcg write lock");
    }
    int busy=0;
    if (bcg_write_busy) {
        busy=1;
    } else {
        bcg_write_busy=1;
    }
    if (pthread_mutex_unlock(&bcg_write_lock)){
        Abort("releasing bcg write lock");
    }
    if (busy) Abort("cannot write more than one BCG file at once");
    lts_file_t file=lts_file_bare(name,ltstype,1,settings,sizeof(struct lts_file_s));
    file->name=HREstrdup(name);
    lts_file_set_write_init(file,bcg_write_init);
    lts_file_set_write_edge(file,bcg_write_edge);
    lts_file_set_close(file,bcg_write_close);
    lts_file_complete(file);
    return file;
}

#endif
