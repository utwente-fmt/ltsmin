// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>

#include <hre/user.h>
#include <lts-io/internal.h>

struct lts_file_s{
    lts_file_t filtered;
    int* type_proj;
};

static void write_state(lts_file_t file,int seg,void* state,void*labels){
    uint32_t dummy=lts_get_state_count(file->filtered,seg);
    lts_write_state(file->filtered,seg,&dummy,labels);
    (void)state;
}

static void write_init(lts_file_t file,int seg,void* state){
    lts_write_init(file->filtered,seg,state);
}


static void write_edge(lts_file_t file,int src_seg,void* src_state,
                           int dst_seg,void*dst_state,void* labels){
    lts_write_edge(file->filtered,src_seg,src_state,dst_seg,dst_state,labels);
}

static void write_close(lts_file_t file){
    lts_file_close(file->filtered);
    RTfree(file->type_proj);
}

static value_table_t set_table(lts_file_t lts,int type_no,value_table_t table){
    // if the type is used by the filtered stream, pass the table on.
    if (lts->type_proj[type_no]>=0) {
        lts_file_set_table(lts->filtered,lts->type_proj[type_no],table);
    }
    // in any case return the table unmodified.
    return table;
}

static stream_t attach(lts_file_t lts,char *name){
    return lts_file_attach(lts->filtered,name);
}

lts_file_t lts_file_create_nostate(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    lts_file_t file=lts_file_bare(name,ltstype,segments,settings,sizeof(struct lts_file_s));
    int NS=lts_type_get_state_label_count(ltstype);
    int NE=lts_type_get_edge_label_count(ltstype);
    int NT=lts_type_get_type_count(ltstype);
    file->type_proj=(int*)RTmalloc(NT*sizeof(int));
    for(int i=0;i<NT;i++) file->type_proj[i]=-1;
    lts_type_t filtertype=lts_type_create();
    lts_type_set_edge_label_count(filtertype,NE);
    for(int i=0;i<NE;i++){
        char* name=lts_type_get_edge_label_name(ltstype,i);
        int type_no=lts_type_get_edge_label_typeno(ltstype,i);
        if (file->type_proj[type_no]<0){
            char* type=lts_type_get_edge_label_type(ltstype,i);
            data_format_t format=lts_type_get_format(ltstype,i);
            file->type_proj[type_no]=lts_type_put_type(filtertype,type,format,NULL);
        }
        lts_type_set_edge_label_name(filtertype,i,name);
        lts_type_set_edge_label_typeno(filtertype,i,file->type_proj[type_no]);
    }
    lts_type_set_state_label_count(filtertype,NS);
    for(int i=0;i<NS;i++){
        char* name=lts_type_get_state_label_name(ltstype,i);
        int type_no=lts_type_get_state_label_typeno(ltstype,i);
        if (file->type_proj[type_no]<0){
            char* type=lts_type_get_state_label_type(ltstype,i);
            data_format_t format=lts_type_get_format(ltstype,i);
            file->type_proj[type_no]=lts_type_put_type(filtertype,type,format,NULL);
        }
        lts_type_set_state_label_name(filtertype,i,name);
        lts_type_set_state_label_typeno(filtertype,i,file->type_proj[type_no]);
    }
    Debug("full type is");
    lts_type_print(debug,ltstype);
    Debug("filtered type is");
    lts_type_print(debug,filtertype);
    for (int i=0;i<NT;i++){
        if (file->type_proj[i]==-1){
            Debug("type %d is dropped",i);
        } else {
            Debug("type %d maps to %d",i,file->type_proj[i]);
        }
    }
    file->filtered=lts_file_create(name,filtertype,segments,settings);
    lts_file_set_write_init(file,write_init);
    lts_file_set_write_state(file,write_state);
    lts_file_set_write_edge(file,write_edge);
    lts_file_set_close(file,write_close);
    lts_file_set_table_callback(file,set_table);
    lts_file_set_attach(file,attach);
    lts_file_complete(file);
    return file;
}

