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
    FILE* f;
};

/*
The mCRL2 FSM parser cannot deal with \" so it's changed to ''
*/
static void fix_double_quote(char*str){
    if (str[0]!='\"') Abort("string does not start with double quote");
    int i=1;
    for(;str[i]!=0;i++){
        if(str[i]=='\\'&&str[i+1]=='\"'){
            str[i]='\'';
            str[i+1]='\'';
        }
    }
    if (str[i-1]!='\"') Abort("string does not end with double quote");
}

static void
write_chunk (lts_file_t dst, chunk label_c)
{
    char label_s[label_c.len * 2 + 6];
    chunk2string (label_c, sizeof label_s, label_s);
    fix_double_quote (label_s);
    fprintf (dst->f, " %s", label_s);
}

static void
write_table (lts_file_t dst, value_table_t table, char *name, char *sort)
{
    int C = table ? VTgetCount (table) : 0;
    fprintf (dst->f, "%s(%d) %s", name, C, sort);
    if (C > 0) {
        int last_idx = 0;
        table_iterator_t it = VTiterator (table);
        while (IThasNext (it)) {
            chunk label_c = ITnext (it);
            int idx = VTputChunk (table, label_c);
            while (last_idx < idx) { // fill non-dense indices
                write_chunk(dst, (chunk){0, ""});
                last_idx++;
            }
            write_chunk (dst, label_c);
            last_idx++;
        }
    }
    fprintf (dst->f, "\n");
}

static void fsm_pull(lts_file_t dst,lts_file_t src){
    lts_type_t ltstype=lts_file_get_type(src);
    int N1=lts_type_get_state_length(ltstype);
    int N2=lts_type_get_state_label_count(ltstype);
    int K=lts_type_get_edge_label_count(ltstype);
    Debug("check src requirements.");
    if(lts_file_init_mode(src)!=Index){
        Abort("init mode is not index");
    }
    if(lts_file_source_mode(src)!=Index){
        Abort("source mode is not index");
    }
    if(lts_file_dest_mode(src)!=Index){
        Abort("dest mode is not index");
    }
    uint32_t root;
    int dummy;
    if (!lts_read_init(src,&dummy,&root)){
        Abort("no initial state");
    }
    if (lts_read_init(src,&dummy,&root)){
        Abort("multiple initial states");
    }
    if (root!=0){
        Abort("initial state is not 0");
    }
    Debug("write state vectors specs.");
    for(int i=0;i<N1;i++){
        char* name=lts_type_get_state_name(ltstype,i);
        char* sort=lts_type_get_state_type(ltstype,i);
        int type_no=lts_type_get_state_typeno(ltstype,i);
        value_table_t table=lts_file_get_table(dst,type_no);
        write_table (dst, table, name, sort);
    }
    Debug("write state label specs.");
    for(int i=0;i<N2;i++){
        char* name=lts_type_get_state_label_name(ltstype,i);
        char* sort=lts_type_get_state_label_type(ltstype,i);
        int type_no=lts_type_get_state_label_typeno(ltstype,i);
        value_table_t table=lts_file_get_table(dst,type_no);
        write_table (dst, table, name, sort);
    }
    fprintf(dst->f,"---\n");
    if (N1+N2>0){
        Debug("writing state vector and/or labels.");
        int src_seg;
        // Without vector, a number could be read. TODO: decide if that is allowed.
        uint32_t src_state[N1==0?1:N1];
        uint32_t state_labels[N2];
        while(lts_read_state(src,&src_seg,src_state,state_labels)){
            for(int i=0;i< N1;i++){
                fprintf(dst->f," %d",src_state[i]);
            }
            for(int i=0;i< N2;i++){
                fprintf(dst->f," %d",state_labels[i]);
            }
            fprintf(dst->f,"\n");
        }
    }
    fprintf(dst->f,"---\n");
    {
        Debug("writing transitions.");
        int src_seg;
        uint32_t src_idx;
        int dst_seg;
        uint32_t dst_idx;
        uint32_t edge_labels[K];
        while(lts_read_edge(src,&src_seg,&src_idx,&dst_seg,&dst_idx,edge_labels)){
            fprintf(dst->f,"%d %d",src_idx+1,dst_idx+1);
            for(int i=0;i<K;i++){
                int type_no=lts_type_get_edge_label_typeno(ltstype,i);
                value_table_t table=lts_file_get_table(dst,type_no);
                if (table==NULL){
                    fprintf(dst->f," %d",edge_labels[i]);
                } else {
                    chunk label_c=VTgetChunk(table,edge_labels[i]);
                    char label_s[label_c.len*2+6];
                    chunk2string(label_c,sizeof label_s,label_s);
                    fix_double_quote(label_s);
                    fprintf(dst->f," %s",label_s);
                }
            }
            fprintf(dst->f,"\n");
        }
    }
}


static void fsm_write_close(lts_file_t file){
    if (fclose(file->f)){
        AbortCall("while closing %s",lts_file_get_name(file));
    }
}

lts_file_t fsm_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    if (segments!=1) Abort("FSM files contain precisely 1 segment");
    lts_file_t file=lts_file_bare(name,ltstype,1,settings,sizeof(struct lts_file_s));
    file->f=fopen(name,"w");
    if(file->f==NULL){
        AbortCall("while opening %s",name);
    }
    lts_file_set_pull(file,fsm_pull);
    lts_file_set_close(file,fsm_write_close);
    lts_file_complete(file);
    return file;
}
