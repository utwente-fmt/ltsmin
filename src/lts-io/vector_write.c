// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <assert.h>
#include <libgen.h>
#include <string.h>

#include <hre/dir_ops.h>
#include <hre-io/user.h>
#include <lts-io/internal.h>
#include <util-lib/chunk_support.h>
#include <util-lib/tables.h>

/* Should this be an option? */
static char* compression_policy="*ofs:diff32|gzip;SV*:rle32|gzip;?L*:rle32|gzip;info:;gzip";

struct lts_file_s{
    archive_t archive;
    stream_t init;
    struct_stream_t *state_vec;
    struct_stream_t *state_lbl;
    stream_t *src_seg;
    struct_stream_t *src_vec;
    stream_t *src_ofs;
    stream_t *dst_seg;
    struct_stream_t *dst_vec;
    stream_t *dst_ofs;
    struct_stream_t *edge_lbl;
    value_table_t *type_table;
};

static void write_state(lts_file_t file,int seg,void* state,void*labels){
    if (file->state_vec) {
        if (file->state_vec[seg]==NULL) Abort("segment is not owned");
        DSwriteStruct(file->state_vec[seg],state);
    }
    if (file->state_lbl) {
        if (file->state_lbl[seg]==NULL) Abort("segment is not owned");
        DSwriteStruct(file->state_lbl[seg],labels);
    }
}

static void write_init(lts_file_t file,int seg,void* state){
    int me=HREme(lts_file_context(file));
    if (me) Abort("initial state at worker %d instead of 0",me);
    if (file->init==NULL) Abort("initial states are implicit!");
    switch(lts_file_init_mode(file)){
    case Index:
        if (lts_file_get_segments(file)>1) DSwriteS32(file->init,seg);
        DSwriteU32(file->init,*((uint32_t*)state));
        break;
    case SegVector:
        DSwriteS32(file->init,seg); // fall through
    case Vector:
        {
            lts_type_t ltstype=lts_file_get_type(file);
            int NV=lts_type_get_state_length(ltstype);
            for(int i=0;i<NV;i++){
                DSwriteU32(file->init,((uint32_t*)state)[i]);
            }
        }
        break;
    }
}

static void write_edge(lts_file_t file,int src_seg,void* src_state,
                           int dst_seg,void*dst_state,void* labels){
    int i=0;
    switch(lts_file_get_edge_owner(file)){
    case SourceOwned:
        i=src_seg;
        break;
    case DestOwned:
        i=dst_seg;
        break;
    }
    if (file->src_seg) DSwriteU32(file->src_seg[i],src_seg);
    if (file->src_ofs) DSwriteU32(file->src_ofs[i],*((uint32_t*)src_state));
    if (file->src_vec) DSwriteStruct(file->src_vec[i],src_state);
    if (file->dst_seg) DSwriteU32(file->dst_seg[i],dst_seg);
    if (file->dst_ofs) DSwriteU32(file->dst_ofs[i],*((uint32_t*)dst_state));
    if (file->dst_vec) DSwriteStruct(file->dst_vec[i],dst_state);
    if (file->edge_lbl) DSwriteStruct(file->edge_lbl[i],labels);
}


static void write_begin(lts_file_t file){
    int me=HREme(lts_file_context(file));
    lts_type_t ltstype=lts_file_get_type(file);
    int segments=lts_file_get_segments(file);
    int NV=lts_type_get_state_length(ltstype);
    int NS=lts_type_get_state_label_count(ltstype);
    int NE=lts_type_get_edge_label_count(ltstype);
    char base[128];
    if (me==0){
        file->init=arch_write(file->archive,"init");
    }
    if (NV>0) {
        file->state_vec=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
            sprintf(base,"SV-%d-%%d",seg);
            file->state_vec[seg]=arch_write_vec_U32(file->archive,base,NV);
        }
    }
    if (NS>0) {
        file->state_lbl=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
            sprintf(base,"SL-%d-%%d",seg);
            file->state_lbl[seg]=arch_write_vec_U32(file->archive,base,NS);
        }
    }
    int write_seg=0;
    switch(lts_file_source_mode(file)){
    case Index:
        file->src_ofs=(stream_t*)RTmallocZero(segments*sizeof(stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
            sprintf(base,"ES-%d-ofs",seg);
            file->src_ofs[seg]=arch_write(file->archive,base);
        }
        if (lts_file_get_edge_owner(file)==DestOwned && segments>1) {
            write_seg=1;
        }
        break;
    case SegVector:
        write_seg=1; // fall through
    case Vector:
        if (NV==0) Abort("vector mode unusable if vector undefined");
        file->src_vec=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
            sprintf(base,"ES-%d-%%d",seg);
            file->src_vec[seg]=arch_write_vec_U32(file->archive,base,NV);
        }
        break;
    }
    if (write_seg){
        file->src_seg=(stream_t*)RTmallocZero(segments*sizeof(stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
             sprintf(base,"ES-%d-seg",seg);
            file->src_seg[seg]=arch_write(file->archive,base);
        }
    }
    write_seg=0;
    switch(lts_file_dest_mode(file)){
    case Index:
        file->dst_ofs=(stream_t*)RTmallocZero(segments*sizeof(stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
            sprintf(base,"ED-%d-ofs",seg);
            file->dst_ofs[seg]=arch_write(file->archive,base);
        }
        if (lts_file_get_edge_owner(file)==SourceOwned && segments>1) {
            write_seg=1;
        }
        break;
    case SegVector:
        write_seg=1; // fall through
    case Vector:
        if (NV==0) Abort("vector mode unusable if vector undefined");
        file->dst_vec=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
            sprintf(base,"ED-%d-%%d",seg);
            file->dst_vec[seg]=arch_write_vec_U32(file->archive,base,NV);
        }
        break;
    }
    if (write_seg){
        file->dst_seg=(stream_t*)RTmallocZero(segments*sizeof(stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
            sprintf(base,"ED-%d-seg",seg);
            file->dst_seg[seg]=arch_write(file->archive,base);
        }
    }
    if (NE>0) {
        file->edge_lbl=(struct_stream_t*)RTmallocZero(segments*sizeof(struct_stream_t));
        for(int i=0;i<lts_file_owned_count(file);i++){
            int seg=lts_file_owned(file,i);
            sprintf(base,"EL-%d-%%d",seg);
            file->edge_lbl[seg]=arch_write_vec_U32(file->archive,base,NE);
        }
    }
}

static void write_chunk_tables(lts_file_t file){
    lts_type_t ltstype=lts_file_get_type(file);
    int T=lts_type_get_type_count(ltstype);
    stream_t ds;
    for(int i=0;i<T;i++){
        char*type_name=lts_type_get_type(ltstype,i);
        value_table_t values=lts_file_get_table(file,i);
        switch(lts_type_get_format(ltstype,i)){
        case LTStypeDirect:
        case LTStypeRange:
        case LTStypeBool:
        case LTStypeTrilean:
        case LTStypeSInt32:
            if (values && VTgetCount(values)!=0) {
                Print(lerror,"non-chunk type %s has table",type_name);
            }
            break;
        case LTStypeChunk:
        case LTStypeEnum:
            if (values==NULL || VTgetCount(values)==0) {
                Print(lerror,"table for type %s is missing or empty",type_name);
                Print(lerror,"change format for %s to direct",type_name);
                lts_type_set_format(ltstype,i,LTStypeDirect);
            } else {
                char stream_name[1024];
                sprintf(stream_name,"CT-%d",i);
                ds=arch_write(file->archive,stream_name);
                int element_count = VTgetCount(values);
                Warning(debug,"type %d has %d elements",i,element_count);

                int last_idx = 0;
                table_iterator_t it = VTiterator (values);
                while (IThasNext(it)) {
                    chunk c = ITnext (it);
                    int idx = VTputChunk (values, c);
                    while (last_idx < idx) { // fill non-dense indices
                        DSwriteVL (ds, 0);
                        DSwrite (ds, "", 0);
                        last_idx++;
                    }
                    DSwriteVL (ds, c.len);
                    DSwrite (ds, c.data, c.len);
                    last_idx++;
                }
                DSclose (&ds);
            }
            break;
        }
    }
}

static void write_state_mode(stream_t ds,state_format_t mode){
    switch(mode){
    case Index:
        DSwriteS(ds,"i");
        break;
    case Vector:
        DSwriteS(ds,"v");
        break;
    case SegVector:
        DSwriteS(ds,"sv");
        break;
    default:
        Abort("bad state mode");
    }
}

static void write_header(lts_file_t file){
    lts_type_t ltstype=lts_file_get_type(file);
    stream_t ds=arch_write(file->archive,"info");
    int N;
    stream_t fs=FIFOcreate(4096);
// Identify format
    DSwriteS(ds,"vector 1.0");
// Comment
    char *comment="archive I/O";
    char *fname = (char *) lts_file_get_name(file);
    Warning(infoLong,"comment is %s (%s)",comment, basename(fname));
    DSwriteS(ds,comment);
// the LTS type
    lts_type_serialize(ltstype,fs);
    Debug("ltstype is %zu bytes",FIFOsize(fs));
    DSwriteVL(ds,FIFOsize(fs));
    for(;;){
        char data[1024];
        int len=stream_read_max(fs,data,1024);
        if (len) DSwrite(ds,data,len);
        if (len<1024) break;
    }
// structural information.
    // segment count
    N=lts_file_get_segments(file);
    DSwriteU32(fs,N);
    Debug("segment count is %d",N);
    // initial state representation.
    write_state_mode(fs,lts_file_init_mode(file));
    // ownership
    switch(lts_file_get_edge_owner(file)){
    case SourceOwned:
        DSwriteS(fs,"src");
        break;
    case DestOwned:
        DSwriteS(fs,"dst");
        break;
    default:
        Abort("bad edge owner");
    }
    // source mode
    write_state_mode(fs,lts_file_source_mode(file));
    // dest mode
    write_state_mode(fs,lts_file_dest_mode(file));
    Debug("state format info is %zu bytes",FIFOsize(fs));
    DSwriteVL(ds,FIFOsize(fs));
    for(;;){
        char data[1024];
        int len=stream_read_max(fs,data,1024);
        if (len) DSwrite(ds,data,len);
        if (len<1024) break;
    }
// The state and transition counts.
    N=lts_file_get_segments(file);
    long long unsigned int total_states=0;
    long long unsigned int total_edges=0;
    {
        uint32_t count=lts_get_init_count(file);
        Print(infoLong,"LTS has %u initial state(s) (%s)",count, basename(fname));
        DSwriteU32(fs,count);
    }
    for(int i=0;i<N;i++){
        uint32_t count=lts_get_state_count(file,i);
        if (count) {
            uint32_t tmp;
            tmp=lts_get_max_src_p1(file,i);
            if (tmp>count) Abort("edge source uses an unwritten state (%d.%u, %u written)",i,tmp,count);
            tmp=lts_get_max_dst_p1(file,i);
            if (tmp>count) Abort("edge target uses an unwritten state (%d.%u, %u written)",i,tmp,count);
        } else {
            count=lts_get_max_src_p1(file,i);
            lts_set_state_count(file,i,count);
            count=lts_get_max_dst_p1(file,i);
            lts_set_state_count(file,i,count);
        }
        Print(infoLong,"segment %d has %u states (%s)",i,count, basename(fname));
        total_states+=count;
        DSwriteU32(fs,count);
    }
    for(int i=0;i<N;i++){
        uint32_t count=(uint32_t)lts_get_edge_count(file,i);
        Print(infoLong,"segment %d has %u transitions (%s)",i,count, basename(fname));
        total_edges+=count;
        DSwriteU32(fs,count);
    }
    Print(infoLong,"accounted for %llu states and %llu transitions (%s)",total_states,total_edges, basename(fname));
    N=lts_type_get_type_count(ltstype);
    for(int i=0;i<N;i++){
        switch(lts_type_get_format(ltstype,i)){
        case LTStypeDirect:
        case LTStypeRange:
        case LTStypeBool:
        case LTStypeTrilean:
        case LTStypeSInt32:
            DSwriteU32(fs,0);
            break;
        case LTStypeChunk:
        case LTStypeEnum:
            if (file->type_table[i]==NULL) {
                Abort("table for type %s missing",lts_type_get_type(ltstype,i));
            }
            DSwriteU32(fs,VTgetCount(file->type_table[i]));
            break;
        }

    }
    DSwriteVL(ds,FIFOsize(fs));
    for(;;){
        char data[1024];
        int len=stream_read_max(fs,data,1024);
        if (len) DSwrite(ds,data,len);
        if (len<1024) break;
    }
// The tree compression used. (none)
    DSwriteVL(ds,0);
// header is written.
    DSclose(&ds);
    DSclose(&fs);
}

static void write_close(lts_file_t file){
    int me=HREme(lts_file_context(file));
    int segments=lts_file_get_segments(file);
    lts_file_sync(file);
    for(int i=0;i<segments;i++){
        if (file->state_vec && file->state_vec[i]) DSstructClose(file->state_vec+i);
        if (file->state_lbl && file->state_lbl[i]) DSstructClose(file->state_lbl+i);
        if (file->src_seg && file->src_seg[i]) DSclose(file->src_seg+i);
        if (file->src_ofs && file->src_ofs[i]) DSclose(file->src_ofs+i);
        if (file->src_vec && file->src_vec[i]) DSstructClose(file->src_vec+i);
        if (file->dst_seg && file->dst_seg[i]) DSclose(file->dst_seg+i);
        if (file->dst_ofs && file->dst_ofs[i]) DSclose(file->dst_ofs+i);
        if (file->dst_vec && file->dst_vec[i]) DSstructClose(file->dst_vec+i);
        if (file->edge_lbl && file->edge_lbl[i]) DSstructClose(file->edge_lbl+i);
    }
    if (me==0){
        if (file->init) DSclose(&file->init);
        write_chunk_tables(file);
        write_header(file);
    } else {
        Debug("waiting for worker 0");
    }
    arch_close(&(file->archive));
}



static value_table_t set_table(lts_file_t lts,int type_no,value_table_t table){
    if (lts->type_table[type_no] && lts->type_table[type_no]!=table) Abort("changing tables is future work");
    lts->type_table[type_no]=table;
    return table;
}

static stream_t attach(lts_file_t lts,char *name){
    return arch_write(lts->archive,name);
}

static lts_file_t archive_create(archive_t archive,const char* name,
                                 lts_type_t ltstype,int segments,lts_file_t settings){
    Debug("create");
    lts_file_t file=lts_file_bare(name,ltstype,segments,settings,sizeof(struct lts_file_s));
    file->archive=archive;
    int NT=lts_type_get_type_count(ltstype);
    file->type_table=RTmallocZero(NT*sizeof(value_table_t));
    lts_file_set_write_init(file,write_init);
    lts_file_set_write_state(file,write_state);
    lts_file_set_write_edge(file,write_edge);
    lts_file_set_close(file,write_close);
    lts_file_set_table_callback(file,set_table);
    lts_file_set_attach(file,attach);
    lts_file_complete(file);
    Debug("worker %d owns %d segments",HREme(lts_file_context(file)),lts_file_owned_count(file));
    for(int i=0;i<lts_file_owned_count(file);i++){
        int seg=lts_file_owned(file,i);
        Debug("owned segment %d is %d",i,seg);
        (void) seg;
    }
    write_begin(file);
    return file;
}


lts_file_t gcf_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    hre_context_t global = lts_file_context(settings) ? lts_file_context(settings) : HREglobal();
    int me=HREme(global);
    int peers=HREpeers(global);
    if (segments%peers) {
        Abort("number of peers (%d) does not divide number of segments (%d)",
              peers,segments);
    }
    archive_t archive=arch_gcf_create(raf_unistd((char*)name),lts_io_blocksize,
                                      lts_io_blocksize*lts_io_blockcount,me,peers);
    arch_set_write_policy(archive,SSMcreateSWP(compression_policy));
    return archive_create(archive,name,ltstype,segments,settings);
}

lts_file_t gcd_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    hre_context_t global = lts_file_context(settings) ? lts_file_context(settings) : HREglobal();
    int me=HREme(global);
    int peers=HREpeers(global);
    if (segments%peers) {
        Abort("number of peers (%d) does not divide number of segments (%d)",
              peers,segments);
    }
    if(me==0){
        if(create_empty_dir((char*)name,DELETE_ALL)){
            Abort("could not create or clear directory %s",name);
        }
        HREbarrier(global);
    } else {
        HREbarrier(global);
        for(int i=0;;i++){
            if (i==1000) Abort("timeout during creation of %s",name);
            if (is_a_dir((char*)name)) break;
            usleep(10000);
        }
    }
    archive_t archive=arch_dir_open((char*)name,lts_io_blocksize);
    arch_set_write_policy(archive,SSMcreateSWP(compression_policy));
    return archive_create(archive,name,ltstype,segments,settings);
}

lts_file_t dir_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    hre_context_t global = lts_file_context(settings) ? lts_file_context(settings) : HREglobal();
    int me=HREme(global);
    int peers=HREpeers(global);
    if (segments%peers) {
        Abort("number of peers (%d) does not divide number of segments (%d)",
              peers,segments);
    }
    if(me==0){
        if(create_empty_dir((char*)name,DELETE_ALL)){
            Abort("could not create or clear directory %s",name);
        }
        HREbarrier(global);
    } else {
        HREbarrier(global);
        for(int i=0;;i++){
            if (i==1000) Abort("timeout during creation of %s",name);
            if (is_a_dir((char*)name)) break;
            usleep(10000);
        }
    }
    archive_t archive=arch_dir_open((char*)name,lts_io_blocksize);
    arch_set_write_policy(archive,NULL);
    return archive_create(archive,name,ltstype,segments,settings);
}
