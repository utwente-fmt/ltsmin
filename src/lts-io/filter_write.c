// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <lts-io/internal.h>

struct lts_file_s{
    /// The file to which write requests are passed on.
    lts_file_t filtered;
    /// A projection from type numbers in the caller to type number in the LTS being written.
    int* type_proj;
    /// The number of edge labels in the written LTS.
    int edge_count;
    /// The number of state labels in the written LTS.
    int state_count;
    /// Mapping from state label indices in the LTS being written to those in the caller.
    int* edge_proj;
    /** Mapping from state label indices in the LTS being written to those in the caller.
        State vector i is denoted 2*i+1.
        State label i is denoted 2*i.
     */
    int* state_proj;
};

static void write_state(lts_file_t file,int seg,void* state,void*labels){
    uint32_t dummy=lts_get_state_count(file->filtered,seg);
    uint32_t new_labels[file->state_count];
    for(int i=0;i<file->state_count;i++){
        if(file->state_proj[i]&1){
            // LSB set means state vector.
            new_labels[i]=((uint32_t*)state)[file->state_proj[i]>>1];
        } else {
            // LSB clear means state label.
            new_labels[i]=((uint32_t*)labels)[file->state_proj[i]>>1];
        }
    }
    lts_write_state(file->filtered,seg,&dummy,new_labels);
    (void)state;
}

static void write_init(lts_file_t file,int seg,void* state){
    lts_write_init(file->filtered,seg,state);
}


static void write_edge(lts_file_t file,int src_seg,void* src_state,
                           int dst_seg,void*dst_state,void* labels){
    uint32_t new_labels[file->edge_count];
    for(int i=0;i<file->edge_count;i++){
        new_labels[i]=((uint32_t*)labels)[file->edge_proj[i]];
    }
    lts_write_edge(file->filtered,src_seg,src_state,dst_seg,dst_state,new_labels);
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

static lts_file_t lts_file_create_filtered(const char* name,lts_type_t ltstype,string_set_t filter,
    int allow_vector,int segments,lts_file_t settings
){
    lts_file_t file=lts_file_bare(name,ltstype,segments,settings,sizeof(struct lts_file_s));
    int NV=lts_type_get_state_length(ltstype);
    int NS=lts_type_get_state_label_count(ltstype);
    int NE=lts_type_get_edge_label_count(ltstype);
    int NT=lts_type_get_type_count(ltstype);
    file->type_proj=(int*)RTmalloc(NT*sizeof(int));
    file->edge_count=0;
    file->edge_proj=(int*)RTmalloc(NE*sizeof(int));
    file->state_count=0;
    file->state_proj=(int*)RTmalloc((NV+NS)*sizeof(int));
    lts_type_t filtertype=lts_type_create();
    for(int i=0;i<NT;i++) file->type_proj[i]=-1;
    Debug("filter state parameters");
    if (allow_vector) for(int i=0;i<NV;i++){
        char* name=lts_type_get_state_name(ltstype,i);
        if(SSMmember(filter,name)){
            Print(infoShort,"keeping %s",name);
            file->state_proj[file->state_count]=(i<<1)+1;
            file->state_count++;
            int type_no=lts_type_get_state_typeno(ltstype,i);
            if (file->type_proj[type_no]<0){
                char* type=lts_type_get_state_type(ltstype,i);
                data_format_t format=lts_type_get_format(ltstype,type_no);
                file->type_proj[type_no]=lts_type_put_type(filtertype,type,format,NULL);
            }
        } else {
            Print(infoShort,"discarding %s",name);
        }
    }
    Debug("filter state labels");
    for(int i=0;i<NS;i++){
        char* name=lts_type_get_state_label_name(ltstype,i);
        if(SSMmember(filter,name)){
            Print(infoShort,"keeping %s",name);
            file->state_proj[file->state_count]=(i<<1);
            file->state_count++;
            int type_no=lts_type_get_state_label_typeno(ltstype,i);
            if (file->type_proj[type_no]<0){
                char* type=lts_type_get_state_label_type(ltstype,i);
                data_format_t format=lts_type_get_format(ltstype,type_no);
                file->type_proj[type_no]=lts_type_put_type(filtertype,type,format,NULL);
            }
        } else {
            Print(infoShort,"discarding %s",name);
        }
    }
    Debug("declare %d state labels for filtered LTS",file->state_count);
    lts_type_set_state_label_count(filtertype,file->state_count);
    for(int i=0;i<file->state_count;i++){
        char* name;
        int type_no;
        if (file->state_proj[i]&1){
            name=lts_type_get_state_name(ltstype,file->state_proj[i]>>1);
            type_no=lts_type_get_state_typeno(ltstype,file->state_proj[i]>>1);
       } else {
            name=lts_type_get_state_label_name(ltstype,file->state_proj[i]>>1);
            type_no=lts_type_get_state_label_typeno(ltstype,file->state_proj[i]>>1);
        }
        lts_type_set_state_label_name(filtertype,i,name);
        lts_type_set_state_label_typeno(filtertype,i,file->type_proj[type_no]);
    }
    Debug("filter edge labels");
    for(int i=0;i<NE;i++){
        char* name=lts_type_get_edge_label_name(ltstype,i);
        if(SSMmember(filter,name)){
            Print(infoShort,"keeping %s",name);
            file->edge_proj[file->edge_count]=i;
            file->edge_count++;
            int type_no=lts_type_get_edge_label_typeno(ltstype,i);
            if (file->type_proj[type_no]<0){
                char* type=lts_type_get_edge_label_type(ltstype,i);
                data_format_t format=lts_type_get_format(ltstype,type_no);
                file->type_proj[type_no]=lts_type_put_type(filtertype,type,format,NULL);
            }
        } else {
            Print(infoShort,"discarding %s",name);
        }
    }
    Debug("declare edge labels for filtered LTS");
    lts_type_set_edge_label_count(filtertype,file->edge_count);
    for(int i=0;i<file->edge_count;i++){
        char* name=lts_type_get_edge_label_name(ltstype,file->edge_proj[i]);
        int type_no=lts_type_get_edge_label_typeno(ltstype,file->edge_proj[i]);
        lts_type_set_edge_label_name(filtertype,i,name);
        lts_type_set_edge_label_typeno(filtertype,i,file->type_proj[type_no]);
    }
    Debug("full type is");
    if (log_active(debug)){
        lts_type_printf(debug,ltstype);
    }
    Debug("filtered type is");
    if (log_active(debug)){
        lts_type_printf(debug,filtertype);
    }
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

lts_file_t lts_file_create_nostate(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    return lts_file_create_filtered(name,ltstype,SSMcreateSWPset("*"),0,segments,settings);
}

lts_file_t lts_file_create_filter(const char* name,lts_type_t ltstype,string_set_t filter,int segments,lts_file_t settings){
    return lts_file_create_filtered(name,ltstype,filter,1,segments,settings);
}



