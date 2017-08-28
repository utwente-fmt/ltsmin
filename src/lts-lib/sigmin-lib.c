// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <hre/dir_ops.h>
#include <hre-io/user.h>
#include <lts-io/provider.h>
#include <lts-lib/dir-info.h>
#include <lts-lib/lts.h>
#include <lts-lib/lts-pg-io.h>
#include <hre/stringindex.h>
#include <util-lib/string-map.h>

/* DIR format writing */

static void DSwriteLN(stream_t stream,char* string){
    int len=strlen(string);
    DSwrite(stream,string,len);
    DSwrite(stream,"\n",strlen("\n"));
}

static void
write_chunk (stream_t output, chunk label_c)
{
    char label_s[label_c.len * 2 + 6];
    chunk2string (label_c, sizeof label_s, label_s);
    DSwriteLN (output, label_s);
}

static void lts_write_dir(archive_t archive,string_map_t map,lts_t lts,int segments){
    if (map) arch_set_write_policy(archive,map);
    dir_info_t info=DIRinfoCreate(segments);
    int i,j;
    uint32_t k;
    char filename[1024];
    stream_t output;
    stream_t *src_out;
    stream_t *lbl_out;
    stream_t *dst_out;

    if (lts->root_count !=1) Abort("LTS has %u initial states DIR requires 1",lts->root_count);
    lts_set_type(lts,LTS_BLOCK);
    info->label_tau=lts->tau;
    int type_no=lts_type_get_edge_label_typeno(lts->ltstype,0);
    switch(lts_type_get_format(lts->ltstype,type_no)){
        case LTStypeChunk:
        case LTStypeEnum:
            break;
        default:
            Abort("DIR is limited to Chunk/Enum edge labels.");
    }
    info->label_count=VTgetCount(lts->values[type_no]);
    info->initial_seg=lts->root_list[0]%segments;
    info->initial_ofs=lts->root_list[0]/segments;
    output=arch_write(archive,"TermDB");
    int last_idx = 0;
    table_iterator_t it = VTiterator (lts->values[type_no]);
    while (IThasNext(it)) {
        chunk label_c = ITnext (it);
        int idx = VTputChunk (lts->values[type_no], label_c);
        while (last_idx < idx) { // fill non-dense indices
            write_chunk (output, (chunk){0, ""});
            last_idx++;
        }
        write_chunk (output, label_c);
    }
    DSclose(&output);
    src_out=(stream_t*)RTmalloc(segments*sizeof(stream_t));
    lbl_out=(stream_t*)RTmalloc(segments*sizeof(stream_t));
    dst_out=(stream_t*)RTmalloc(segments*sizeof(stream_t));
    for(i=0;i<segments;i++) {
        for(j=0;j<segments;j++) {
            sprintf(filename,"src-%d-%d",i,j);
            src_out[j]=arch_write(archive,filename);
            sprintf(filename,"label-%d-%d",i,j);
            lbl_out[j]=arch_write(archive,filename);
            sprintf(filename,"dest-%d-%d",i,j);
            dst_out[j]=arch_write(archive,filename);
        }
        for(j=i;j<(int)lts->states;j+=segments){
            for(k=lts->begin[j];k<lts->begin[j+1];k++){
                int dseg=(lts->dest[k])%segments;
                info->transition_count[i][dseg]++;
                DSwriteU32(src_out[dseg],info->state_count[i]);
                DSwriteU32(lbl_out[dseg],lts->label[k]);
                DSwriteU32(dst_out[dseg],(lts->dest[k])/segments);
            }
            info->state_count[i]++;
        }
        for(j=0;j<segments;j++) {
            DSclose(&src_out[j]);
            DSclose(&lbl_out[j]);
            DSclose(&dst_out[j]);
        }
    }
    info->info="bsim2 output";
    output=arch_write(archive,"info");
    DIRinfoWrite(output,info);
    DSclose(&output);
    info->info=NULL;
    DIRinfoDestroy(info);
}

static void lts_read_dir(archive_t archive,lts_t lts){
    Print(infoShort,"opening info");
    stream_t input=arch_read(archive,"info");
    dir_info_t info=DIRinfoRead(input,1);
    DSclose(&input);
    int segments=info->segment_count;
    Print(infoShort,"got info for %d segments",segments);
    int offset[segments];
    offset[0]=0;
    for(int i=1;i<segments;i++){
        offset[i]=info->state_count[i-1]+offset[i-1];
    }
    int s_count=0;
    int t_count=0;
    for(int i=0;i<segments;i++){
        s_count+=info->state_count[i];
        for(int j=0;j<segments;j++){
            t_count+=info->transition_count[i][j];
        }
    }
    Print(infoShort,"counted %u states and %u transitions",s_count,t_count);
    lts_set_sig(lts,single_action_type());
    lts_set_type(lts,LTS_LIST);
    lts_set_size(lts,1,s_count,t_count);
    lts->root_list[0]=offset[info->initial_seg]+info->initial_ofs;
    lts->tau=info->label_tau;
    Print(infoShort,"getting %d labels from TermDB",info->label_count);
    input=arch_read(archive,"TermDB");
    int type_no=lts_type_get_edge_label_typeno(lts->ltstype,0);
    for(int i=0;i<info->label_count;i++){
        char *line=DSreadLN(input);
        int len=strlen(line);
        char data[len];
        chunk tmp_chunk=chunk_ld(len,data);
        string2chunk(line,&tmp_chunk);
        VTputAtChunk(lts->values[type_no], tmp_chunk, i);
    }
    DSclose(&input);
    Print(infoShort,"got labels");
    int t_offset=0;
    char filename[1024];
    for(int i=0;i<segments;i++){
        for(int j=0;j<segments;j++){
            sprintf(filename,"src-%d-%d",i,j);
            stream_t src=arch_read(archive,filename);
            sprintf(filename,"label-%d-%d",i,j);
            stream_t lbl=arch_read(archive,filename);
            sprintf(filename,"dest-%d-%d",i,j);
            stream_t dst=arch_read(archive,filename);
            for(int k=0;k<info->transition_count[i][j];k++){
                lts->src[t_offset]=offset[i]+DSreadU32(src);
                lts->label[t_offset]=DSreadU32(lbl);
                lts->dest[t_offset]=offset[j]+DSreadU32(dst);
                t_offset++;
            }
            DSclose(&src);
            DSclose(&lbl);
            DSclose(&dst);
        }
    }
    DIRinfoDestroy(info);
}

/* management functions */

#define LTS_AUT  1
#define LTS_BCG  2
#define LTS_DIR  3
#define LTS_SVC  4
#define LTS_FSM  5
#define LTS_FC2  6
#define LTS_VSF  7
#define LTS_GCF  8
#define LTS_GZ   9
#define LTS_DMP 10
#define LTS_TRA 11
#define LTS_GCD 12
#define LTS_PG  13
#define LTS_IMCA 14

static int lts_guess_format(char *name){
    char *lastdot=strrchr(name,'.');
    if(!lastdot) Abort("filename %s has no extension",name);
    lastdot++;
    if (!strcmp(lastdot,"gz"))  return LTS_GZ;
    if (!strcmp(lastdot,"aut")) return LTS_AUT;
    if (!strcmp(lastdot,"bcg")) return LTS_BCG;
    if (!strcmp(lastdot,"svc")) return LTS_SVC;
    if (!strcmp(lastdot,"dir")) return LTS_DIR;
    if (!strcmp(lastdot,"gcf")) return LTS_GCF;
    if (!strcmp(lastdot,"fsm")) return LTS_FSM;
    if (!strcmp(lastdot,"fc2")) return LTS_FC2;
    if (!strcmp(lastdot,"dmp")) return LTS_DMP;
    if (!strcmp(lastdot,"tra")) return LTS_TRA;
    if (!strcmp(lastdot,"gcd")) return LTS_GCD;
    if (!strcmp(lastdot,"pg")) return LTS_PG;
    if (!strcmp(lastdot,"gm")) return LTS_PG;
    if (!strcmp(lastdot,"ma")) return LTS_IMCA;
    Abort("unknown extension %s",lastdot);
}


void lts_read(char *name,lts_t lts){
    int format=lts_guess_format(name);
    switch(format){
    case LTS_GCF:
    case LTS_DIR:
    {
        archive_t archive=format==LTS_DIR?arch_dir_open(name,65536):arch_gcf_read(raf_unistd(name));
        stream_t ds=arch_read(archive,"info");
        int magic=DSreadS32(ds);
        DSclose(&ds);
        if (magic==31) {
            // DIR in .dir
            lts_read_dir(archive,lts);
            arch_close(&archive);
            return;
        } else {
            // VEC in .dir
            arch_close(&archive);
            break;
        }
    }
    case LTS_TRA:
        lts_read_tra(name,lts);
        return;
    case LTS_IMCA:
        Abort("no read support for imca");
    default:
        break;
    }
    lts_file_t src=lts_file_open(name);
    int segments=lts_file_get_segments(src);
    lts_type_t ltstype=lts_file_get_type(src);
    if (lts->ltstype==NULL){
        lts_set_sig(lts,ltstype);
    } else {
        Print(info,"** warning ** omitting signature check");
    }
    lts_file_t dst=lts_writer(lts,segments,src);
    int T=lts_type_get_type_count(ltstype);
    for(int i=0;i<T;i++){
        if (lts->values[i]) lts_file_set_table(src,i,lts->values[i]);
    }
    lts_file_copy(src,dst);
    lts_file_close(src);
    lts_file_close(dst);
}

void lts_write(char *name,lts_t lts,string_set_t filter,int segments){
    int format=lts_guess_format(name);
    lts_type_t ltstype=lts->ltstype;
    switch(format){
    case LTS_IMCA:
        lts_write_imca(name,lts);
        break;
    case LTS_TRA:
        lts_write_tra(name,lts);
        break;
    case LTS_PG:
        lts_write_pg(name,lts);
        break;
    case LTS_DIR:
        if (lts_type_get_state_length(ltstype)==0
            && lts_type_get_state_label_count(ltstype)==0
            && lts_type_get_edge_label_count(ltstype)==1
        ){
            archive_t archive=arch_dir_create(name,65536,DELETE_ALL);
            lts_write_dir(archive,NULL,lts,segments);
            arch_close(&archive);
            break;
        } else // fall through
    default: {
        lts_file_t src=lts_reader(lts,segments,NULL);
        lts_file_t dst;
        if (filter==NULL){
            dst=lts_file_create(name,lts->ltstype,segments,src);
        } else {
            dst=lts_file_create_filter(name,lts->ltstype,filter,segments,src);
        }
        int T=lts_type_get_type_count(lts->ltstype);
        for(int i=0;i<T;i++){
            if (lts->values[i]) lts_file_set_table(dst,i,lts->values[i]);
        }
        lts_file_copy(src,dst);
        lts_file_close(src);
        lts_file_close(dst);
        break;
        }
    }
}

lts_t lts_copy(lts_t orig){
    lts_t copy=lts_create();
    lts_set_sig(copy,orig->ltstype);
    lts_file_t src=lts_reader(orig,1,NULL);
    lts_file_t dst=lts_writer(copy,1,src);
    int T=lts_type_get_type_count(orig->ltstype);
    for(int i=0;i<T;i++){
        if (orig->values[i]) {
            table_iterator_t it = VTiterator (orig->values[i]);
            for (int j = 0; IThasNext(it); j++) {
                chunk c = ITnext (it);
                VTputAtChunk (copy->values[i], c, j);
            }
        }
    }
    lts_file_copy(src,dst);
    lts_file_close(src);
    lts_file_close(dst);
    return copy;
}


