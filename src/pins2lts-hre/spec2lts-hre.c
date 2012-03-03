// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <dynamic-array.h>
#include <fast_hash.h>
#ifdef USEMPI
#include <hre-mpi/user.h>
#else
#include <hre/user.h>
#endif
#include <lts-io/user.h>
#include <spec-greybox.h>
#include <stringindex.h>
#include <treedbs.h>
#include <string-map.h>

struct dist_thread_context {
    lts_file_t output;
    treedbs_t dbs;
    int mpi_me;
    int *tcount;
    array_manager_t state_man;
    uint32_t *parent_ofs;
    uint16_t *parent_seg;
    long long int explored,visited,transitions;
};

static int dst_ofs=2;
static int lbl_ofs;
static int trans_len;
static int write_lts=0;
static int nice_value=0;
static int find_dlk=0;
static int write_state=0;
static int size;
static int state_labels;
static int edge_labels;
static int mpi_nodes;
static char* label_filter=NULL;

static  struct poptOption options[] = {
    { "nice" , 0 , POPT_ARG_INT , &nice_value , 0 , "set the nice level of all workers"
      " (useful when running on other peoples workstations)" , NULL},
    { "write-state" , 0 , POPT_ARG_VAL , &write_state, 1 , "write the full state vector" , NULL },
    { "filter" , 0 , POPT_ARG_STRING , &label_filter , 0 ,
      "Select the labels to be written to file from the state vector elements, "
      "state labels and edge labels." , "<patternlist>" },
    { "deadlock" , 'd' , POPT_ARG_VAL , &find_dlk , 1 , "detect deadlocks" , NULL },
    SPEC_POPT_OPTIONS,
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
    POPT_TABLEEND
};

static uint32_t chk_base=0;

static inline void adjust_owner(int32_t *state){
    chk_base=SuperFastHash((char*)state,size*4,0);
}

static inline int owner(int32_t *state){
    uint32_t hash=chk_base^SuperFastHash((char*)state,size*4,0);
    return (hash%mpi_nodes);
}

/********************************************************/

struct src_info {
    int seg;
    int ofs;
    hre_task_t new_trans;
};

static void callback(void*context,transition_info_t*info,int*dst){
    int who=owner(dst);
    uint32_t trans[trans_len];
    trans[0]=((struct src_info*)context)->ofs;
    for(int i=0;i<size;i++){
        trans[dst_ofs+i]=dst[i];
    }
    for(int i=0;i<edge_labels;i++){
        trans[lbl_ofs+i]=info->labels[i];
    }
    TaskSubmitFixed(((struct src_info*)context)->new_trans,who,trans);
}

static void new_transition(void*context,int src_seg,int len,void*arg){
    struct dist_thread_context* ctx=(struct dist_thread_context*)context;
    (void)len;
    uint32_t *trans=(uint32_t*)arg;
    int temp=TreeFold(ctx->dbs,(int32_t*)(trans+dst_ofs));
    if (temp>=ctx->visited) {
        ctx->visited=temp+1;
        if(find_dlk){
            ensure_access(ctx->state_man,temp);
            ctx->parent_seg[temp]=src_seg;
            ctx->parent_ofs[temp]=trans[0];
        }
    }
    if (write_lts){
        lts_write_edge(ctx->output,src_seg,trans,ctx->mpi_me,&temp,trans+lbl_ofs);
    }
    ctx->tcount[src_seg]++;
    ctx->transitions++;
}

static pthread_mutex_t shared_mutex = PTHREAD_MUTEX_INITIALIZER;
static int shared_todo=1;

int main(int argc, char*argv[]){
    long long int global_visited,global_explored,global_transitions;
    char *files[2];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Perform a distributed enumerative reachability analysis of <model>\n\nOptions");
    lts_lib_setup();
    HREenableAll();
    if (!SPEC_MT_SAFE){
        HREenableThreads(0);
    }
    HREinitStart(&argc,&argv,1,2,files,"<model> [<lts>]");

    struct dist_thread_context ctx;
    mpi_nodes=HREpeers(HREglobal());
    ctx.mpi_me=HREme(HREglobal());

    hre_task_queue_t task_queue=HREcreateQueue(HREglobal());

    ctx.tcount=(int*)RTmalloc(mpi_nodes*sizeof(int));
    memset(ctx.tcount,0,mpi_nodes*sizeof(int));

    model_t model=GBcreateBase();
    GBsetChunkMethods(model,HREgreyboxNewmap,HREglobal(),HREgreyboxI2C,HREgreyboxC2I,HREgreyboxCount);

    pthread_mutex_lock(&shared_mutex);
    if (shared_todo){
        GBloadFileShared(model,files[0]);
        shared_todo=0;
    }
    pthread_mutex_unlock(&shared_mutex);
    GBloadFile(model,files[0],&model);

    HREbarrier(HREglobal());
    Warning(info,"model created");
    lts_type_t ltstype=GBgetLTStype(model);

    /* Initializing according to the options just parsed.
     */
    if (nice_value) {
        if (ctx.mpi_me==0) Warning(info,"setting nice to %d",nice_value);
        int rv = nice(nice_value);
        if (rv==-1) Warning(info,"failed to set nice");
    }
    /***************************************************/
    if (find_dlk) {
        ctx.state_man=create_manager(65536);
        ctx.parent_ofs=NULL;
        ADD_ARRAY(ctx.state_man,ctx.parent_ofs,uint32_t);
        ctx.parent_seg=NULL;
        ADD_ARRAY(ctx.state_man,ctx.parent_seg,uint16_t);
    }
    HREbarrier(HREglobal());
    /***************************************************/
    size=lts_type_get_state_length(ltstype);
    if (size<2) Fatal(1,error,"there must be at least 2 parameters");
    ctx.dbs=TreeDBScreate(size);
    int src[size];
    state_labels=lts_type_get_state_label_count(ltstype);
    edge_labels=lts_type_get_edge_label_count(ltstype);
    Warning(info,"there are %d state labels and %d edge labels",state_labels,edge_labels);
    int labels[state_labels];
    /***************************************************/
    HREbarrier(HREglobal());
    /***************************************************/
    GBgetInitialState(model,src);
    Warning(info,"initial state computed at %d",ctx.mpi_me);
    adjust_owner(src);
    Warning(info,"initial state translated at %d",ctx.mpi_me);
    ctx.explored=0;
    ctx.transitions=0;
    global_visited=1;
    global_explored=0;
    global_transitions=0;
    if(ctx.mpi_me==0){
        Warning(info,"folding initial state at %d",ctx.mpi_me);
        if (TreeFold(ctx.dbs,src)) Fatal(1,error,"Initial state wasn't assigned state no 0");
        ctx.visited=1;
    } else {
        ctx.visited=0;
    }
    /***************************************************/
    HREbarrier(HREglobal());
    if (files[1]) {
        Warning(info,"Writing output to %s",files[1]);
        write_lts=1;
        if (write_state) {
            // write-state means write everything.
            ctx.output=lts_file_create(files[1],ltstype,mpi_nodes,lts_index_template());
        } else if (label_filter!=NULL) {
            string_set_t label_set=SSMcreateSWPset(label_filter);
            Print(info,"label filter is \"%s\"",label_filter);
            ctx.output=lts_file_create_filter(files[1],ltstype,label_set,mpi_nodes,lts_index_template());
            write_state=1;
        } else {
            // default is all state labels and all edge labels
            ctx.output=lts_file_create_nostate(files[1],ltstype,mpi_nodes,lts_index_template());
            if (state_labels>0) write_state=1;
        }
        int T=lts_type_get_type_count(ltstype);
        for(int i=0;i<T;i++){
            lts_file_set_table(ctx.output,i,GBgetChunkMap(model,i));
        }
        HREbarrier(HREglobal()); // opening is sometimes a collaborative operation. (e.g. *.dir)
        if (ctx.mpi_me==0){
            uint32_t tmp[1]={0};
            lts_write_init(ctx.output,0,tmp);
        }
        HREbarrier(HREglobal());
    } else {
        Warning(info,"No output, just counting the number of states");
        write_lts=0;
        HREbarrier(HREglobal());
    }
    /***************************************************/
    dst_ofs=1;
    lbl_ofs=dst_ofs+size;
    trans_len=lbl_ofs+edge_labels;
    struct src_info src_ctx;
    src_ctx.new_trans=TaskCreate(task_queue,1,65536,new_transition,&ctx,trans_len*4);
    /***************************************************/
    int level=0;
    for(;;){
        long long int limit=ctx.visited;
        int lvl_scount=0;
        int lvl_tcount=0;
        HREbarrier(HREglobal());
        if (ctx.mpi_me==0) {
            Warning(info,"level %d has %lld states, explored %lld states %lld transitions",
                level,global_visited-global_explored,global_explored,global_transitions);
        }
        level++;
        while(ctx.explored<limit){
            TreeUnfold(ctx.dbs,ctx.explored,src);
            src_ctx.seg=ctx.mpi_me;
            src_ctx.ofs=ctx.explored;
            ctx.explored++;
            int count=GBgetTransitionsAll(model,src,callback,&src_ctx);
            if (count<0) Fatal(1,error,"error in GBgetTransitionsAll");
            if (count==0 && find_dlk){
                Warning(info,"deadlock found: %d.%d",src_ctx.seg,src_ctx.ofs);
                //deadlock_found(ctx.seg,ctx.ofs);
                Fatal(1,error,"trace printing unimplemented");
            }
            if (state_labels){
                GBgetStateLabelsAll(model,src,labels);
            }
            if(write_lts && write_state){
                lts_write_state(ctx.output,ctx.mpi_me,src,labels);
            }

            lvl_scount++;
            lvl_tcount+=count;
            if ((lvl_scount%1000)==0) {
                Warning(infoLong,"generated %d transitions from %d states",
                    lvl_tcount,lvl_scount);
            }
            if ((lvl_scount%4)==0) HREyield(HREglobal());
        }
        Warning(infoLong,"explored %d states and %d transitions",lvl_scount,lvl_tcount);
        TQwait(task_queue);
        Warning(infoLong,"level terminated");
        HREreduce(HREglobal(),1,&ctx.visited,&global_visited,UInt64,Sum);
        HREreduce(HREglobal(),1,&ctx.explored,&global_explored,UInt64,Sum);
        HREreduce(HREglobal(),1,&ctx.transitions,&global_transitions,UInt64,Sum);
        if (global_visited==global_explored) break;
    }
    if (ctx.mpi_me==0) {
        Warning(info,"state space has %d levels %lld states %lld transitions",
            level,global_explored,global_transitions);
    }
    HREbarrier(HREglobal());;
    /* State space was succesfully generated. */
    Warning(infoLong,"My share is %lld states and %lld transitions",ctx.explored,ctx.transitions);
    HREbarrier(HREglobal());;
    if(write_lts){
        lts_file_close(ctx.output);
    }
    //char dir[16];
    //sprintf(dir,"gmon-%d",mpi_me);
    //chdir(dir);
    HREbarrier(HREglobal());
    HREexit(0);
}
