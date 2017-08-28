// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/dir_ops.h>
#include <hre-io/user.h>
#include <lts-io/internal.h>
#include <util-lib/chunk_support.h>
#include <util-lib/tables.h>

struct lts_file_s{
    archive_t archive;
    stream_t init;
    struct_stream_t *state_vec;
    struct_stream_t *state_lbl;
    stream_t *edge_src_seg;
    struct_stream_t *edge_src_state;
    stream_t *edge_dst_seg;
    struct_stream_t *edge_dst_state;
    struct_stream_t *edge_lbl;
};

static void read_begin(lts_file_t file){
    lts_type_t ltstype=lts_file_get_type(file);
    char base[128];
    int segments=lts_file_get_segments(file);
    int SV=lts_type_get_state_length(ltstype);
    int SL=lts_type_get_state_label_count(ltstype);
    int K=lts_type_get_edge_label_count(ltstype);
    if (SV) file->state_vec=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
    if (SL) file->state_lbl=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
    if (K) file->edge_lbl=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
    file->edge_src_state=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
    file->edge_dst_state=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
    if (segments > 1) switch(lts_file_get_edge_owner(file)){
    case SourceOwned:
        file->edge_dst_seg=(stream_t*)RTmallocZero(segments*sizeof(stream_t));
        break;
    case DestOwned:
        file->edge_src_seg=(stream_t*)RTmallocZero(segments*sizeof(stream_t));
        break;
    default:
        Abort("missing case in vector read begin");
    }
    for(int i=0;i<lts_file_owned_count(file);i++){
        int seg=lts_file_owned(file,i);
        if (file->state_vec) {
            sprintf(base,"SV-%d-%%d",seg);
            file->state_vec[seg]=arch_read_vec_U32(file->archive,base,SV);
        }
        if (file->state_lbl) {
            sprintf(base,"SL-%d-%%d",seg);
            file->state_lbl[seg]=arch_read_vec_U32(file->archive,base,SL);
        }
        if (file->edge_src_seg) {
            sprintf(base,"ES-%d-seg",seg);
            file->edge_src_seg[seg]=arch_read(file->archive,base);
        }
        if (file->edge_dst_seg) {
            sprintf(base,"ED-%d-seg",seg);
            file->edge_dst_seg[seg]=arch_read(file->archive,base);
        }
        char* offset[1]={"ofs"};
        switch(lts_file_source_mode(file)){
        case Index:
            sprintf(base,"ES-%d-%%s",seg);
            file->edge_src_state[seg]=arch_read_vec_U32_named(file->archive,base,1,offset);
            break;
        case SegVector:
        case Vector:
            sprintf(base,"ES-%d-%%d",seg);
            file->edge_src_state[seg]=arch_read_vec_U32(file->archive,base,SV);
            break;
        }
        switch(lts_file_dest_mode(file)){
        case Index:
            sprintf(base,"ED-%d-%%s",seg);
            file->edge_dst_state[seg]=arch_read_vec_U32_named(file->archive,base,1,offset);
            break;
        case SegVector:
        case Vector:
            sprintf(base,"ED-%d-%%d",seg);
            file->edge_dst_state[seg]=arch_read_vec_U32(file->archive,base,SV);
            break;
        }
        if (file->edge_lbl){
            sprintf(base,"EL-%d-%%d",seg);
            file->edge_lbl[seg]=arch_read_vec_U32(file->archive,base,K);
        }
    }
}

static int read_init(lts_file_t file,int *seg,void* state){
    if (file->init==NULL || DSempty(file->init)) return 0;
    switch(lts_file_init_mode(file)){
    case Index:
        if (lts_file_get_segments(file)>1) {
            *seg=DSreadS32(file->init);
        } else {
            *seg=0;
        }
        *((uint32_t*)state)=DSreadS32(file->init);
        break;
    case SegVector:
        *seg=DSreadS32(file->init); // fall through
    case Vector:
        {
            lts_type_t ltstype=lts_file_get_type(file);
            int NV=lts_type_get_state_length(ltstype);
            for(int i=0;i<NV;i++){
                ((uint32_t*)state)[i]=DSreadS32(file->init);
            }
        }
        break;
    }
    return 1;
}

static int read_state(lts_file_t file,int *seg,void* state,void*labels){
    if (file->state_vec){
        if (DSreadStruct(file->state_vec[*seg],state)){
            if(file->state_lbl && !DSreadStruct(file->state_lbl[*seg],labels)) {
                Abort("label file is shorter than vector file");
            }
            return 1;
        }
        if (file->state_lbl && DSreadStruct(file->state_lbl[*seg],labels)) {
            Abort("label file is longer than vector file");
        }
    } else {
        if (DSreadStruct(file->state_lbl[*seg],labels)) return 1;
    }
    return 0;
}

static int read_edge(lts_file_t file,int *src_seg,void* src_state,
                         int *dst_seg,void*dst_state,void* labels
){
    int seg=0;
    switch(lts_file_get_edge_owner(file)){
    case SourceOwned:
        seg=*src_seg;
        if (!DSreadStruct(file->edge_src_state[seg],src_state)) return 0;
        if (file->edge_dst_seg) {
            *dst_seg=DSreadU32(file->edge_dst_seg[seg]);
        } else {
            *dst_seg=0;
        }
        break;
    case DestOwned:
        seg=*dst_seg;
        if (!DSreadStruct(file->edge_src_state[seg],src_state)) return 0;
        if (file->edge_src_seg) {
            *src_seg=DSreadU32(file->edge_src_seg[seg]);
        } else {
            *src_seg=0;
        }
        break;
    }
    DSreadStruct(file->edge_dst_state[seg],dst_state);
    if (file->edge_lbl) DSreadStruct(file->edge_lbl[seg],labels);
    return 1;
}

static void read_close(lts_file_t file){
    (void)file;
    // TO DO.
}


static value_table_t set_table(lts_file_t file,int type_no,value_table_t table){
    lts_type_t ltstype=lts_file_get_type(file);
    switch(lts_type_get_format(ltstype,type_no)){
    case LTStypeDirect:
    case LTStypeRange:
    case LTStypeBool:
    case LTStypeTrilean:
    case LTStypeSInt32:
        Abort("attempt to set table for an integer type");
        break;
    case LTStypeChunk:
    case LTStypeEnum:
        break;
    }
    stream_t ds;
    Debug("reading values of type %s",lts_type_get_type(ltstype,type_no));
    char stream_name[1024];
    sprintf(stream_name,"CT-%d",type_no);
    ds=arch_read(file->archive,stream_name);
    unsigned int L;
    for(L=0;;L++){
        if (DSempty(ds)) break;
        int len=DSreadVL(ds);
        char data[len];
        DSread(ds,data,len);
        VTputAtChunk (table, chunk_ld(len,data), L);
        Debug("element %u length %d",L,len);
    }
    if (L == 0) {
        lts_type_set_format(ltstype, type_no, LTStypeDirect);
    }
    Debug("%u elements in table %p",L,table);
    DSclose(&ds);
    return table;
}

static lts_file_t vector_open_old(stream_t ds,char* description,archive_t archive){
    int N;
    stream_t fifo=FIFOcreate(4096);
    if (description[3]!=' '){
        Abort("unknown format: %s",description);
    }
    description[3]=0;// extract mode and check
    if (strcmp(description,"-si") && strcmp(description,"vsi") &&
        strcmp(description,"-is") && strcmp(description,"vis") &&
        strcmp(description,"-ii")){
        Abort("unknown mode: %s",description);
    }
    if (strcmp(description+4,"1.0")){ // check version
        Abort("unknown version %s",description+4);
    }
    char *comment=DSreadSA(ds);
    Print(infoLong,"comment is %s",comment);
    N=DSreadU32(ds);
    Print(infoLong,"segment count is %d",N);
    int segment_count=N;
    N=DSreadVL(ds);
    uint32_t root_seg;
    uint32_t root_ofs;
    {
        char data[N];
        DSread(ds,data,N);
        DSwrite(fifo,data,N);
        root_seg=DSreadU32(fifo);
        root_ofs=DSreadU32(fifo);
        N=DSreadU32(fifo);
        if (N) {
            Print(infoLong,"state length is %d",N);
            for(int i=0;i<N;i++){
                DSreadU32(fifo);
            }
        }
        if (FIFOsize(fifo)) Abort("Too much data in initial state (%zu bytes)",FIFOsize(fifo));
    }
    N=DSreadVL(ds);
    lts_type_t ltstype;
    {
        char data[N];
        DSread(ds,data,N);
        DSwrite(fifo,data,N);
        ltstype=lts_type_deserialize(fifo);
        if (FIFOsize(fifo)) Abort("Too much data in lts type (%zu bytes)",FIFOsize(fifo));
        if (description[0]=='-') lts_type_set_state_length(ltstype,0);
    }
    lts_file_t file=lts_file_bare(arch_name(archive),ltstype,segment_count,NULL,sizeof(struct lts_file_s));
    Debug("getting counts");
    N=DSreadVL(ds);
    {
        char data[N];
        Debug("getting %d bytes",N);
        DSread(ds,data,N);
        DSwrite(fifo,data,N);
        N=segment_count;
        Debug("fifo now %zu bytes for %d segments",FIFOsize(fifo),N);
        for(int i=0;i<N;i++){
            uint32_t tmp=DSreadU32(fifo);
            Debug("segment %d has %u states",i,tmp);
            lts_set_state_count(file,i,tmp);
        }
        for(int i=0;i<N;i++){
            uint32_t tmp=DSreadU32(fifo);
            Debug("segment %d has %u edges",i,tmp);
            lts_set_edge_count(file,i,tmp);
        }
        if (FIFOsize(fifo)) Abort("Too much data in state and transition counts (%zu bytes)",FIFOsize(fifo));
    }
    Debug("getting compression tree");
    N=DSreadVL(ds);
    if (N) {
        Abort("Tree compression is unsupported in this version");
    }
    DSclose(&ds);
    switch(description[0]){
    case 'v':
        //lts_file_set_state_mode(file,Vector);
        break;
    case '-':
        //lts_file_set_state_mode(file,Implicit);
        break;
    default:
        Abort("unsupported mode: %s",description);
    }
    switch(description[1]){
    case 'i':
        lts_file_set_source_mode(file,Index);
        if (description[2]=='i'){
            lts_file_set_edge_owner(file,DestOwned);
        } else {
            lts_file_set_edge_owner(file,SourceOwned);
        }
        break;
    case 's':
        lts_file_set_source_mode(file,Index);
        lts_file_set_edge_owner(file,DestOwned);
        break;
    default:
        Abort("unsupported mode: %s",description);
    }
    switch(description[2]){
    case 'i':
        lts_file_set_dest_mode(file,Index);
        if (lts_file_get_edge_owner(file)!=DestOwned) Abort("inconsistent mode");
        break;
    case 's':
        lts_file_set_dest_mode(file,Index);
        if (lts_file_get_edge_owner(file)!=SourceOwned) Abort("inconsistent mode");
        break;
    default:
        Abort("unsupported mode: %s",description);
    }
    file->archive=archive;
    lts_file_set_init_mode(file,Index);
    lts_set_init_count(file,1);
    if (HREme(lts_file_context(file))==0) {
        file->init=FIFOcreate(32);
        if (lts_file_get_segments(file)>1) DSwriteU32(file->init,root_seg);
        DSwriteU32(file->init,root_ofs);
    }
    lts_file_set_read_init(file,read_init);
    lts_file_set_read_state(file,read_state);
    lts_file_set_read_edge(file,read_edge);
    lts_file_set_close(file,read_close);
    lts_file_set_table_callback(file,set_table);
    lts_file_complete(file);
    read_begin(file);
    return file;
}

lts_file_t vector_open(archive_t archive){
    Debug("opening info stream");
    stream_t ds=arch_read(archive,"info");
    char description[1024];
    Debug("getting description");
    DSreadS(ds,description,1024);
    Debug("file type is %s",description);
    if (strlen(description)==0) {
        if (31==DSreadS16(ds)) {
            Print(error,"this tool does not support legacy DIR");
            Abort("the file can be converted with ltstrans");
        }
    }
    if (strncmp(description,"vector",6)){
        return vector_open_old(ds,description,archive);
    }
    int N;
    int segment_count;
    stream_t fifo=FIFOcreate(4096);
    if (description[6]!=' '){
        Abort("unknown format: %s",description);
    }
    if (strcmp(description+7,"1.0")){ // check version
        Abort("unknown version %s",description+7);
    }
    char *comment=DSreadSA(ds);
    Print(infoLong,"comment is %s",comment);
    N=DSreadVL(ds);
    lts_type_t ltstype;
    {
        char data[N];
        DSread(ds,data,N);
        DSwrite(fifo,data,N);
        Debug("deserialize type pre");
        ltstype=lts_type_deserialize(fifo);
        Debug("deserialize type post");
        if (FIFOsize(fifo)) Abort("Too much data in lts type (%zu bytes)",FIFOsize(fifo));
    }
    lts_file_t file;
    Debug("getting state format info");
    N=DSreadVL(ds);
    {
        char data[N];
        DSread(ds,data,N);
        DSwrite(fifo,data,N);
        N=DSreadU32(fifo);
        Print(infoLong,"segment count is %d",N);
        segment_count=N;
        file=lts_file_bare(arch_name(archive),ltstype,segment_count,NULL,sizeof(struct lts_file_s));
        char*tmp=DSreadSA(fifo);
        Debug("initial state %s",tmp);
        if (strcmp(tmp,"i")==0) {
            lts_file_set_init_mode(file,Index);
        } else if (strcmp(tmp,"v")==0) {
            lts_file_set_init_mode(file,Vector);
        } else if (strcmp(tmp,"sv")==0) {
            lts_file_set_init_mode(file,SegVector);
        } else {
            Abort("unknown initial state representation %s",tmp);
        }
        RTfree(tmp);
        tmp=DSreadSA(fifo);
        Debug("edge owner %s",tmp);
        if (strcmp(tmp,"src")==0) {
            lts_file_set_edge_owner(file,SourceOwned);
        } else if (strcmp(tmp,"dst")==0) {
            lts_file_set_edge_owner(file,DestOwned);
        } else {
            Abort("illegal edge owner %s",tmp);
        }
        RTfree(tmp);
        tmp=DSreadSA(fifo);
        Debug("source mode %s",tmp);
        if (strcmp(tmp,"i")==0) {
            lts_file_set_source_mode(file,Index);
        } else if (strcmp(tmp,"v")==0) {
            lts_file_set_source_mode(file,Vector);
        } else if (strcmp(tmp,"sv")==0) {
            lts_file_set_source_mode(file,SegVector);
        } else {
            Abort("illegal source mode %s",tmp);
        }
        RTfree(tmp);
        tmp=DSreadSA(fifo);
        Debug("dest mode %s",tmp);
        if (strcmp(tmp,"i")==0) {
            lts_file_set_dest_mode(file,Index);
        } else if (strcmp(tmp,"v")==0) {
            lts_file_set_dest_mode(file,Vector);
        } else if (strcmp(tmp,"sv")==0) {
            lts_file_set_dest_mode(file,SegVector);
        } else {
            Abort("illegal dest mode %s",tmp);
        }
        RTfree(tmp);
        if (FIFOsize(fifo)) Abort("Too much data in lts layout description (%zu bytes)",FIFOsize(fifo));
    }
    Debug("getting counts");
    N=DSreadVL(ds);
    {
        char data[N];
        Debug("getting %d bytes",N);
        DSread(ds,data,N);
        DSwrite(fifo,data,N);
        N=segment_count;
        Debug("fifo now %zu bytes for %d segments",FIFOsize(fifo),N);
        {
            uint32_t tmp=DSreadU32(fifo);
            Debug("LTS has %u initial states",tmp);
            lts_set_init_count(file,tmp);
        }
        for(int i=0;i<N;i++){
            uint32_t tmp=DSreadU32(fifo);
            Debug("segment %d has %u states",i,tmp);
            lts_set_state_count(file,i,tmp);
        }
        for(int i=0;i<N;i++){
            uint32_t tmp=DSreadU32(fifo);
            Debug("segment %d has %u edges",i,tmp);
            lts_set_edge_count(file,i,tmp);
        }
        N=lts_type_get_type_count(ltstype);
        for(int i=0;i<N;i++){
            uint32_t tmp=DSreadU32(fifo);
            lts_set_expected_value_count(file,i,tmp);
        }
        if (FIFOsize(fifo)) Abort("Too much data in state and transition counts (%zu bytes)",FIFOsize(fifo));
    }
    Debug("getting compression tree");
    N=DSreadVL(ds);
    if (N) {
        Abort("Tree compression is unsupported in this version");
    }
    DSclose(&ds);
    file->init=arch_read(archive,"init");
    file->archive=archive;
    lts_file_set_read_init(file,read_init);
    lts_file_set_read_state(file,read_state);
    lts_file_set_read_edge(file,read_edge);
    lts_file_set_close(file,read_close);
    lts_file_set_table_callback(file,set_table);
    lts_file_complete(file);
    read_begin(file);
    return file;
}

lts_file_t gcf_file_open(const char* name){
    archive_t archive=arch_gcf_read(raf_unistd((char*)name));
    return vector_open(archive);
}

lts_file_t gcd_file_open(const char* name){
    Debug("attempting to open %s",name);
    archive_t archive=arch_dir_open((char*)name,lts_io_blocksize);
    Debug("directory was opened");
    arch_set_transparent_compression(archive);
    return vector_open(archive);
}

lts_file_t dir_file_open(const char* name){
    Debug("attempting to open %s",name);
    archive_t archive=arch_dir_open((char*)name,lts_io_blocksize);
    Debug("directory was opened");
    return vector_open(archive);
}


