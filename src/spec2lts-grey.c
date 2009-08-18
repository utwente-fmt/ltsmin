#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>
#include <assert.h>

#include <stdint.h>
#include <lts_enum.h>
#include <lts_io.h>
#include <stringindex.h>
#include <limits.h>
#include "dynamic-array.h"

#include "archive.h"
#include "runtime.h"
#include "treedbs.h"
#include "vector_set.h"
#include "dfs-stack.h"
#include "is-balloc.h"
#include "bitset.h"
#include "scctimer.h"

#if defined(MCRL)
#include "mcrl-greybox.h"
#endif
#if defined(MCRL2)
#include "mcrl2-greybox.h"
#endif
#if defined(NIPS)
#include "nips-greybox.h"
#endif
#if defined(ETF)
#include "etf-greybox.h"
#endif

static lts_enum_cb_t output_handle=NULL;

static char* trc_output=NULL;
static int dlk_detect=0;
static lts_enum_cb_t trace_handle=NULL;
static lts_output_t trace_output=NULL;

static array_manager_t state_man=NULL;
static uint32_t *parent_ofs=NULL;

static treedbs_t dbs=NULL;
static int write_lts;
static int matrix=0;
static int write_state=0;
static size_t max = UINT_MAX;

typedef enum { UseGreyBox , UseBlackBox } mode_t;
static mode_t call_mode=UseBlackBox;

static char *arg_strategy = "bfs";
static enum { Strat_BFS, Strat_DFS } strategy = Strat_BFS;
static char *arg_state_db = "tree";
static enum { DB_TreeDBS, DB_Vset } state_db = DB_TreeDBS;

static si_map_entry strategies[] = {
    {"bfs",  Strat_BFS},
    {"dfs",  Strat_DFS},
    {NULL, 0}
};

static si_map_entry db_types[]={
    {"tree", DB_TreeDBS},
    {"vset", DB_Vset},
    {NULL, 0}
};

static void
state_db_popt (poptContext con, enum poptCallbackReason reason,
               const struct poptOption *opt, const char *arg, void *data)
{
    (void)con; (void)opt; (void)arg; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST: {
            if (trc_output)
                dlk_detect = 1;

            int db = linear_search (db_types, arg_state_db);
            if (db < 0) {
                Warning (error, "unknown vector storage mode type %s", arg_state_db);
                RTexitUsage (EXIT_FAILURE);
            }
            state_db = db;

            int s = linear_search (strategies, arg_strategy);
            if (s < 0) {
                Warning (error, "unknown search mode %s", arg_strategy);
                RTexitUsage (EXIT_FAILURE);
            }
            strategy = s;
        }
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal (1, error, "unexpected call to state_db_popt");
}

static  struct poptOption development_options[] = {
	{ "grey", 0 , POPT_ARG_VAL , &call_mode , UseGreyBox , "make use of GetTransitionsLong calls" , NULL },
	{ "matrix", 0 , POPT_ARG_VAL, &matrix,1,"Print the dependency matrix and quit",NULL},
	{ "write-state" , 0 , POPT_ARG_VAL , &write_state, 1 , "write the full state vector" , NULL },
	POPT_TABLEEND
};

static  struct poptOption options[] = {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION  , (void*)state_db_popt , 0 , NULL , NULL },
	{ "deadlock" , 'd' , POPT_ARG_VAL , &dlk_detect , 1 , "detect deadlocks" , NULL },
	{ "trace" , 0 , POPT_ARG_STRING , &trc_output , 0 , "file to write trace to" , "<lts output>" },
	{ "state" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &arg_state_db , 0 ,
		"select the data structure for storing states", "<tree|vset>"},
	{ "strategy" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &arg_strategy , 0 ,
		"select the search strategy", "<bfs|dfs>"},
	{ "max" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &max , 0 ,"maximum search depth", "<int>"},
#if defined(MCRL)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl_options , 0 , "mCRL options", NULL },
#endif
#if defined(MCRL2)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl2_options , 0 , "mCRL2 options", NULL },
#endif
#if defined(NIPS)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, nips_options , 0 , "NIPS options", NULL },
#endif
#if defined(ETF)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, etf_options , 0 , "ETF options", NULL },
#endif
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_setonly_options , 0 , "Vector set options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL , NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, development_options , 0 , "Development options" , NULL },
	POPT_TABLEEND
};

static vdom_t domain;
static vset_t visited_set;
static vset_t being_explored_set;
static vset_t next_set;
static bitset_t dfs_open_set;
static dfs_stack_t stack;

static int N;
static int K;
static int state_labels;
static int edge_labels;
static int visited=1;
static int explored=0;
static int trans=0;

static void
maybe_write_state (model_t model, const int *idx, const int *state)
{
    if (write_lts) {
        int                 labels[state_labels];
        if (state_labels)
            GBgetStateLabelsAll (model, (int *)state, labels);
        if (write_state || idx == NULL) {
            assert (state != NULL);
            enum_state (output_handle, 0 , (int *)state, labels);
        } else {
            assert (idx != NULL);
            if (state_labels) enum_state (output_handle, 0, (int *)idx, labels);
        }
    }
}

static void *
new_string_index (void *context)
{
    (void)context;
    Warning (info, "creating a new string index");
    return SIcreate ();
}

/* Transition Callbacks */
static void
vector_next (void *arg, int *lbl, int *dst)
{
    int                 src_ofs = *(int *)arg;
    if (!vset_member (visited_set, dst)) {
        visited++;
        vset_add (visited_set, dst);
        vset_add (next_set, dst);
    }
    if (write_lts) enum_seg_vec (output_handle, 0, src_ofs, dst, lbl);
    trans++;
}

static void
index_next (void *arg, int *lbl, int *dst)
{
    int                 src_ofs = *(int *)arg;
    int                 idx = TreeFold (dbs, dst);
    if (idx >= visited) {
        visited = idx + 1;
        if (trc_output) {
            ensure_access(state_man,idx);
            parent_ofs[idx]=src_ofs;
        }
    }
    if (write_lts) enum_seg_seg (output_handle, 0, src_ofs, 0, idx, lbl);
    trans++;
}

static inline void get_state(int state_no, int *state)
{
    TreeUnfold(dbs, state_no, state);
}

static void write_trace_state(model_t model, int src_no, int *state){
    Warning(debug,"dumping state %d",src_no);
    int labels[state_labels];
    if (state_labels) GBgetStateLabelsAll(model,state,labels);
    enum_state(trace_handle,0,state,labels);
}

struct write_trace_step_s {
    int src_no;
    int dst_no;
    int* dst;
    int found;
};

static void write_trace_next(void*arg,int*lbl,int*dst){
    struct write_trace_step_s*ctx=(struct write_trace_step_s*)arg;
    if(ctx->found) return;
    for(int i=0;i<N;i++) {
        if (ctx->dst[i]!=dst[i]) return;
    }
    ctx->found=1;
    enum_seg_seg(trace_handle,0,ctx->src_no,0,ctx->dst_no,lbl);
}

static void write_trace_step(model_t model, int src_no,int*src,int dst_no,int*dst){
    Warning(debug,"finding edge for state %d",src_no);
    struct write_trace_step_s ctx;
    ctx.src_no=src_no;
    ctx.dst_no=dst_no;
    ctx.dst=dst;
    ctx.found=0;
    GBgetTransitionsAll(model,src,write_trace_next,&ctx);
    if (ctx.found==0) Fatal(1,error,"no matching transition found");
}

static void find_trace_to(model_t model, int dst_idx, int level)
{
    uint32_t *trace = (uint32_t*)RTmalloc(sizeof(uint32_t) * level);

    if (trace == NULL)
        Fatal(1, error, "unable to allocate memory for trace");

    int i = level - 1;
    int curr_idx = dst_idx;
    trace[i] = curr_idx;
    while(curr_idx != 0)
    {
        i--;
        curr_idx = parent_ofs[curr_idx];
        trace[i] = curr_idx;
    }

    // write trace

    // write initial state
    int step = 0;
    int src[N];
    int dst[N];
    get_state(0, dst);
    write_trace_state(model, 0, dst);

    i++;
    while(i < level)
    {
        for(int j=0; j < N; ++j)
            src[j] = dst[j];
        get_state(trace[i], dst);

        // write step
        write_trace_step(model, step, src, step + 1, dst);
        // write dst_idx
        write_trace_state(model, trace[i], dst);

        i++;
        step++;
    }

    RTfree(trace);

    return;
}

static void find_trace(model_t model, int dst_idx, int level) {
    mytimer_t timer = SCCcreateTimer();
    SCCstartTimer(timer);
    find_trace_to(model, dst_idx, level);
    SCCstopTimer(timer);
    SCCreportTimer(timer,"constructing the trace took");
}

static void
vector_next_dfs (void *arg, int *lbl, int *dst)
{
    int                 src_ofs = *(int *)arg;
    if (!vset_member (being_explored_set, dst)) {
        ++visited;
        dfs_stack_push (stack, dst);
    }
    if (write_lts) enum_seg_vec (output_handle, 0, src_ofs, dst, lbl);
    ++trans;
}

static void
index_next_dfs (void *arg, int *lbl, int *dst)
{
    int                 src_ofs = *(int *)arg;
    int                 idx = TreeFold (dbs, dst);
    dfs_stack_push (stack, &idx);
    if (idx >= visited) {
        visited = idx + 1;
        bitset_set (dfs_open_set, idx);
    }
    if (write_lts) enum_seg_seg (output_handle, 0, src_ofs, 0, idx, lbl);
    ++trans;
}


/* Exploration */
static void
bfs_explore_state_index (void *context, int idx, int *src, int level)
{
    model_t             model = (model_t)context;
    maybe_write_state (model, &idx, src);
    int count = 0;
    switch (call_mode) {
    case UseBlackBox:
        count = GBgetTransitionsAll (model, src, index_next, &idx);
        break;
    case UseGreyBox:
        for (int i = 0; i < K; i++) {
            count += GBgetTransitionsLong (model, i, src, index_next, &idx);
        }
        break;
    }
    if (count == 0 && dlk_detect) {
        Warning(info,"deadlock found in state %d", idx);
        if (trc_output) {
            trace_output=lts_output_open(trc_output,model,1,0,1,"vsi",NULL);
            {
                int init_state[N];
                get_state(0, init_state);
                lts_output_set_root_vec(trace_output,(uint32_t*)init_state);
                lts_output_set_root_idx(trace_output,0,0);
            }
            trace_handle=lts_output_begin(trace_output,0,1,0);
            find_trace(model, idx, level);
            lts_output_end(trace_output,trace_handle);
            lts_output_close(&trace_output);
        }
        Fatal(1,info, "exiting now");
    }

    explored++;
    if (explored % 1000 == 0 && RTverbosity >= 2)
        Warning (info, "explored %d visited %d trans %d", explored, visited, trans);
}

static void
bfs_explore_state_vector (void *context, int *src)
{
    model_t             model = (model_t)context;
    maybe_write_state (model, NULL, src);
	int count = 0;
    switch (call_mode) {
    case UseBlackBox:
        count = GBgetTransitionsAll (model, src, vector_next, &explored);
        break;
    case UseGreyBox:
        for (int i = 0; i < K; i++) {
            count += GBgetTransitionsLong (model, i, src, vector_next, &explored);
        }
        break;
    }
	if (count == 0 && dlk_detect) {
		Warning(info,"deadlock found!");
		Fatal(1,info, "exiting now");
	}
    explored++;
    if (explored % 1000 == 0 && RTverbosity >= 2)
        Warning (info, "explored %d visited %d trans %d", explored, visited, trans);
}

static void
dfs_explore_state_vector (model_t model, int src_idx, const int *src,
                          int *o_next_group)
{
    if (*o_next_group == 0)
        maybe_write_state (model, NULL, src);
    int                 i = *o_next_group;
    switch (call_mode) {
    case UseBlackBox:
        GBgetTransitionsAll (model, (int *)src, vector_next_dfs, &src_idx);
        i = K;
        break;
    case UseGreyBox:
        /* try to find at least one transition */
        for (; i < K && !dfs_stack_frame_size(stack); ++i) {
            GBgetTransitionsLong (model, i, (int *)src, vector_next_dfs, &src_idx);
        }
        break;
    }
    if (i == K) {
        ++explored;
        if (explored % 1000 == 0 && RTverbosity >= 2)
            Warning (info, "explored %d visited %d trans %d", explored, visited, trans);
    }
    *o_next_group = i;
}

static void
dfs_explore_state_index (model_t model, int idx, int *o_next_group)
{
    int                 state[N];
    TreeUnfold (dbs, idx, state);
    if (*o_next_group == 0)
        maybe_write_state (model, &idx, state);
    int                 i = *o_next_group;
    switch (call_mode) {
    case UseBlackBox:
        GBgetTransitionsAll (model, state, index_next_dfs, &idx);
        i = K;
        break;
    case UseGreyBox:
        /* try to find at least one transition */
        for (; i < K && dfs_stack_frame_size (stack) == 0; ++i) {
            GBgetTransitionsLong (model, i, state, index_next_dfs, &idx);
        }
        break;
    }
    if (i == K) {
        ++explored;
        if (explored % 1000 == 0 && RTverbosity >= 2)
            Warning (info, "explored %d visited %d trans %d", explored, visited, trans);
    }    
    *o_next_group = i;
}

static void
dfs_explore (model_t model, int *src, size_t *o_depth)
{
    enum { SD_NEXT_GROUP, SD_SRC_IDX, SD__SIZE };
    isb_allocator_t     buffer;
    size_t              depth = 0;
    int                 next_group = 0;
    int                 write_idx = 0;
    int                 src_idx = 0;
    switch (state_db) {
    case DB_Vset:
        buffer = isba_create (SD__SIZE);
        domain = vdom_create_default (N);
        being_explored_set = vset_create (domain, 0, NULL);
        stack = dfs_stack_create (N);
        dfs_stack_push (stack, src);
        while ((src = dfs_stack_top (stack)) || dfs_stack_nframes (stack)) {
            if (src == NULL) {
                dfs_stack_leave (stack);
                int *sd = isba_pop_int (buffer);
                next_group = sd[SD_NEXT_GROUP];
                src_idx = sd[SD_SRC_IDX];
                continue;
            }
            if (next_group == 0) {
                if (vset_member (being_explored_set, src) ||
                    dfs_stack_nframes (stack) > max)
                    next_group = K;
                else {
                    vset_add (being_explored_set, src);
                    src_idx = write_idx++;
                }
            }

            if (next_group < K) {
                dfs_stack_enter (stack);
                dfs_explore_state_vector (model, src_idx, src, &next_group);
                isba_push_int (buffer, (int[SD__SIZE]){next_group, src_idx});
                if (dfs_stack_nframes (stack) > depth) {
                    depth = dfs_stack_nframes (stack);
                    if (RTverbosity >= 1)
                        Warning (info, "new depth reached %d. Visited %d states and %d trans",
                                 depth, visited, trans);
                }
            } else
                dfs_stack_pop (stack);
            next_group = 0;
        }
        break;

    case DB_TreeDBS:
        buffer = isba_create (1);
        /* Store folded states on the stack, at the cost of having to
           unfold them */
        stack = dfs_stack_create (1);
        dfs_open_set = bitset_create (128,128);
        dbs = TreeDBScreate (N);
        int                 idx = TreeFold (dbs, src);
        int                *fvec = &idx;
        dfs_stack_push (stack, fvec);
        bitset_set (dfs_open_set, *fvec);
        while ((fvec = dfs_stack_top (stack)) || dfs_stack_nframes (stack)) {
            if (fvec == NULL) {
                dfs_stack_leave (stack);
                next_group = *isba_pop_int (buffer);
                continue;
            }
            if (next_group == 0) {
                if (!bitset_test (dfs_open_set, *fvec) ||
                    dfs_stack_nframes (stack) > max)
                    next_group = K;
                else
                    bitset_clear (dfs_open_set, *fvec);
            }
            
            if (next_group < K) {
                dfs_stack_enter (stack);
                dfs_explore_state_index (model, *fvec, &next_group);
                isba_push_int (buffer, &next_group);
                if (dfs_stack_nframes (stack) > depth) {
                    depth = dfs_stack_nframes (stack);
                    if (RTverbosity >= 1)
                        Warning (info, "new depth reached %d. Visited %d states and %d trans",
                                 depth, visited, trans);
                }
            } else
                dfs_stack_pop (stack);
            next_group = 0;
        }
        break;

    default:
        Fatal (1, error, "Unsupported combination: strategy=%s, state=%s",
               strategies[strategy].key, db_types[state_db].key);
    }
    *o_depth = depth;
}

/* Main */
static void
init_write_lts (lts_output_t *p_output,
                const char *filename, int db_type,
                model_t model, int *src)
{
    lts_output_t output = NULL;
    switch (db_type) {
    case DB_TreeDBS:
        output = lts_output_open ((char *)filename, model, 1, 0, 1,
                                   write_state ? "vsi" : "-ii", NULL);
        if (write_state)
            lts_output_set_root_vec (output, (uint32_t *)src);
        break;
    case DB_Vset:
        output = lts_output_open ((char *)filename, model, 1, 0, 1, "viv", NULL);
        lts_output_set_root_vec (output, (uint32_t *)src);
        break;
    }
    lts_output_set_root_idx (output, 0, 0);
    output_handle = lts_output_begin (output, 0, 1, 0);
    *p_output = output;
}

int main(int argc, char *argv[]){
	char           *files[2];
        lts_output_t    output = NULL;
	RTinitPopt(&argc,&argv,options,1,2,files,NULL,"<model> [<lts>]",
		"Perform an enumerative reachability analysis of <model>\n\n"
		"Options");
	if (files[1]) {
		Warning(info,"Writing output to %s",files[1]);
		write_lts=1;
	} else {
		Warning(info,"No output, just counting the number of states");
		write_lts=0;
	}

	Warning(info,"loading model from %s",files[0]);
	model_t model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);

	GBloadFile(model,files[0],&model);

	if (matrix) {
	  GBprintDependencyMatrix(stdout,model);
	  exit (EXIT_SUCCESS);
	}
	if (RTverbosity >=2) {
	  fprintf(stderr,"Dependency Matrix:\n");
	  GBprintDependencyMatrix(stderr,model);
	}
    if (trc_output)
    {
        state_man=create_manager(65536);
        ADD_ARRAY(state_man, parent_ofs, uint32_t);
    }
	lts_type_t ltstype=GBgetLTStype(model);
	N=lts_type_get_state_length(ltstype);
	K= dm_nrows(GBgetDMInfo(model));
	Warning(info,"length is %d, there are %d groups",N,K);
	state_labels=lts_type_get_state_label_count(ltstype);
	edge_labels=lts_type_get_edge_label_count(ltstype);
	Warning(info,"There are %d state labels and %d edge labels",state_labels,edge_labels);
	if (state_labels&&write_lts&&!write_state) {
		Fatal(1,error,"Writing state labels without state vectors is unsupported. "
                      "Writing of state vector is enabled with option --write-state");
	}
	int src[N];
	GBgetInitialState(model,src);
	Warning(info,"got initial state");

        if (write_lts) init_write_lts (&output, files[1], state_db, model, src);

        size_t level = 0;
	switch (strategy) {
        case Strat_BFS:
            switch (state_db) {
            case DB_Vset:
		if (trc_output) Fatal(1, error, "--trace not supported for vset, use tree");
		domain=vdom_create_default(N);
		visited_set=vset_create(domain,0,NULL);
		next_set=vset_create(domain,0,NULL);
		vset_add(visited_set,src);
		vset_add(next_set,src);
		vset_t current_set=vset_create(domain,0,NULL);
		while (!vset_is_empty(next_set)){
		  if (RTverbosity >= 1)
		    Warning(info,"level %d has %d states, explored %d states %d trans",
			    level,(visited-explored),explored,trans);
		  if (level == max) break;
		  level++;
		  vset_copy(current_set,next_set);
		  vset_clear(next_set);
		  vset_enum(current_set,bfs_explore_state_vector,model);
		}
		long long size;
		long nodes;
		vset_count(visited_set,&nodes,&size);
	    	Warning(info,"%lld reachable states represented symbolically with %ld nodes",size,nodes);
		break;
            case DB_TreeDBS:
		dbs=TreeDBScreate(N);
		if(TreeFold(dbs,src)!=0){
			Fatal(1,error,"expected 0");
		}
		int limit=explored;
		while(explored<visited){
		  if (limit==explored){
		    if (RTverbosity >= 1)
		      Warning(info,"level %d has %d states, explored %d states %d trans",
			      level,(visited-explored),explored,trans);
		    limit=visited;
		    level++;
			if (level == max) break;
		  }
		  TreeUnfold(dbs,explored,src);
		  bfs_explore_state_index(model,explored,src,level);
		}
		break;
            default:
                Fatal (1, error, "Unsupported combination: strategy=%s, state=%s",
                       strategies[strategy].key, db_types[state_db].key);
            }
            Warning(info,"state space has %zu levels %d states %d transitions",
                    level,visited,trans);
            break;
        case Strat_DFS: {
            size_t depth = 0;
            dfs_explore (model, src, &depth);
            Warning (info, "state space has depth %zu, %d states %d transitions",
                    depth, visited, trans);
            break;
        }
	}

	if (write_lts){
		lts_output_end(output,output_handle);
		Warning(info,"finishing the writing");
		lts_output_close(&output);
	}
	return 0;
}
