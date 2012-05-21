// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hre/user.h>
#include <fast_hash.h>
#include <mc/dbs-ll.h>
#include <mc/dfs-stack.h>
#include <mc/lb.h>
#include <mc/scctimer.h>
#include <lts-io/user.h>
#include <spec-greybox.h>

static const size_t         PROGRESS_REPORT_THRESHOLD_INIT = (1UL<<10);
static size_t               mpi_nodes;
static int                  dbs_size = 22;

static  struct poptOption options[] = {
    SPEC_POPT_OPTIONS,
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
    {"size", 's', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &dbs_size, 0,
     "size of the state store (log2(size))", NULL},
    POPT_TABLEEND
};

/********************************************************/

typedef struct dist_thread_context wctx_t;

typedef int                *state_data_t;

struct dist_thread_context {
    model_t                 model;
    dfs_stack_t             stack;
    int                     mpi_me;
    size_t                  explored;
    size_t                  visited;
    size_t                  transitions;
    size_t                  load;
    size_t                  level_cur;
};

static dbs_ll_t             dbs;
static lb_t                *lb;
static wctx_t             **contexts;
static size_t              *threshold;
static size_t              *nr_exit;
static size_t size;

void *
smalloc (size_t size)
{
    return HREshmGet (HREglobal(), size);
}

static void
exit_ltsmin (int sig)
{
    size_t                  procs = fetch_add (nr_exit, 1);
    if ( procs >= mpi_nodes ) {
        if ( procs == mpi_nodes ) Warning(info, "UNGRACEFUL EXIT");
        exit (EXIT_FAILURE);
    } else if (lb_stop(lb)) {
        Warning(info, "PREMATURE EXIT (caught signal: %d)", sig);
    }
}

static inline void
print_state_space (wctx_t *ctx)
{
    Warning (info, "explored: %zu, transitions: %zu, load: %zu",
             ctx->explored * mpi_nodes, ctx->transitions * mpi_nodes, ctx->load * mpi_nodes);
}

static inline int
do_report (size_t explored)
{
    size_t t = *threshold ;
    return (explored & (t-1)) == 0 && cas (threshold, t, t<<1);
}

void callback (void *context, transition_info_t *t, int *dst) {
    wctx_t             *ctx = (wctx_t *) context;
    dfs_stack_push (ctx->stack, dst);
    ctx->load += 1;
    ctx->visited += 1;
    ctx->transitions += 1;
    (void) t;
}

void
dfs (wctx_t *ctx)
{
    size_t          ref;
    while ( (ctx->load = lb_balance (lb, ctx->mpi_me, ctx->load)) ) {
        int            *state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            //if (0 == ctx->mpi_me)
                HREyield(HREglobal());
            if ( !DBSLLlookup_ret(dbs, state_data, &ref) ) {
                dfs_stack_enter (ctx->stack);
                ctx->level_cur++;
                GBgetTransitionsAll (ctx->model, state_data, callback, ctx);
                ctx->explored++;
                if (do_report(ctx->explored)) print_state_space (ctx);
            } else {
                dfs_stack_pop (ctx->stack);
                ctx->load -= 1;
            }
        } else if (0 != dfs_stack_nframes (ctx->stack)) {
            ctx->level_cur--;
            dfs_stack_leave (ctx->stack);
            dfs_stack_pop (ctx->stack);
            ctx->load -= 1;
        }
    }
}

size_t
split_dfs (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    size_t              in_size = dfs_stack_size (source->stack);
    handoff = min (in_size >> 1, handoff);
    for (size_t i = 0; i < handoff; i++) {
        state_data_t        one = dfs_stack_top (source->stack);
        if (!one) {
            dfs_stack_leave (source->stack);
            source->level_cur--;
            one = dfs_stack_pop (source->stack);
            dfs_stack_push (target->stack, one);
            dfs_stack_enter (target->stack);
            target->level_cur++;
        } else {
            dfs_stack_push (target->stack, one);
            dfs_stack_pop (source->stack);
        }
    }
    return handoff;
}

int main (int argc, char*argv[]) {
	char *files[2];
    HREinitBegin (argv[0]);
    HREaddOptions (options,"Perform a distributed enumerative reachability analysis of <model>\n\nOptions");
    lts_lib_setup ();
    HREenableAll ();
    //if (!SPEC_MT_SAFE) { // incompatible with MP-aware data structures.
    //    Warning (info, "Thread-safe frontend detected, switching to pthreads.")
        HREenableThreads (0);
    //}
    HREinitStart (&argc,&argv,1,2,files,"<model> [<lts>]");

    mpi_nodes = HREpeers (HREglobal());
    int mpi_me = HREme (HREglobal());
    contexts = smalloc (sizeof(wctx_t[mpi_nodes]));
    threshold = smalloc (sizeof(size_t));
    nr_exit = smalloc (sizeof(size_t));
    for (size_t i = 0; i < mpi_nodes; i++)
        contexts[i] = smalloc (sizeof(wctx_t));
    wctx_t *ctx = contexts[mpi_me];
    ctx->mpi_me = mpi_me;
    ctx->model = GBcreateBase();
	GBsetChunkMethods (ctx->model,HREgreyboxNewmap,HREglobal(),HREgreyboxI2C,HREgreyboxC2I,HREgreyboxCount);
	GBloadFileShared (ctx->model,files[0]);
    if (mpi_me == 0) {
        *threshold = PROGRESS_REPORT_THRESHOLD_INIT;
        *nr_exit = 0;
    }
    /***************************************************/
    HREbarrier (HREglobal());
    /***************************************************/

	GBloadFile (ctx->model,files[0],&ctx->model);
    lts_type_t ltstype = GBgetLTStype(ctx->model);
    size = lts_type_get_state_length(ltstype);
    if (size<2) Fatal(1,error,"there must be at least 2 parameters");
    for (size_t i = 0; i < mpi_nodes; i++)
        contexts[i]->stack = dfs_stack_create (size, smalloc, mpi_me);
    dbs = DBSLLcreate_malloc (size, dbs_size, (hash32_f)SuperFastHash, 0, smalloc, ctx->mpi_me);
    lb = lb_create (mpi_nodes, split_dfs, 4, 0, smalloc, ctx->mpi_me);
  /// (void) signal(SIGINT, exit_ltsmin);

    /***************************************************/
    HREbarrier (HREglobal());
    /***************************************************/

	int src[size];
	int state_labels=lts_type_get_state_label_count(ltstype);
	int edge_labels=lts_type_get_edge_label_count(ltstype);
	ctx->explored = 0;
	ctx->transitions = 0;
    ctx->level_cur = 0;
    ctx->load = 0;
	if (mpi_me == 0) {
	    Warning (info,"State size: %zu, labels: %d, edge labels: %d",
	             size, state_labels, edge_labels);
	    GBgetInitialState(ctx->model,src);
	    Warning(info,"initial state computed at %d",ctx->mpi_me);
	    callback (ctx, NULL, src);
	    ctx->transitions--;
	}

	/***************************************************/
	lb_local_init (lb, mpi_me, ctx);
	/***************************************************/
    mytimer_t           timer = SCCcreateTimer ();
    SCCstartTimer (timer);
	dfs (ctx);
    SCCstopTimer (timer);
    Warning(info,"My share is %zu states and %zu transitions",ctx->explored,ctx->transitions);

    size_t global_levels=0;
    size_t global_explored=0;
    size_t global_transitions=0;
    HREreduce (HREglobal(), 1, &ctx->level_cur, &global_levels, UInt64, Max);
    HREreduce (HREglobal(), 1, &ctx->explored, &global_explored, UInt64, Sum);
    HREreduce (HREglobal(), 1, &ctx->transitions, &global_transitions, UInt64, Sum);
	if (ctx->mpi_me==0) {
	    Warning(info,"");
	    SCCreportTimer (timer, "Total exploration time");
		Warning(info,"state space has %zu levels %zu states %zu transitions",
		        global_levels,global_explored,global_transitions);
	}
    SCCdeleteTimer (timer);
	Warning(infoLong,"My share is %zu states and %zu transitions",ctx->explored,ctx->transitions);
    /***************************************************/
    HREbarrier(HREglobal());
    /***************************************************/
    HREexit(0);
}
