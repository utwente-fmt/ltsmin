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
#include <lts-io/user.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/property-semantics.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/fast_hash.h>
#include <hre/stringindex.h>
#include <util-lib/treedbs.h>
#include <util-lib/string-map.h>

struct dist_thread_context {
    model_t model;
    lts_file_t output;
    treedbs_t dbs;
    int mpi_me;
    int *tcount;
    array_manager_t state_man;
    uint32_t *parent_ofs;
    uint16_t *parent_seg;
    size_t explored,visited,transitions,level,deadlocks,errors,violations;
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
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, greybox_options, 0, "Greybox options", NULL },
    POPT_TABLEEND
};

static inline int
valid_end_state(struct dist_thread_context *ctx, int *state)
{
#if defined(SPINJA)
    return GBbuchiIsAccepting(ctx->model, state);
#endif
    return 0;
    (void) ctx; (void) state;
}

static inline void
deadlock_detect (struct dist_thread_context *ctx, int *state, int count)
{
    if (count==0 && dlk_detect && !valid_end_state(ctx, state)){
        ctx->deadlocks++;
        if (no_exit) return;
        Warning (info, "");
        Warning (info, "deadlock found at depth %zu", ctx->level);
        HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
    }
}

static inline void
invariant_detect (struct dist_thread_context *ctx, int *state)
{
    if ( !inv_expr || eval_predicate(inv_expr, NULL, state) ) return;
    ctx->violations++;
    if (no_exit) return;

    Warning (info, "");
    Warning (info, "Invariant violation (%s) found at depth %zu!", inv_detect, ctx->level);
    HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
}

static inline void
action_detect (struct dist_thread_context *ctx, transition_info_t *ti)
{
    if (-1 == act_index || NULL == ti->labels || ti->labels[act_label] != act_index) return;
    ctx->errors++;
    if (no_exit) return;
    Warning (info, "");
    Warning (info, "Error action '%s' found at depth %zu!", act_detect, ctx->level);
    HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
}

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
    struct dist_thread_context *ctx;
};

static void callback(void*context,transition_info_t*info,int*dst){
    struct src_info *ctx  = (struct src_info*)context;
    action_detect (ctx->ctx, info);
    int who=owner(dst);
    uint32_t trans[trans_len];
    trans[0]=ctx->ofs;
    for(int i=0;i<size;i++){
        trans[dst_ofs+i]=dst[i];
    }
    for(int i=0;i<edge_labels;i++){
        trans[lbl_ofs+i]=info->labels[i];
    }
    TaskSubmitFixed(ctx->new_trans,who,trans);
}

static void new_transition(void*context,int src_seg,int len,void*arg){
    struct dist_thread_context* ctx=(struct dist_thread_context*)context;
    (void)len;
    uint32_t *trans=(uint32_t*)arg;
    size_t temp=TreeFold(ctx->dbs,(int32_t*)(trans+dst_ofs));
    if (temp>=ctx->visited) {
        ctx->visited=temp+1;
        if(dlk_detect){
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

int main(int argc, char*argv[]){
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
    ctx.model = model;
    GBsetChunkMethods(model,HREgreyboxNewmap,HREglobal(),HREgreyboxI2C,HREgreyboxC2I,HREgreyboxCount);

    if (ctx.mpi_me == 0)
        GBloadFileShared(model,files[0]);
    HREbarrier(HREglobal());
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
    if (dlk_detect) {
        ctx.state_man=create_manager(65536);
        ctx.parent_ofs=NULL;
        ADD_ARRAY(ctx.state_man,ctx.parent_ofs,uint32_t);
        ctx.parent_seg=NULL;
        ADD_ARRAY(ctx.state_man,ctx.parent_seg,uint16_t);
    }
    if (act_detect) {
        // table number of first edge label
        act_label = 0;
        if (lts_type_get_edge_label_count(ltstype) == 0 ||
                strncmp(lts_type_get_edge_label_name(ltstype, act_label),
                        "action", 6) != 0)
            Abort("No edge label 'action...' for action detection");
        int typeno = lts_type_get_edge_label_typeno(ltstype, act_label);
        chunk c = chunk_str(act_detect);
        act_index = GBchunkPut(model, typeno, c);
        Warning(info, "Detecting action \"%s\"", act_detect);
    }
    if (inv_detect)
        inv_expr = parse_file (inv_detect, pred_parse_file, model);
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
    size_t global_visited,global_explored,global_transitions;
    size_t global_deadlocks,global_errors,global_violations;
    ctx.explored=0;
    ctx.transitions=0;
    ctx.level=0;
    ctx.deadlocks=0;
    ctx.errors=0;
    ctx.violations=0;
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
    src_ctx.ctx = &ctx;
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
            Warning(info,"level %d has %zu states, explored %zu states %zu transitions",
                ctx.level,global_visited-global_explored,global_explored,global_transitions);
        }
        ctx.level++;
        while(ctx.explored<limit){
            TreeUnfold(ctx.dbs,ctx.explored,src);
            src_ctx.seg=ctx.mpi_me;
            src_ctx.ofs=ctx.explored;
            ctx.explored++;
            invariant_detect (&ctx, src);
            int count=GBgetTransitionsAll(model,src,callback,&src_ctx);
            if (count<0) Abort("error in GBgetTransitionsAll");
            deadlock_detect (&ctx, src, count);
            if (state_labels){
                GBgetStateLabelsAll(model,src,labels);
            }
            if(write_lts && write_state){
                lts_write_state(ctx.output,ctx.mpi_me,src,labels);
            }

            lvl_scount++;
            lvl_tcount+=count;
            if (ctx.mpi_me == 0 && lvl_scount >= threshold) {
                Warning(info,"generated ~%d transitions from ~%d states",
                    lvl_tcount * mpi_nodes,lvl_scount * mpi_nodes);
                threshold <<= 1;
            }
            if ((lvl_scount%4)==0) HREyield(HREglobal());
        }
        //Warning(infoLong,"saw %d states and %d transitions",lvl_scount,lvl_tcount);
        TQwait(task_queue);
        HREreduce(HREglobal(),1,&ctx.visited,&global_visited,UInt64,Sum);
        HREreduce(HREglobal(),1,&ctx.explored,&global_explored,UInt64,Sum);
        HREreduce(HREglobal(),1,&ctx.transitions,&global_transitions,UInt64,Sum);
        if (global_visited==global_explored) break;
    }
    RTstopTimer(timer);
    Warning(infoLong,"My share is %lld states and %lld transitions",ctx.explored,ctx.transitions);

    HREreduce(HREglobal(),1,&ctx.deadlocks,&global_deadlocks,UInt64,Sum);
    HREreduce(HREglobal(),1,&ctx.errors,&global_errors,UInt64,Sum);
    HREreduce(HREglobal(),1,&ctx.violations,&global_violations,UInt64,Sum);

    if (ctx.mpi_me==0) {
        Warning(info,"state space has %d levels %lld states %lld transitions",
            ctx.level,global_explored,global_transitions);
        RTprintTimer (info, timer, "Exploration time");

        if (no_exit || log_active(infoLong))
            Warning (info, "\n\nDeadlocks: %zu\nInvariant violations: %zu\n"
                     "Error actions: %zu", global_deadlocks,global_violations,
                     global_errors);
    }
    /* State space was succesfully generated. */
    HREbarrier(HREglobal());;
    if (write_lts) {
        lts_file_close(ctx.output);
    }
    HREbarrier(HREglobal());
    HREexit(LTSMIN_EXIT_SUCCESS);
}
