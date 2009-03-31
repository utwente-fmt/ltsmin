#include "config.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <mpi.h>
#include <stdlib.h>

#include <lts_enum.h>
#include <lts_io.h>

#include "fast_hash.h"
#include "treedbs.h"
#include "stream.h"
#include <mpi-runtime.h>
#include "archive.h"
#include "mpi_io_stream.h"
#include "mpi_ram_raf.h"
#include "stringindex.h"
#include "dynamic-array.h"
#include "mpi-event-loop.h"

static lts_enum_cb_t output_handle=NULL;
static lts_output_t output=NULL;

/********************************************************************************************/

typedef struct mpi_index_pool {
	MPI_Comm comm;
	event_queue_t queue;
	int me;
	int nodes;
	int next_tag;
	int max_chunk;
} *mpi_index_pool_t;

typedef struct mpi_index {
	mpi_index_pool_t pool;
	int tag;
	int owner;
	string_index_t index;
	char *recv_buf;
	void *wanted;
	int w_len;
	int looking;
} *mpi_index_t;

static void mpi_index_handler(void* context,MPI_Status *status){
	mpi_index_t index=(mpi_index_t)context;
	int len;
	MPI_Get_count(status,MPI_CHAR,&len);
	if (index->pool->me==index->owner) {
		int res=SIlookupC(index->index,index->recv_buf,len);
		if (res==SI_INDEX_FAILED) {
			res=SIputC(index->index,index->recv_buf,len);
			//Warning(info,"added label %d, broadcasting",res);
			int todo=index->pool->nodes-1;
			for(int i=0;i<index->pool->nodes;i++) if (i!=index->owner) {
				event_Isend(index->pool->queue,index->recv_buf,len,MPI_CHAR,
					i,index->tag,index->pool->comm,event_decr,&todo);
			}
			event_while(index->pool->queue,&todo);
			//Warning(info,"broadcast complete");
		}
	} else {
		int res=SIputC(index->index,index->recv_buf,len);
		if (index->wanted && !index->looking) Fatal(1,error,"wanted and not looking");
		if (index->wanted) {
			//Warning(info,"added %d waiting for wanted",res);
			if (len==index->w_len && memcmp(index->recv_buf,index->wanted,len)==0){
				index->wanted=NULL;
				index->looking=0;
			}
		} else {
			//Warning(info,"added %d waiting for %d",res,index->looking-1);
			if (index->looking==res+1){
				index->looking=0;
			}
		}
	}
	event_Irecv(index->pool->queue,index->recv_buf,index->pool->max_chunk,MPI_CHAR,
		MPI_ANY_SOURCE,index->tag,index->pool->comm,mpi_index_handler,context);
}

mpi_index_pool_t mpi_index_pool_create(MPI_Comm comm,event_queue_t queue,int max_chunk){
	mpi_index_pool_t pool=(mpi_index_pool_t)RTmalloc(sizeof(struct mpi_index_pool));
	MPI_Comm_dup(comm,&pool->comm);
	pool->queue=queue;
	MPI_Comm_size(pool->comm,&pool->nodes);
        MPI_Comm_rank(pool->comm,&pool->me);
	pool->next_tag=1;
	pool->max_chunk=max_chunk;
	return pool;
}

void* mpi_newmap(void*newmap_context){
	mpi_index_pool_t pool=(mpi_index_pool_t)newmap_context;
	mpi_index_t index=(mpi_index_t)RTmalloc(sizeof(struct mpi_index));
	index->pool=pool;
	index->tag=pool->next_tag;
	pool->next_tag++;
	index->owner=(index->tag)%(pool->nodes);
	index->index=SIcreate();
	index->wanted=NULL;
	index->looking=0;
	index->recv_buf=RTmalloc(pool->max_chunk);
	event_Irecv(index->pool->queue,index->recv_buf,index->pool->max_chunk,MPI_CHAR,
		MPI_ANY_SOURCE,index->tag,index->pool->comm,mpi_index_handler,index);
	Warning(info,"%d: new index tag %d owned by %d",index->pool->me,index->tag,index->owner);
	return index;
}

int mpi_chunk2int(void*map,void*chunk,int len){
	mpi_index_t index=(mpi_index_t)map;
	int res=SIlookupC(index->index,chunk,len);
	if (res==SI_INDEX_FAILED) {
		//Warning(info,"looking up chunk");
		if (index->pool->me==index->owner) {
			res=SIputC(index->index,chunk,len);
			int todo=index->pool->nodes-1;
			for(int i=0;i<index->pool->nodes;i++) if (i!=index->owner) {
				event_Isend(index->pool->queue,chunk,len,MPI_CHAR,
					i,index->tag,index->pool->comm,event_decr,&todo);
			}
			event_while(index->pool->queue,&todo);
		} else {
			index->wanted=chunk;
			index->w_len=len;
			index->looking=1;
			event_Isend(index->pool->queue,chunk,len,MPI_CHAR,
				index->owner,index->tag,index->pool->comm,NULL,NULL);
			event_while(index->pool->queue,&index->looking);
			res=SIlookupC(index->index,chunk,len);
		}
		//Warning(info,"got %d",res);
	}
	return res;
}

void* mpi_int2chunk(void*map,int idx,int*len){
	mpi_index_t index=(mpi_index_t)map;
	if (index->pool->me!=index->owner && SIgetCount(index->index)<=idx){
		//Warning(info,"looking up %d",idx);
		index->looking=idx+1;
		event_while(index->pool->queue,&index->looking);
		//Warning(info,"got it");
	}
	return SIgetC(index->index,idx,len);
}

int mpi_get_count(void*map){
	mpi_index_t index=(mpi_index_t)map;
	return SIgetCount(index->index);
}

/********************************************************************************************/

#define MAX_PARAMETERS 256
#define MAX_TERM_LEN 5000

static int write_lts;
static int nice_value=0;
static int find_dlk=0;
static treedbs_t dbs;

static event_queue_t mpi_queue;
static event_barrier_t barrier;

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

static int write_state=0;

static  struct poptOption options[] = {
	{ "nice" , 0 , POPT_ARG_INT , &nice_value , 0 , "set the nice level of all workers"
		" (useful when running on other peoples workstations)" , NULL},
	{ "write-state" , 0 , POPT_ARG_VAL , &write_state, 1 , "write the full state vector" , NULL },
#if defined(MCRL)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl_options , 0 , "mCRL options", NULL},
#endif
#if defined(MCRL2)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl2_options , 0 , "mCRL2 options", NULL},
#endif
#if defined(NIPS)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, nips_options , 0 , "NIPS options", NULL},
#endif
#if defined(ETF)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, etf_options , 0 , "ETF options", NULL},
#endif
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL, NULL},
	POPT_TABLEEND
};

/*
Deadlocks can be found, but traces cannot be printed yet.
	{"-dlk",OPT_NORMAL,set_int,&find_dlk,NULL,
		"If a deadlock is found, a trace to the deadlock will be",
		"printed and the exploration will be aborted.",
		"using this option implies -nolts",NULL},

	{"-mpi-io",OPT_NORMAL,set_int,&mpi_io,NULL,
		"use MPI-IO (default)",NULL,NULL,NULL},
	{"-nice",OPT_REQ_ARG,parse_int,&nice_value,"-nice <val>",
		"all workers will set nice to <val>",
		"useful when running on other people's workstations",NULL,NULL},

*/

static char who[24];
static int mpi_nodes,mpi_me;

static int *tcount;
static int size;
static int state_labels;

static uint32_t chk_base=0;

static inline void adjust_owner(int32_t *state){
	chk_base=SuperFastHash((char*)state,size*4,0);
}

static inline int owner(int32_t *state){
	uint32_t hash=chk_base^SuperFastHash((char*)state,size*4,0);
	return (hash%mpi_nodes);
}

#define POOL_SIZE 16

struct work_msg {
	int src_worker;
	int src_number;
	int label;
	int dest[MAX_PARAMETERS];
} work_send_buf[POOL_SIZE],work_recv_buf[POOL_SIZE];
static int work_send_used[POOL_SIZE];
static int work_send_next=0;

#define BARRIER_TAG 12
#define EXPLORE_IDLE_TAG 11

#define STATE_FOUND_TAG 10

#define LEAF_CHUNK_TAG 9
#define LEAF_INT_TAG 8

#define ACT_CHUNK_TAG 5
#define ACT_INT_TAG 4

#define WORK_TAG 3
#define WORK_SIZE (sizeof(struct work_msg)-(MAX_PARAMETERS-size)*sizeof(int))
//define WORK_SIZE sizeof(struct work_msg)

static idle_detect_t work_counter;

static struct state_found_msg {
	int reason; // -1: deadlock n>=0: action n is enabled and matches a wanted action.
	int segment;
	int offset;
} state_found_buf;

static int deadlock_count=0;

static void state_found_handler(void* context,MPI_Status *status){
	(void)context;(void)status;
	deadlock_count++;
	if (state_found_buf.reason==-1) {
		Warning(info,"deadlock %d found: segment %d, offset %d",
			deadlock_count,state_found_buf.segment,state_found_buf.offset);
	}
	event_idle_recv(work_counter);
	event_Irecv(mpi_queue,&state_found_buf,sizeof(struct state_found_msg),MPI_CHAR,
		MPI_ANY_SOURCE,STATE_FOUND_TAG,MPI_COMM_WORLD,state_found_handler,NULL);
}

static void state_found_init(){
	event_Irecv(mpi_queue,&state_found_buf,sizeof(struct state_found_msg),MPI_CHAR,
		MPI_ANY_SOURCE,STATE_FOUND_TAG,MPI_COMM_WORLD,state_found_handler,NULL);
}

static void deadlock_found(int segment,int offset){
	struct state_found_msg msg;
	msg.reason=-1;
	msg.segment=segment;
	msg.offset=offset;
	event_Send(mpi_queue,&msg,sizeof(struct state_found_msg),MPI_CHAR,
		0,STATE_FOUND_TAG,MPI_COMM_WORLD);
	event_idle_send(work_counter);
}

/********************************************************/

struct src_info {
	int seg;
	int ofs;
};

static void callback(void*context,int*labels,int*dst){
	(void)context;
	int i,who;

	//struct timeval tv1,tv2;
	//gettimeofday(&tv1,NULL);
	event_while(mpi_queue,&work_send_used[work_send_next]);
	work_send_used[work_send_next]=1;
	work_send_buf[work_send_next].src_worker=((struct src_info*)context)->seg;
	work_send_buf[work_send_next].src_number=((struct src_info*)context)->ofs;
	work_send_buf[work_send_next].label=labels[0];
	for(i=0;i<size;i++){
		work_send_buf[work_send_next].dest[i]=dst[i];
	}
	who=owner(work_send_buf[work_send_next].dest);
	event_Isend(mpi_queue,&work_send_buf[work_send_next],WORK_SIZE,MPI_CHAR,who,
			WORK_TAG,MPI_COMM_WORLD,event_decr,&work_send_used[work_send_next]);
	event_idle_send(work_counter);
	work_send_next=(work_send_next+1)%POOL_SIZE;
	//gettimeofday(&tv2,NULL);
	//long long int usec=(tv2.tv_sec-tv1.tv_sec)*1000000LL + tv2.tv_usec - tv1.tv_usec;
	//static long long int max=0;
	//if (usec>max) {
	//	Warning(info,"callback took %lld us",usec);
	//	max=usec;
	//}
}


static array_manager_t state_man=NULL;
static uint32_t *parent_ofs=NULL;
static uint16_t *parent_seg=NULL;
static long long int explored,visited,transitions;

static void in_trans_handler(void*context,MPI_Status *status){
#define work_recv ((struct work_msg*)context)
	(void)status;
	event_idle_recv(work_counter);
	int who=owner(work_recv->dest);
	if (who != mpi_me) {
		Fatal(1,error,"state does not belong to me");
	}
	int temp=TreeFold(dbs,work_recv->dest);
	if (temp>=visited) {
		visited=temp+1;
		if(find_dlk){
			ensure_access(state_man,temp);
			parent_seg[temp]=work_recv->src_worker;
			parent_ofs[temp]=work_recv->src_number;
		}
	}
	if (write_lts){
		//DSwriteU32(output_src[work_recv->src_worker],work_recv->src_number);
		//DSwriteU32(output_label[work_recv->src_worker],work_recv->label);
		//DSwriteU32(output_dest[work_recv->src_worker],temp);
		enum_seg_seg(output_handle,work_recv->src_worker,work_recv->src_number,mpi_me,temp,&(work_recv->label));		
	}
	tcount[work_recv->src_worker]++;
	transitions++;
	//if (transitions%1000==0) {
	//	Warning(info,"%lld transitions %lld explored",transitions,explored);
	//}
	event_Irecv(mpi_queue,work_recv,WORK_SIZE,MPI_CHAR,MPI_ANY_SOURCE,
			WORK_TAG,MPI_COMM_WORLD,in_trans_handler,work_recv);
#undef work_recv
}

static void io_trans_init(){
	for(int i=0;i<POOL_SIZE;i++){
		event_Irecv(mpi_queue,&work_recv_buf[i],WORK_SIZE,MPI_CHAR,MPI_ANY_SOURCE,
			WORK_TAG,MPI_COMM_WORLD,in_trans_handler,&work_recv_buf[i]);
		work_send_used[i]=0;
	}
}

int main(int argc, char*argv[]){
	long long int global_visited,global_explored,global_transitions;
	char *files[2];
#ifdef OMPI_MPI_H
	char* mpirun="mpirun --mca btl <transport>,self [MPI OPTIONS] -np <workers>"; 
#else
	char* mpirun="mpirun [MPI OPTIONS] -np <workers>";
#endif
	RTinitPoptMPI(&argc, &argv, options,1,2,files,mpirun,"<model> [<lts>]",
		"Perform a distributed enumerative reachability analysis of <model>\n\nOptions");
        MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
	sprintf(who,"%s(%2d)",get_label(),mpi_me);
	set_label(who);
	mpi_queue=event_queue();
	state_found_init();
	work_counter=event_idle_create(mpi_queue,MPI_COMM_WORLD,EXPLORE_IDLE_TAG);
	barrier=event_barrier_create(mpi_queue,MPI_COMM_WORLD,BARRIER_TAG);

	tcount=(int*)RTmalloc(mpi_nodes*sizeof(int));
	bzero(tcount,mpi_nodes*sizeof(int));

	model_t model=GBcreateBase();
	GBsetChunkMethods(model,mpi_newmap,mpi_index_pool_create(MPI_COMM_WORLD,mpi_queue,MAX_TERM_LEN),
		 mpi_int2chunk,mpi_chunk2int,mpi_get_count);

	GBloadFile(model,files[0],&model);

	event_barrier_wait(barrier);
	Warning(info,"model created");
	lts_type_t ltstype=GBgetLTStype(model);

	/* Initializing according to the options just parsed.
	 */
	if (nice_value) {
		if (mpi_me==0) Warning(info,"setting nice to %d\n",nice_value);
		nice(nice_value);
	}
	/***************************************************/
	if (find_dlk) {
		state_man=create_manager(65536);
		ADD_ARRAY(state_man,parent_ofs,uint32_t);
		ADD_ARRAY(state_man,parent_seg,uint16_t);
	}
	event_barrier_wait(barrier);
	/***************************************************/
	size=lts_type_get_state_length(ltstype);
	if (size<2) Fatal(1,error,"there must be at least 2 parameters");
	if (size>MAX_PARAMETERS) Fatal(1,error,"please make src and dest dynamic");
	dbs=TreeDBScreate(size);
	int src[size];
	io_trans_init();
	state_labels=lts_type_get_state_label_count(ltstype);
	Warning(info,"there are %d state labels",state_labels);
	if (state_labels&&files[1]&&!write_state) {
		Fatal(1,error,"Writing state labels, but not state vectors unsupported. "
			"Writing of state vector is enabled with the option --write-state");
	}
	int labels[state_labels];
	/***************************************************/
	event_barrier_wait(barrier);
	/***************************************************/
	GBgetInitialState(model,src);
	Warning(info,"initial state computed at %d",mpi_me);
	adjust_owner(src);
	Warning(info,"initial state translated at %d",mpi_me);
	explored=0;
	transitions=0;
	if(mpi_me==0){
		Warning(info,"folding initial state at %d",mpi_me);
		if (TreeFold(dbs,src)) Fatal(1,error,"Initial state wasn't assigned state no 0");
		visited=1;
	} else {
		visited=0;
	}
	/***************************************************/
	event_barrier_wait(barrier);
	if (files[1]) {
		Warning(info,"Writing output to %s",files[1]);
		write_lts=1;
		if (write_state) {
			output=lts_output_open(files[1],model,mpi_nodes,mpi_me,mpi_nodes,"vsi",NULL);
		} else {
			output=lts_output_open(files[1],model,mpi_nodes,mpi_me,mpi_nodes,"-ii",NULL);
		}
		lts_output_set_root_vec(output,(uint32_t*)src);
		lts_output_set_root_idx(output,0,0);
		event_barrier_wait(barrier); // opening is sometimes a collaborative operation. (e.g. *.dir)
		output_handle=lts_output_begin(output,mpi_me,mpi_nodes,mpi_me);
		// write states belonging to me and edge from any source to me.
	} else {
		Warning(info,"No output, just counting the number of states");
		write_lts=0;
	}
	event_barrier_wait(barrier);
	/***************************************************/
	int level=0;
	for(;;){
		long long int limit=visited;
		level++;
		int lvl_scount=0;
		int lvl_tcount=0;
		if (mpi_me==0 || RTverbosity>1) Warning(info,"exploring level %d",level);
		event_barrier_wait(barrier);
		while(explored<limit){
			TreeUnfold(dbs,explored,src);
			struct src_info ctx;
			ctx.seg=mpi_me;
			ctx.ofs=explored;
			explored++;
			int count=GBgetTransitionsAll(model,src,callback,&ctx);
			if (count<0) Fatal(1,error,"error in GBgetTransitionsAll");
			if (count==0 && find_dlk){
				Warning(info,"deadlock found: %d.%d",ctx.seg,ctx.ofs);
				deadlock_found(ctx.seg,ctx.ofs);
			}
			if (state_labels){
				GBgetStateLabelsAll(model,src,labels);
			}
			if(write_lts && write_state){
				enum_vec(output_handle,src,labels);
			}

			lvl_scount++;
			lvl_tcount+=count;
			if ((lvl_scount%1000)==0) {
				Warning(info,"generated %d transitions from %d states",
					lvl_tcount,lvl_scount);
			}
			event_yield(mpi_queue);
		}
		if (RTverbosity>1) Warning(info,"explored %d states and %d transitions",lvl_scount,lvl_tcount);
		event_idle_detect(work_counter);
		MPI_Allreduce(&visited,&global_visited,1,MPI_LONG_LONG,MPI_SUM,MPI_COMM_WORLD);
		MPI_Allreduce(&explored,&global_explored,1,MPI_LONG_LONG,MPI_SUM,MPI_COMM_WORLD);
		MPI_Allreduce(&transitions,&global_transitions,1,MPI_LONG_LONG,MPI_SUM,MPI_COMM_WORLD);
		if (RTverbosity>1) event_statistics(mpi_queue);
		if (global_visited==global_explored) break;
		if (mpi_me==0) {
			Warning(info,"level %d: %lld explored %lld transitions %lld visited",
				level,global_explored,global_transitions,global_visited);
		}
	}
	if (mpi_me==0) {
		if (write_lts) {
			Warning(info,"State space has %d levels %lld states %lld transitions",
				level,global_explored,global_transitions);
		} else {
			printf("State space has %d levels %lld states %lld transitions\n",
				level,global_explored,global_transitions);
		}
	}
	event_barrier_wait(barrier);
	/* State space was succesfully generated. */
	if (RTverbosity>1) Warning(info,"My share is %lld states and %lld transitions",explored,transitions);
	event_barrier_wait(barrier);
	if(write_lts){
		if (mpi_me==0 && RTverbosity>1) Warning(info,"collecting LTS info");
		int temp[mpi_nodes*mpi_nodes];
		MPI_Gather(&explored,1,MPI_INT,temp,1,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			lts_count_t *count=lts_output_count(output);
			for(int i=0;i<mpi_nodes;i++){
				if (RTverbosity>1) Warning(info,"state count of %d is %d",i,temp[i]);
				count->state[i]=temp[i];
			}
		}
		//for(int i=0;i<mpi_nodes;i++){
		//	Warning(info,"in from %d is %d",i,tcount[i]);
		//}
		MPI_Gather(tcount,mpi_nodes,MPI_INT,temp,mpi_nodes,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			lts_count_t *count=lts_output_count(output);
			for(int i=0;i<mpi_nodes;i++){
				for(int j=0;j<mpi_nodes;j++){
					if (RTverbosity>1) Warning(info,"transition count %d to %d is %d",i,j,temp[i+mpi_nodes*j]);
					count->cross[i][j]=temp[i+mpi_nodes*j];
				}
			}
			for(int i=0;i<mpi_nodes;i++){
				count->in[i]=0;
				count->out[i]=0;
				for(int j=0;j<mpi_nodes;j++){
					count->in[i]+=count->cross[j][i];
					count->out[i]+=count->cross[i][j];
				}
			}
		}
		lts_output_end(output,output_handle);
		lts_output_close(&output);
	}
	//char dir[16];
	//sprintf(dir,"gmon-%d",mpi_me);
	//chdir(dir);
	event_barrier_wait(barrier);
	RTfiniMPI();
	MPI_Finalize();
	return 0;
}


