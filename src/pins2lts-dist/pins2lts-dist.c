// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef USEMPI
#include <hre-mpi/user.h>
#else
#include <hre/user.h>
#endif
#include <hre/stringindex.h>
#include <lts-io/user.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/property-semantics.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/fast_hash.h>
#include <util-lib/treedbs.h>
#include <util-lib/string-map.h>

/** priorities of tasks */

#define NEW_EDGE_PRIO 3
#define TRACE_EXTEND_PRIO 2
#define TRACE_INITIAL_PRIO 3

//#define CHUNK_LOOKUP_PRIO 3
//#define CHUNK_REPLY_PRIO 4


struct dist_thread_context {
    model_t model;
    lts_file_t output;
    lts_file_t trace;
    uint32_t trace_next;
    hre_task_t extend_task;
    hre_task_t initial_task;
    treedbs_t dbs;
    int mpi_me;
    int *tcount;
    array_manager_t state_man;
    uint32_t *parent_ofs;
    uint16_t *parent_seg;
    size_t explored,visited,transitions,targets,level,deadlocks,errors,violations;
    ltsmin_parse_env_t env;
};

static const size_t         THRESHOLD = 100000 / 100 * SPEC_REL_PERF;
static int              trans_len;
static int              write_lts=0;
static int              nice_value=0;
static int              dlk_detect=0;
static char            *act_detect = NULL;
static char            *inv_detect = NULL;
static int              no_exit = 0;
static int              act_index = -1;
static int              act_label = -1;
static ltsmin_expr_t    inv_expr = NULL;
static int              write_state=0;
static int              size;
static int              state_labels;
static int              edge_labels;
static int              mpi_nodes;
static int              dst_ofs=2;
static int              lbl_ofs;
static char*            label_filter=NULL;
static char*            trc_output=NULL;

static  struct poptOption options[] = {
    { "filter" , 0 , POPT_ARG_STRING , &label_filter , 0 ,
      "Select the labels to be written to file from the state vector elements, "
      "state labels and edge labels." , "<patternlist>" },
    { "nice", 0, POPT_ARG_INT, &nice_value, 0, "set the nice level of all workers"
      " (useful when running on other peoples workstations)", NULL},
    { "write-state", 0, POPT_ARG_VAL, &write_state, 1, "write the full state vector", NULL },
    { "deadlock", 'd', POPT_ARG_VAL, &dlk_detect, 1, "detect deadlocks", NULL },
    { "action", 'a', POPT_ARG_STRING, &act_detect, 0, "detect error action", NULL },
    { "invariant", 'i', POPT_ARG_STRING, &inv_detect, 0, "detect invariant violations", NULL },
    { "no-exit", 'n', POPT_ARG_VAL, &no_exit, 1, "no exit on error, just count (for error counters use -v)", NULL },
    SPEC_POPT_OPTIONS,
    {"trace", 0, POPT_ARG_STRING, &trc_output, 0, "file to write trace to", "<lts file>" },
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, greybox_options, 0, "PINS options", NULL },
    POPT_TABLEEND
};

static void start_trace(struct dist_thread_context* ctx,uint32_t seg,uint32_t ofs);

static inline void
deadlock_detect (struct dist_thread_context *ctx, int *state, int count)
{
    if (count != 0) return;
    ctx->deadlocks++;
    if (GBstateIsValidEnd(ctx->model, state)) return;
    if ( !inv_expr ) ctx->violations++;
    if (!dlk_detect) return;
    if (trc_output!=NULL){
        uint32_t ofs=TreeFold(ctx->dbs,(int32_t*)(state));
        Warning(info,"deadlock: %u.%u",ctx->mpi_me,ofs);
        start_trace(ctx,ctx->mpi_me,ofs);
        return;
    }
    if (no_exit) return;
    Warning (info, " ");
    Warning (info, "deadlock found at depth %zu", ctx->level);
    HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
}

static inline void
invariant_detect (struct dist_thread_context *ctx, int *state)
{
    if ( !inv_expr || eval_predicate(ctx->model, inv_expr, NULL, state, size, ctx->env) ) return;
    ctx->violations++;
    if (no_exit) return;

    Warning (info, " ");
    Warning (info, "Invariant violation (%s) found at depth %zu!", inv_detect, ctx->level);
    HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
}

static inline void
action_detect (struct dist_thread_context *ctx, transition_info_t *ti)
{
    if (-1 == act_index || NULL == ti->labels || ti->labels[act_label] != act_index) return;
    ctx->errors++;
    if (no_exit) return;
    Warning (info, " ");
    Warning (info, "Error action '%s' found at depth %zu!", act_detect, ctx->level);
    HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
}

static uint32_t chk_base=0;

static inline void adjust_owner(int32_t *state){
    chk_base=SuperFastHash((char*)state,size*4,0);
}

static inline int owner(int *state){
    uint32_t hash=chk_base^SuperFastHash((char*)state,size*4,0);
    return (hash%mpi_nodes);
}

/********************************************************/

struct src_info {
    int seg;
    int ofs;
    hre_task_t new_trans;
    stream_t fifo;
    struct dist_thread_context *ctx;
};

static void callback(void*context,transition_info_t*info,int*dst){
    struct src_info *ctx  = (struct src_info*)context;
    action_detect (ctx->ctx, info);
    uint32_t trans[trans_len];
    trans[0]=ctx->ofs;
    for(int i=0;i<size;i++){
        trans[dst_ofs+i]=dst[i];
    }
    for(int i=0;i<edge_labels;i++){
        trans[lbl_ofs+i]=info->labels[i];
    }
    if (ctx->fifo==NULL){
        TaskSubmitFixed(ctx->new_trans,owner(dst),trans);
    } else {
        stream_write(ctx->fifo,trans,sizeof(trans));
    }
    info->por_proviso = 1;
}

/********************************************************/

static void extend_trace(struct dist_thread_context* ctx,uint32_t* trc_vector);

static void start_trace(struct dist_thread_context* ctx,uint32_t seg,uint32_t ofs){
    if (ctx->mpi_me!=(int)seg) Abort("cannot start trace worker is not owner of segment %u",seg);
    uint32_t trc_vector[4];
    trc_vector[0]=seg;
    trc_vector[1]=ofs;
    trc_vector[2]=seg;
    trc_vector[3]=ofs;
    Debug("generating trace to %u.%u: end point %u.%u",seg,ofs,seg,ctx->trace_next);
    extend_trace(ctx,trc_vector);
}

static void extend_trace(struct dist_thread_context* ctx,uint32_t* trc_vector){
    int real_vector[size];
    TreeUnfold(ctx->dbs,trc_vector[3],real_vector);
    Debug("writing state %d.%d",trc_vector[2],ctx->trace_next);
    lts_write_state(ctx->trace,trc_vector[2],&ctx->trace_next,real_vector);
    uint32_t seg=ctx->parent_seg[trc_vector[3]];
    uint32_t ofs=ctx->parent_ofs[trc_vector[3]];
    if (seg==trc_vector[2] && ofs==trc_vector[3]){
        Debug("generating trace to %u.%u: initial   %u.%u",trc_vector[0],trc_vector[1],seg,ctx->trace_next);
        uint32_t message[3];
        message[0]=trc_vector[0];
        message[1]=trc_vector[1];
        message[2]=ctx->trace_next;
        ctx->trace_next++;
        TaskSubmitFixed(ctx->initial_task,0,message);
    } else {
        Debug("generating trace to %u.%u: next worker %u",trc_vector[0],trc_vector[1],seg);
        uint32_t message[5];
        message[0]=trc_vector[0];
        message[1]=trc_vector[1];
        message[2]=seg;
        message[3]=ofs;
        message[4]=ctx->trace_next;
        ctx->trace_next++;
        TaskSubmitFixed(ctx->extend_task,seg,message);
    }
}

static void extend_trace_task(void*context,int src_seg,int len,void*arg){
    struct dist_thread_context* ctx=(struct dist_thread_context*)context;
    (void)len;
    uint32_t *message=(uint32_t*)arg;
    Debug("generating trace to %u.%u: transition %u.%u <- %u.%u",message[0],message[1],src_seg,message[4],ctx->mpi_me,ctx->trace_next);
    lts_write_edge(ctx->trace,ctx->mpi_me,&(ctx->trace_next),src_seg,&(message[4]),NULL);
    Debug("edge written");
    extend_trace(ctx,message);
}

static void initial_trace_task(void*context,int src_seg,int len,void*arg){
    struct dist_thread_context* ctx=(struct dist_thread_context*)context;
    (void)len;
    uint32_t *message=(uint32_t*)arg;
    Debug("generating trace to %u.%u: initial state %u.%u",message[0],message[1],src_seg,message[2]);
    lts_write_init(ctx->trace,src_seg,&(message[2]));
    Debug("initial state written");
}

/********************************************************/

static void new_transition(void*context,int src_seg,int len,void*arg){
    struct dist_thread_context* ctx=(struct dist_thread_context*)context;
    (void)len;
    uint32_t *trans=(uint32_t*)arg;
    size_t temp=TreeFold(ctx->dbs,(int32_t*)(trans+dst_ofs));
    if (temp>=ctx->visited) {
        ctx->visited=temp+1;
        if(trc_output!=NULL){
            ensure_access(ctx->state_man,temp);
            ctx->parent_seg[temp]=src_seg;
            ctx->parent_ofs[temp]=trans[0];
        }
    }
    if (write_lts){
        lts_write_edge(ctx->output,src_seg,trans,ctx->mpi_me,&temp,trans+lbl_ofs);
    }
    ctx->tcount[src_seg]++;
    ctx->targets++;
}


struct repr_info {
    int first;
    int next;
    int last;
    int number;
    int low_number;
    int low_state;
    int back;
};

struct repr_context {
    array_manager_t state_man;
    array_manager_t trans_man;
    int* trans;
    struct repr_info *info;
    treedbs_t dbs;
    int trans_next;
};

static void discard_callback(void*context,transition_info_t*ti,int*dst){
    (void) context; (void) ti; (void) dst;
}

static void repr_callback(void*context,transition_info_t*ti,int*dst){
    struct repr_context *ctx  = (struct repr_context*)context;
    int idx;
    if (!TreeFold_ret(ctx->dbs,dst,&idx)){
        //Warning(info,"new index %d",idx);
        ensure_access(ctx->state_man,idx);
        ctx->info[idx].first=-1;
    } else {
        //Warning(info,"old index %d",idx);
    }
    ensure_access(ctx->trans_man,ctx->trans_next);
    ctx->trans[ctx->trans_next]=idx;
    ctx->trans_next++;
    (void) ti;
}

static void repr_explore(struct repr_context* ctx,model_t model,matrix_t* confluent,int idx,int *state){
    ctx->info[idx].first=ctx->trans_next;
    ctx->info[idx].next=ctx->trans_next;
    int trans=GBgetTransitionsMarked(model,confluent,0,state,repr_callback,ctx);
    ctx->info[idx].last=ctx->trans_next;
    if (trans!=(ctx->info[idx].last-ctx->info[idx].first)) Abort("confluent hyper edge detected");
    if (trans>0){
        Debug("marked confluent edges found (%d).",trans);
        return;
    }
    if (trans==0) {
      Debug("check silent steps for single edge, without non-confluence markers.");
      trans=GBgetTransitionsMarked(model,confluent,1,state,repr_callback,ctx);
      ctx->info[idx].last=ctx->trans_next;
      if (trans!=1) {
        Debug("not a single silent edge.");
        ctx->info[idx].last=ctx->info[idx].first;
        ctx->trans_next=ctx->info[idx].first;
        return;
      }
      if (trans!=(ctx->info[idx].last-ctx->info[idx].first)) {
        Debug("hyper edge does not count as confluent.");
        ctx->info[idx].last=ctx->info[idx].first;
        ctx->trans_next=ctx->info[idx].first;
        return;
      }
      trans=GBgetTransitionsMarked(model,confluent,2,state,discard_callback,NULL);
      if (trans!=0) {
        Debug("any non-confluent marker makes step non-confluent.");
        ctx->info[idx].last=ctx->info[idx].first;
        ctx->trans_next=ctx->info[idx].first;
        return;
      }
      lts_type_t ltstype=GBgetLTStype(model);
      int N=lts_type_get_state_length(ltstype);
      int S=lts_type_get_state_label_count(ltstype);
      if (S>0) {
          Debug("checking state labels");
          int L1[S];
          int S2[N];
          int L2[S];
          TreeUnfold(ctx->dbs,ctx->trans[ctx->info[idx].first],S2);
          GBgetStateLabelsAll(model,state,L1);
          GBgetStateLabelsAll(model,S2,L2);
          for(int i=0;i<S;i++){
            if (L1[i]!=L2[i]){
              Debug("not silent due to state label %d difference",i);
              ctx->info[idx].last=ctx->info[idx].first;
              ctx->trans_next=ctx->info[idx].first;
              return;
            }
          }
      }
      Debug("dynamic confluent edge.");
    }
}

int state_less(treedbs_t dbs,int N,int idx1,int idx2){
    int s1[N];
    int s2[N];
    TreeUnfold(dbs,idx1,s1);
    TreeUnfold(dbs,idx2,s2);
    for(int i=0;i<N;i++){
        if (s1[i] < s2[i]) return 1;
        if (s1[i] > s2[i]) return 0;
    }
    return 0;
}

static void get_repr(model_t model,matrix_t* confluent,int *state){
    lts_type_t ltstype=GBgetLTStype(model);
    int N=lts_type_get_state_length(ltstype);
    struct repr_context ctx;

    ctx.trans_man=create_manager(256);
    ctx.trans=NULL;
    ctx.trans_next=0;
    ADD_ARRAY(ctx.trans_man,ctx.trans,int);
    
    ctx.state_man=create_manager(256);
    ctx.info=NULL;
    ADD_ARRAY(ctx.state_man,ctx.info,struct repr_info);
    
    ctx.dbs=TreeDBScreate(N);    

    int current=TreeFold(ctx.dbs,state);
    if (current!=0) Abort("root is not 0");
    repr_explore(&ctx,model,confluent,0,state);
    ctx.info[0].number=0;
    int next_number=1;
    int next;
    for(;;){
        if(ctx.info[current].first==-1){
            TreeUnfold(ctx.dbs,current,state);
            repr_explore(&ctx,model,confluent,current,state);
            ctx.info[current].number=next_number;
            ctx.info[current].low_number=next_number;
            ctx.info[current].low_state=current;
            next_number++;
            if(ctx.info[current].first==ctx.info[current].last){
                //Warning(info,"trivial TSCC found");
                break;
            }
        }
        if (ctx.info[current].next<ctx.info[current].last) {
          next=ctx.trans[ctx.info[current].next];
          ctx.info[current].next++;
          if (ctx.info[next].first==-1){
            ctx.info[next].back=current;
            current=next;  
          } else {
            if (ctx.info[next].number < ctx.info[current].low_number){
                ctx.info[current].low_number = ctx.info[next].number;
            }
            if (state_less(ctx.dbs,N,next,ctx.info[current].low_state)){
                ctx.info[current].low_state = next;
            }
          }
          continue;
        }
        if (ctx.info[current].low_number==ctx.info[current].number) {
            //Warning(info,"non-trivial TSCC found");
            break;
        }
        next=ctx.info[current].back;
        if (ctx.info[current].low_number < ctx.info[next].low_number) {
            ctx.info[next].low_number=ctx.info[current].low_number;
        }
        if (state_less(ctx.dbs,N,ctx.info[current].low_state,ctx.info[next].low_state)){
            ctx.info[next].low_state = ctx.info[current].low_state;
        }
        current=next;
    }

    TreeUnfold(ctx.dbs,ctx.info[current].low_state,state);
    destroy_manager(ctx.trans_man);
    destroy_manager(ctx.state_man);
    TreeDBSfree(ctx.dbs);
}

int main(int argc, char*argv[]){
    char *files[2];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Perform a distributed enumerative reachability analysis of <model>\n\nOptions");
    lts_lib_setup();
    HREenableAll();
    if (!SPEC_MT_SAFE){
        HREenableThreads(0, false);
    }
    HREinitStart(&argc,&argv,1,2,files,"<model> [<lts>]");

    struct dist_thread_context ctx;
    mpi_nodes=HREpeers(HREglobal());
    ctx.mpi_me=HREme(HREglobal());

    hre_task_queue_t task_queue=HREcreateQueue(HREglobal());

    ctx.tcount=(int*)RTmalloc(mpi_nodes*sizeof(int));
    memset(ctx.tcount,0,mpi_nodes*sizeof(int));

    model_t model=GBcreateBase();
    ctx.model = model;
    GBsetChunkMethods(model,HREgreyboxNewmap,HREglobal(),
                      HREgreyboxI2C,
                      HREgreyboxC2I,
                      HREgreyboxCAtI,
                      HREgreyboxCount);

    if (ctx.mpi_me == 0)
        GBloadFileShared(model,files[0]);
    HREbarrier(HREglobal());
    GBloadFile(model,files[0],&model);

    HREbarrier(HREglobal());
    Warning(info,"model created");
    lts_type_t ltstype=GBgetLTStype(model);
    size=lts_type_get_state_length(ltstype);

    int class_label=lts_type_find_edge_label(ltstype,LTSMIN_EDGE_TYPE_ACTION_CLASS);
    matrix_t *inhibit_matrix=NULL;
    matrix_t *class_matrix=NULL;
    matrix_t *confluence_matrix=NULL;
    {
        int id=GBgetMatrixID(model,"inhibit");
        if (id>=0){
            inhibit_matrix=GBgetMatrix(model,id);
            Warning(infoLong,"inhibit matrix is:");
            if (log_active(infoLong)) dm_print(stderr,inhibit_matrix);
        } else {
            Warning(infoLong,"no inhibit matrix");
        }
        id = GBgetMatrixID(model,LTSMIN_EDGE_TYPE_ACTION_CLASS);
        if (id>=0){
            class_matrix=GBgetMatrix(model,id);
            Warning(infoLong,"inhibit class matrix is:");
            if (log_active(infoLong)) dm_print(stderr,class_matrix);
        } else {
            Warning(infoLong,"no inhibit class matrix");
        }
        if (class_label>=0) {
            Warning(infoLong,"inhibit class label is %d",class_label);
        } else {
            Warning(infoLong,"no inhibit class label");
        }
        id = GBgetMatrixID(model,"confluent");
        if (id>=0){
            confluence_matrix=GBgetMatrix(model,id);
            Warning(infoLong,"confluence matrix is:");
            if (log_active(infoLong)) dm_print(stderr,confluence_matrix);
        } else {
            Warning(infoLong,"no confluence matrix");
        }
    }

    /* Initializing according to the options just parsed.
     */
    if (nice_value) {
        if (ctx.mpi_me==0) Warning(info,"setting nice to %d",nice_value);
        int rv = nice(nice_value);
        if (rv==-1) Warning(info,"failed to set nice");
    }
    /***************************************************/
    if (trc_output!=NULL) {
        ctx.state_man=create_manager(65536);
        ctx.parent_ofs=NULL;
        ADD_ARRAY(ctx.state_man,ctx.parent_ofs,uint32_t);
        ctx.parent_seg=NULL;
        ADD_ARRAY(ctx.state_man,ctx.parent_seg,uint16_t);
        if (ctx.mpi_me==0) {
            ensure_access(ctx.state_man,0);
            ctx.parent_ofs[0]=0;
            ctx.parent_seg[0]=0;
        }
        ctx.extend_task=TaskCreate(task_queue,TRACE_EXTEND_PRIO,65536,extend_trace_task,&ctx,20);
        TaskEnableFifo(ctx.extend_task);
        ctx.initial_task=TaskCreate(task_queue,TRACE_INITIAL_PRIO,65536,initial_trace_task,&ctx,12);
        ctx.trace_next=0;
        lts_file_t template=lts_index_template();
        lts_file_set_edge_owner(template,SourceOwned);
        lts_type_t trace_type=lts_type_create();
        lts_type_set_state_label_count(trace_type,size);
        for(int i=0;i<size;i++){
            char*type_name=lts_type_get_state_type(ltstype,i);
            if (ctx.mpi_me==0) Warning(info,"label %d is %s:%s",i,lts_type_get_state_name(ltstype,i),type_name);
            lts_type_set_state_label_name(trace_type,i,lts_type_get_state_name(ltstype,i));
            int is_new;
            int typeno=lts_type_add_type(trace_type,type_name,&is_new);
            if (is_new){
                int type_orig=lts_type_get_state_typeno(ltstype,i);
                lts_type_set_format(trace_type,typeno,lts_type_get_format(ltstype,type_orig));
            }
            lts_type_set_state_label_typeno(trace_type,i,typeno);
        }
        ctx.trace=lts_file_create(trc_output,trace_type,mpi_nodes,template);
        int T=lts_type_get_type_count(trace_type);
        for(int i=0;i<T;i++){
            int typeno=lts_type_find_type(ltstype,lts_type_get_type(trace_type,i));
            if (typeno<0) continue;
            void *table=GBgetChunkMap(model,typeno);
            Debug("address of table %d/%d: %p",i,typeno,table);
            lts_file_set_table(ctx.trace,i,table);
        }
        HREbarrier(HREglobal());
    }
    if (act_detect) {
        if (PINS_POR) Abort ("Distributed tool implements no cycle provisos.");
        // table number of first edge label
        act_label = lts_type_find_edge_label_prefix (ltstype, LTSMIN_EDGE_TYPE_ACTION_PREFIX);
        if (act_label == -1)
            Abort("No edge label '%s...' for action detection", LTSMIN_EDGE_TYPE_ACTION_PREFIX);
        int typeno = lts_type_get_edge_label_typeno(ltstype, act_label);
        chunk c = chunk_str(act_detect);
        act_index = GBchunkPut(model, typeno, c);
        Warning(info, "Detecting action \"%s\"", act_detect);
    }
    if (inv_detect) {
        if (PINS_POR) Abort ("Distributed tool implements no cycle provisos.");
        ltsmin_parse_env_t env = LTSminParseEnvCreate();
        inv_expr = parse_file_env (inv_detect, pred_parse_file, model, env);
        ctx.env = env;
    }
    HREbarrier(HREglobal());
    /***************************************************/
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
    if (confluence_matrix!=NULL){
      get_repr(model,confluence_matrix,src);
      Warning(info,"representative of initial state computed at %d",ctx.mpi_me);
    }
    adjust_owner(src);
    Warning(info,"initial state translated at %d",ctx.mpi_me);
    size_t global_visited,global_explored,global_transitions,global_targets;
    size_t global_deadlocks,global_errors,global_violations;
    ctx.explored=0;
    ctx.transitions=0;
    ctx.targets=0;
    ctx.level=0;
    ctx.deadlocks=0;
    ctx.errors=0;
    ctx.violations=0;
    global_visited=1;
    global_explored=0;
    global_transitions=0;
    global_targets=0;
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
        // get default filter.
        string_set_t label_set=GBgetDefaultFilter(model);
        // get command line override if it exists.
        if (label_filter!=NULL){
            label_set=SSMcreateSWPset(label_filter);
        }
        if (write_state) {
            // write-state means write everything.
            ctx.output=lts_file_create(files[1],ltstype,mpi_nodes,lts_index_template());
        } else if (label_set!=NULL) {
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
    src_ctx.new_trans=TaskCreate(task_queue,NEW_EDGE_PRIO,65536,new_transition,&ctx,trans_len*4);
    src_ctx.ctx = &ctx;
    if (confluence_matrix==NULL) {
        src_ctx.fifo=NULL;
    } else {
        src_ctx.fifo=FIFOcreate(4096);
    }
    rt_timer_t timer = RTcreateTimer();
    /***************************************************/

    RTstartTimer(timer);
    size_t threshold = THRESHOLD;
    for(;;){
        size_t limit=ctx.visited;
        size_t lvl_scount=0;
        size_t lvl_tcount=0;
        HREbarrier(HREglobal());
        if (ctx.mpi_me==0) {
            Warning(info,"level %zu has %zu states, explored %zu states %zu transitions",
                ctx.level,global_visited-global_explored,global_explored,global_transitions);
        }
        ctx.level++;
        while(ctx.explored<limit){
            TreeUnfold(ctx.dbs,ctx.explored,src);
            src_ctx.seg=ctx.mpi_me;
            src_ctx.ofs=ctx.explored;
            ctx.explored++;
            invariant_detect (&ctx, src);
            int count;
            if (inhibit_matrix!=NULL){
                    int N=dm_nrows(inhibit_matrix);
                    int class_count[N];
                    count=0;
                    for(int i=0;i<N;i++){
                        class_count[i]=0;
                        int j=0;
                        for(;j<i;j++){
                            if (class_count[j]>0 && dm_is_set(inhibit_matrix,j,i)) break;
                        }
                        if (j<i) continue;
                        if (class_label>=0){
                            class_count[i]=GBgetTransitionsMatching(model,class_label,i,src,callback,&src_ctx);
                        } else if (class_matrix!=NULL) {
                            class_count[i]=GBgetTransitionsMarked(model,class_matrix,i,src,callback,&src_ctx);
                        } else {
                            Abort("inhibit set, but no known classification found.");
                        }
                        count+=class_count[i];
                    }
            } else {
                count=GBgetTransitionsAll(model,src,callback,&src_ctx);
            }
            ctx.transitions+=count;
            if(confluence_matrix!=NULL){
              int trans[trans_len];
              while(FIFOsize(src_ctx.fifo)>0){
                stream_read(src_ctx.fifo,trans,sizeof(trans));
                get_repr(model,confluence_matrix,trans+dst_ofs);
                TaskSubmitFixed(src_ctx.new_trans,owner(trans+dst_ofs),trans);
              }
            }
            if (count<0) Abort("error in GBgetTransitionsAll");
            deadlock_detect (&ctx, src, count);
            if(write_lts && write_state){
                if (state_labels)
                    GBgetStateLabelsAll(model,src,labels);
                lts_write_state(ctx.output,ctx.mpi_me,src,labels);
            }

            lvl_scount++;
            lvl_tcount+=count;
            if (ctx.mpi_me == 0 && lvl_scount >= threshold) {
                Warning(info,"generated ~%zu transitions from ~%zu states",
                    lvl_tcount * mpi_nodes,lvl_scount * mpi_nodes);
                threshold <<= 1;
            }
            if ((lvl_scount%4)==0) HREyield(HREglobal());
        }
        Debug("saw %zu states and %zu transitions",lvl_scount,lvl_tcount);
        TQwait(task_queue);
        HREreduce(HREglobal(),1,&ctx.visited,&global_visited,UInt64,Sum);
        HREreduce(HREglobal(),1,&ctx.explored,&global_explored,UInt64,Sum);
        HREreduce(HREglobal(),1,&ctx.transitions,&global_transitions,UInt64,Sum);
        HREreduce(HREglobal(),1,&ctx.targets,&global_targets,UInt64,Sum);
        HREreduce(HREglobal(),1,&ctx.violations,&global_violations,UInt64,Sum);
        if (!no_exit && global_violations>0) break;
        if (global_visited==global_explored) break;
    }
    RTstopTimer(timer);
    Warning(infoLong,"My share is %zu states and %zu transitions",ctx.explored,ctx.transitions);

    HREreduce(HREglobal(),1,&ctx.deadlocks,&global_deadlocks,UInt64,Sum);
    HREreduce(HREglobal(),1,&ctx.errors,&global_errors,UInt64,Sum);
    HREreduce(HREglobal(),1,&ctx.violations,&global_violations,UInt64,Sum);

    if (ctx.mpi_me==0) {
        if (global_transitions==0 && global_targets>0) {
            Warning(error,"language module fails to report the number of transitions");
            Warning(error,"assuming number of transitions is number of targets");
            global_transitions=global_targets;
        }
        if (global_targets > global_transitions) {
            Warning(info,"state space has %zu levels %zu states %zu transitions %zu targets",
                ctx.level,global_explored,global_transitions,global_targets);
        } else {
            Warning(info,"state space has %zu levels %zu states %zu transitions",
                ctx.level,global_explored,global_transitions);
        }
        RTprintTimer (info, timer, "Exploration time");

        if (no_exit || trc_output != NULL || log_active(infoLong))
            HREprintf (info, "\nDeadlocks: %zu\nInvariant violations: %zu\n"
                     "Error actions: %zu\n", global_deadlocks,global_violations,
                     global_errors);
    }
    /* State space was succesfully generated. */
    HREbarrier(HREglobal());
    if (write_lts) {
        lts_file_close(ctx.output);
    }
    HREbarrier(HREglobal());
    if (trc_output!=NULL) {
        lts_file_close(ctx.trace);
    }
    HREbarrier(HREglobal());
    HREexit(LTSMIN_EXIT_SUCCESS);
}
