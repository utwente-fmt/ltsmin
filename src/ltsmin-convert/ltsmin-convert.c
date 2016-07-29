// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <lts-io/user.h>
#include <lts-lib/lts.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <util-lib/treedbs.h>
#include <util-lib/string-map.h>

typedef enum {Undefined,LTScopy,LTSrdwr,LTSindex} task_t;

static task_t task=Undefined;
static int wr_seg=0;
static int rd_seg;
static char* label_filter=NULL;
static int encode=0;
static int bfs_reorder=0;

static int root=-1;

static  struct poptOption options[] = {
    { "copy" , 0 , POPT_ARG_VAL , &task , LTScopy ,
    "perform a streaming copy from <input> to <output>" , NULL},
    { "rdwr" , 0 , POPT_ARG_VAL , &task , LTSrdwr ,
    "perform a load/store copy from <input> to <output>" , NULL},
    { "index" , 0 , POPT_ARG_VAL , &task , LTSindex ,
    "transform the vector based <input> to indexed <output>" , NULL},
    { "segments" , 0 , POPT_ARG_INT , &wr_seg , 0 ,
      "set the number of segments for the output file" , "<N>" },
    { "filter" , 0 , POPT_ARG_STRING , &label_filter , 0 ,
      "Select the labels to be written to file from the state vector elements, "
      "state labels and edge labels." , "<patternlist>" },
    { "encode" , 0 , POPT_ARG_VAL , &encode , 1 ,
      "encode any LTS as a single edge label LTS during a load/store copy" , NULL },
    { "bfs" , 0 , POPT_ARG_VAL , &bfs_reorder , 1 ,
      "renumber the states to conform to BFS order during a load/store copy" , NULL },
    { "set-root" , 0 , POPT_ARG_INT , &root , 0 ,
      "set the initial state during a load/store copy operation" , "<new root>" },
    POPT_TABLEEND
};

int main(int argc, char *argv[]){
    char* files[2];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Tool for transforming labeled transition systems\n\nOptions");
    lts_lib_setup();
    HREinitStart(&argc,&argv,1,2,files,"<input> [<output>]");
    int me=HREme(HREglobal());
    int peers=HREpeers(HREglobal());
    if (peers>1) Abort("parallelizing this tool is future work");(void)me;
    string_set_t label_set=NULL;
    if (label_filter!=NULL){
        label_set=SSMcreateSWPset(label_filter);
    }
    switch(task){
        case Undefined:
            Abort("task unspecified");
        case LTScopy:
            if (files[1]==NULL) Abort("second argument required for copy.");
            Print(infoShort,"streaming copy from %s to %s",files[0],files[1]);
            lts_file_t in=lts_file_open(files[0]);
            lts_type_t ltstype=lts_file_get_type(in);
            rd_seg=lts_file_get_segments(in);
            if (wr_seg==0) {
                wr_seg=rd_seg;
            } else {
                Abort("on-the-fly changing the number of segments is future work");
            }
            lts_file_t out;
            if (label_set==NULL){
                out=lts_file_create(files[1],ltstype,wr_seg,in);
            } else {
                out=lts_file_create_filter(files[1],ltstype,label_set,wr_seg,in);
            }
            int N=lts_type_get_type_count(ltstype);
            for(int i=0;i<N;i++){
                char*name=lts_type_get_type(ltstype,i);
                switch(lts_type_get_format(ltstype,i)){
                case LTStypeDirect:
                case LTStypeRange:
                    Debug("integer type %s does not use tables",name);
                    break;
                case LTStypeChunk:
                case LTStypeEnum:
                    Debug("creating table for type %s",name);
                    value_table_t tmp = simple_chunk_table_create(NULL,name);
                    Debug("set in %s",name);
                    lts_file_set_table(in,i,tmp);
                    Debug("set out %s",name);
                    lts_file_set_table(out,i,tmp);
                    break;
                }
            }
            lts_file_copy(in,out);
            lts_file_close(out);
            lts_file_close(in);
            break;
        case LTSrdwr:
            if (files[1]==NULL) Abort("second argument required for rdwr.");
            Print(infoShort,"loading from %s",files[0]);
            lts_t lts=lts_create();
            lts_read(files[0],lts);
            if (root>=0){
                if (lts->root_count!=1){
                    Abort("original does not have precisely one initial state");
                }
                Print(infoShort,"changing root from %d to %d",lts->root_list[0],root);
                lts->root_list[0]=root;
            }
            if (encode) {
                Print(infoShort,"single edge label encoding");
                lts=lts_encode_edge(lts);
            }
            if (bfs_reorder) {
                Print(infoShort,"reindexing LTS in BFS order");
                lts_bfs_reorder(lts);
            }
            Print(infoShort,"storing in %s",files[1]);
            if(wr_seg==0) wr_seg=1;
            lts_write(files[1],lts,label_set,wr_seg);
            break;
        case LTSindex:{
            if (peers>1) Abort("parallelizing this tool is future work");
            if (files[1]==NULL) Abort("second argument required for index.");
            Print(infoShort,"opening %s",files[0]);
            lts_file_t in=lts_file_open(files[0]);
            lts_type_t ltstype=lts_file_get_type(in);
            int segments=lts_file_get_segments(in);
            lts_file_t settings=lts_get_template(in);
            if (lts_file_get_edge_owner(settings)!=SourceOwned) Abort("bad edge owner");
            lts_file_set_dest_mode(settings,Index);
            lts_file_set_init_mode(settings,Index);
            Print(infoShort,"creating %s",files[1]);
            lts_file_t out=lts_file_create(files[1],ltstype,segments,settings);
            int N=lts_type_get_type_count(ltstype);
            for(int i=0;i<N;i++){
                char*name=lts_type_get_type(ltstype,i);
                switch(lts_type_get_format(ltstype,i)){
                case LTStypeDirect:
                case LTStypeRange:
                    Debug("integer type %s does not use tables",name);
                    break;
                case LTStypeChunk:
                case LTStypeEnum:
                    Debug("creating table for type %s",name);
                    value_table_t tmp = simple_chunk_table_create(NULL,name);
                    Debug("set in %s",name);
                    lts_file_set_table(in,i,tmp);
                    Debug("set out %s",name);
                    lts_file_set_table(out,i,tmp);
                    break;
                }
            }
            treedbs_t db[segments];
            int SV=lts_type_get_state_length(ltstype);
            int SL=lts_type_get_state_label_count(ltstype);
            int K=lts_type_get_edge_label_count(ltstype);
            for(int i=0;i<segments;i++){
                Print(info,"loading and copying states of segment %d",i);
                uint32_t state[SV];
                uint32_t label[SL];
                db[i]=TreeDBScreate(SV);
                int idx=0;
                while(lts_read_state(in,&i,state,label)){
                    int tmp=TreeFold(db[i],(int*)state);
                    if (idx!=tmp){
                        Abort("unexpected index %u != %u",tmp,idx);
                    }
                    idx++;
                    lts_write_state(out,i,(int*)state,label);
                }
            }
            Print(info,"converting initial states");
            {
                uint32_t seg;
                uint32_t state[SV];
                while(lts_read_init(in,(int*)&seg,state)){
                    int idx=TreeFold(db[seg],(int*)state);
                    lts_write_init(out,seg,&idx);
                }
            }
            for(int i=0;i<segments;i++){
                Print(info,"converting edges of segment %d",i);
                uint32_t src_state[1];
                uint32_t dst_seg;
                uint32_t dst_state[SV];
                uint32_t label[K];
                while(lts_read_edge(in,&i,src_state,(int*)&dst_seg,dst_state,label)){
                    int idx=TreeFold(db[dst_seg],(int*)dst_state);
                    lts_write_edge(out,i,src_state,dst_seg,&idx,label);
                }
            }
            lts_file_close(out);
            lts_file_close(in);
        }
    }
    Print(infoShort,"done");
    HREexit(LTSMIN_EXIT_SUCCESS);
}
