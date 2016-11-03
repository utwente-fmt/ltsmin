// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>

#include <hre/user.h>
#include <lts-io/internal.h>
#include <util-lib/chunk_support.h>
#include <util-lib/tables.h>

struct lts_file_s{
    int type_no;
    long long unsigned int root;
    long long unsigned int states;
    long long unsigned int trans;
    FILE* f;
};

static void aut_write_init(lts_file_t lts,int seg,void* state){
    assert(seg==0); (void) seg;
    if (lts->root < lts->states) Abort("at most one root allowed in AUT");
    uint32_t root=*((uint32_t*)state);
    if (root!=0) Print(infoShort,"Detected non-zero root in AUT");
    lts->root=root;
    if (lts->root >= lts->states) lts->states=lts->root+1;
}

static void write_state(lts_file_t file,int seg,void* state,void*labels){
    (void)seg;
    (void)file;
    (void)state;
    (void)labels;
}

static void aut_write_edge(lts_file_t file,int src_seg,void* src_state,
                           int dst_seg,void*dst_state,void* labels){
    (void)src_seg;(void)dst_seg;
    uint32_t src=*((uint32_t*)src_state);
    uint32_t dst=*((uint32_t*)dst_state);
    uint32_t lbl=*((uint32_t*)labels);
    if (src>=file->states) file->states=src+1;
    if (dst>=file->states) file->states=dst+1;
    file->trans++;
    value_table_t table=lts_file_get_table(file,file->type_no);
    if (table==NULL) {
        fprintf(file->f,"(%llu,%llu,%llu)\n",
                (long long unsigned int)src,
                (long long unsigned int)lbl,
                (long long unsigned int)dst);
    } else {
        chunk label_c=VTgetChunk(table,lbl);
        char label_s[label_c.len*2+6];
        chunk2string(label_c,sizeof label_s,label_s);
        fprintf(file->f,"(%llu,%s,%llu)\n",
                (long long unsigned int)src,
                label_s,
                (long long unsigned int)dst);
    }
}

static void aut_write_close(lts_file_t file){
    if (fseek(file->f, 0L, SEEK_SET)){
        AbortCall("while rewinding %s",lts_file_get_name(file));
    }
    fprintf(file->f,"des(%llu,%llu,%llu)",file->root,file->trans,file->states);
    if (fclose(file->f)){
        AbortCall("while closing %s",lts_file_get_name(file));
    }
}

lts_file_t aut_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    if (lts_type_get_state_length(ltstype)) {
        //Abort("cannot write state to AUT file");
        Print(infoShort,"Ignoring state vector");
    }
    if (lts_type_get_state_label_count(ltstype)) {
        Abort("cannot write state labels to AUT file");
    }
    if (lts_type_get_edge_label_count(ltstype)!=1) {
        Abort("AUT files contain precisely one edge label");
    }
    if (segments!=1) Abort("AUT files contain precisely 1 segment");
    lts_file_t file=lts_file_bare(name,ltstype,1,settings,sizeof(struct lts_file_s));
    file->f=fopen(name,"w");
    if(file->f==NULL){
        AbortCall("while opening %s",name);
    }
    file->root--; // set to -1 denoting undefined.
    file->states++; // set to 1, denoting one state.
    file->type_no=lts_type_get_edge_label_typeno(ltstype,0);
    lts_file_set_write_init(file,aut_write_init);
    lts_file_set_write_state(file,write_state);
    lts_file_set_write_edge(file,aut_write_edge);
    lts_file_set_close(file,aut_write_close);
    fprintf(file->f,"des(?,?,?)                                                     \n");
    lts_file_complete(file);
    return file;
}

static int aut_read_init(lts_file_t lts,int *seg,void* state){
    if (lts->root >= lts->states) return 0;
    *seg=0;
    *((uint32_t*)state)=lts->root;
    lts->root=0;
    lts->root--;
    return 1;
}


#define BUFFER_SIZE 2048

static int aut_read_edge(lts_file_t file,int *src_seg,void* src_state,
                         int *dst_seg,void*dst_state,void* labels){
    *src_seg=0;
    *dst_seg=0;
    char buffer[BUFFER_SIZE];
    errno=0;
    if (fgets(buffer,BUFFER_SIZE,file->f)==NULL){
        if (errno) AbortCall("while reading %s",lts_file_get_name(file));
        return 0;
    }
    int from_idx,from_end,label_idx,to_idx,i;
// find destination state and end of label
    for(i=strlen(buffer);!isdigit((unsigned char)buffer[i]);i--);
    buffer[i+1]=0;
    for(;isdigit((unsigned char)buffer[i]);i--);
    to_idx=i+1;
    for(;buffer[i]!=',';i--);
    for(i--;isblank((unsigned char)buffer[i]);i--);
    buffer[i+1]=0;
// find source state and begin of label
    for(i=0;!isdigit((unsigned char)buffer[i]);i++);
    from_idx=i;
    for(;isdigit((unsigned char)buffer[i]);i++);
    from_end=i;
    for(;buffer[i]!=',';i++);
    for(i++;isblank((unsigned char)buffer[i]);i++);
    buffer[from_end]='\00';
    label_idx=i;
    int lbl_len=strlen(buffer+label_idx);
    char tmp_data[lbl_len];
    chunk tmp_chunk=chunk_ld(lbl_len,tmp_data);
    string2chunk(buffer+label_idx,&tmp_chunk);
// write the results
    *((uint32_t*)src_state)=(uint32_t)atoll(buffer+from_idx);
    *((uint32_t*)labels)=VTputChunk(lts_file_get_table(file,file->type_no),tmp_chunk);
    *((uint32_t*)dst_state)=(uint32_t)atoll(buffer+to_idx);
    return 1;

}

static void aut_read_close(lts_file_t file){
    if (fclose(file->f)){
        AbortCall("while closing %s",lts_file_get_name(file));
    }
}

lts_file_t aut_file_open(const char* name){
    lts_type_t ltstype=single_action_type();
    lts_file_t file=lts_file_bare(name,ltstype,1,NULL,sizeof(struct lts_file_s));
    file->type_no=lts_type_get_edge_label_typeno(ltstype,0);
    lts_file_set_read_init(file,aut_read_init);
    lts_file_set_read_edge(file,aut_read_edge);
    lts_file_set_close(file,aut_read_close);
    lts_file_complete(file);
    file->f=fopen(name,"r");
    if(file->f==NULL){
        AbortCall("while opening %s",name);
    }
    int matches;
    matches = fscanf(file->f,"des%*[^(]s");
    if (matches != 0) Abort("while parsing %s",name);
    matches = fscanf(file->f,"(%llu,%llu,%llu)\n",&file->root,&file->trans,&file->states);
    if (matches != 3) Abort("while parsing %s",name);
    Print(infoLong,"file %s contains %llu states and %llu transitions",
         name,file->states,file->trans);
    return file;
}

