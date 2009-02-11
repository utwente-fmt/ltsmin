#include "config.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <mpi.h>
#include <stdlib.h>

#include "fast_hash.h"
#if defined(MCRL)
#include "mcrl-greybox.h"
#define MODEL_TYPE "lpo"
#elif defined(MCRL2)
#include "mcrl2-greybox.h"
#define MODEL_TYPE "lps"
#elif defined(NIPS)
#include "nips-greybox.h"
#define MODEL_TYPE "nips"
#else
#error "Unknown greybox provider."
#endif
#include "treedbs.h"
#include "stream.h"
#include "options.h"
#include "runtime.h"
#include "archive.h"
#include "mpi_io_stream.h"
#include "mpi_ram_raf.h"
#include "stringindex.h"
#include "dynamic-array.h"
#include "mpi-event-loop.h"

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

static archive_t arch;
static int verbosity=1;
static char *outputarch=NULL;
static int write_lts=1;
static int nice_value=0;
static int plain=0;
static int find_dlk=0;
static treedbs_t dbs;
static int cache=0;
static int unix_io=0;
static int mpi_io=0;

static event_queue_t mpi_queue;
static event_barrier_t barrier;


struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: mpirun <nodespec> " MODEL_TYPE "2lts-mpi [options] <model>",NULL,NULL,NULL},
	{"-v",OPT_NORMAL,inc_int,&verbosity,NULL,"increase the level of verbosity",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,NULL,"be silent",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
/* Deadlocks can be found, but traces cannot be printed yet.
	{"-dlk",OPT_NORMAL,set_int,&find_dlk,NULL,
		"If a deadlock is found, a trace to the deadlock will be",
		"printed and the exploration will be aborted.",
		"using this option implies -nolts",NULL},
*/
	{"-mpi-io",OPT_NORMAL,set_int,&mpi_io,NULL,
		"use MPI-IO (default)",NULL,NULL,NULL},
	{"-unix-io",OPT_NORMAL,set_int,&unix_io,NULL,
		"use UNIX IO (e.g. if your NFS locking is broken)",NULL,NULL,NULL},
	{"-out",OPT_REQ_ARG,assign_string,&outputarch,"-out <archive>",
		"Specify the name of the output archive.",
		"This will be a pattern archive if <archive> contains %s",
		"and a GCF archive otherwise",NULL},
	{"-cache",OPT_NORMAL,set_int,&cache,NULL,
		"Add the caching wrapper around the model",NULL,NULL,NULL},
	{"-nolts",OPT_NORMAL,reset_int,&write_lts,NULL,
		"disable writing of the LTS",NULL,NULL,NULL},
	{"-nice",OPT_REQ_ARG,parse_int,&nice_value,"-nice <val>",
		"all workers will set nice to <val>",
		"useful when running on other people's workstations",NULL,NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",NULL,NULL,NULL},
	{"-version",OPT_NORMAL,print_version,NULL,NULL,"print the version",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static char who[24];
static int mpi_nodes,mpi_me;

static stream_t *output_src=NULL;
static stream_t *output_label=NULL;
static stream_t *output_dest=NULL;

static int *tcount;
static int size;

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


static char name[100];

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
		DSwriteU32(output_src[work_recv->src_worker],work_recv->src_number);
		DSwriteU32(output_label[work_recv->src_worker],work_recv->label);
		DSwriteU32(output_dest[work_recv->src_worker],temp);
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

static int armed=0;

static void abort_if_armed(){
	if(armed) {
		fprintf(stderr,"bad exit, aborting\n");
		MPI_Abort(MPI_COMM_WORLD,1);
	} else {
	//	fprintf(stderr,"exit verified\n");
	}
}

int main(int argc, char*argv[]){
	long long int global_visited,global_explored,global_transitions;
	void *bottom=(void*)&argc;

	if (atexit(abort_if_armed)){
		Fatal(1,error,"atexit failed");
	}
        MPI_Init(&argc, &argv);
	armed=1;
	MPI_Errhandler_set(MPI_COMM_WORLD,MPI_ERRORS_ARE_FATAL);
        MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
	sprintf(who,MODEL_TYPE "2lts-mpi(%2d)",mpi_me);
	RTinit(argc,&argv);
	set_label(who);
	mpi_queue=event_queue();
	state_found_init();
	work_counter=event_idle_create(mpi_queue,MPI_COMM_WORLD,EXPLORE_IDLE_TAG);
	barrier=event_barrier_create(mpi_queue,MPI_COMM_WORLD,BARRIER_TAG);

	tcount=(int*)malloc(mpi_nodes*sizeof(int));

	if (mpi_me!=0) MPI_Barrier(MPI_COMM_WORLD);
	parse_options(options,argc,argv);
	switch(mpi_io+unix_io){
	case 0:
		mpi_io=1;
	case 1:
		if(mpi_me==0 && mpi_io) Warning(info,"using MPI-IO");
		if(mpi_me==0 && unix_io) Warning(info,"using UNIX IO");
		break;
	default:
		Fatal(1,error,"IO selections -mpi-io and -unix-io are mutually exclusive");
	}
	if (mpi_me==0) MPI_Barrier(MPI_COMM_WORLD);
	Warning(info,"initializing grey box module");
#if defined(MCRL)
	MCRLinitGreybox(argc,argv,bottom);
#elif defined(MCRL2)
	MCRL2initGreybox(argc,argv,bottom);
#elif defined(NIPS)
        (void)bottom;
	NIPSinitGreybox(argc,argv);
#endif
	Warning(info,"creating model for %s",argv[argc-1]);
	model_t model=GBcreateBase();
	GBsetChunkMethods(model,mpi_newmap,mpi_index_pool_create(MPI_COMM_WORLD,mpi_queue,MAX_TERM_LEN),
		 mpi_int2chunk,mpi_chunk2int,mpi_get_count);
#if defined(MCRL)
	MCRLloadGreyboxModel(model,argv[argc-1]);
#elif defined(MCRL2)
	MCRL2loadGreyboxModel(model,argv[argc-1]);
#elif defined(NIPS)
	NIPSloadGreyboxModel(model,argv[argc-1]);
#endif
	if (cache) model=GBaddCache(model);
	event_barrier_wait(barrier);
	Warning(info,"model created");
	lts_struct_t ltstype=GBgetLTStype(model);

	/* Initializing according to the options just parsed.
	 */
	if (nice_value) {
		if (mpi_me==0) Warning(info,"setting nice to %d\n",nice_value);
		nice(nice_value);
	}
	/***************************************************/
	if (find_dlk) {
		write_lts=0;
		state_man=create_manager(65536);
		ADD_ARRAY(state_man,parent_ofs,uint32_t);
		ADD_ARRAY(state_man,parent_seg,uint16_t);
	}
	/***************************************************/
	if (mpi_me==0){
		if (write_lts && !outputarch) Fatal(1,error,"please specify the output archive with -out");
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (write_lts){
		if (strstr(outputarch,"%s")) {
			if (mpi_io) arch=arch_fmt(outputarch,mpi_io_read,mpi_io_write,prop_get_U32("bs",65536));
			if (unix_io) arch=arch_fmt(outputarch,file_input,file_output,prop_get_U32("bs",65536));
		} else {
			uint32_t bs=prop_get_U32("bs",65536);
			uint32_t bc=prop_get_U32("bc",128);
			if (mpi_io) arch=arch_gcf_create(MPI_Create_raf(outputarch,MPI_COMM_WORLD),bs,bs*bc,mpi_me,mpi_nodes);
			if (unix_io) arch=arch_gcf_create(raf_unistd(outputarch),bs,bs*bc,mpi_me,mpi_nodes);
		}
	}
	/***************************************************/
	if (write_lts) {
		output_src=(stream_t*)malloc(mpi_nodes*sizeof(FILE*));
		output_label=(stream_t*)malloc(mpi_nodes*sizeof(FILE*));
		output_dest=(stream_t*)malloc(mpi_nodes*sizeof(FILE*));
		for(int i=0;i<mpi_nodes;i++){
			sprintf(name,"src-%d-%d",i,mpi_me);
			output_src[i]=arch_write(arch,name,plain?NULL:"diff32|gzip",1);
			sprintf(name,"label-%d-%d",i,mpi_me);
			output_label[i]=arch_write(arch,name,plain?NULL:"gzip",1);
			sprintf(name,"dest-%d-%d",i,mpi_me);
			output_dest[i]=arch_write(arch,name,plain?NULL:"diff32|gzip",1);
			tcount[i]=0;
		}
	}
	/***************************************************/
	size=ltstype->state_length;
	if (size<2) Fatal(1,error,"there must be at least 2 parameters");
	if (size>MAX_PARAMETERS) Fatal(1,error,"please make src and dest dynamic");
	dbs=TreeDBScreate(size);
	int src[size];
	io_trans_init();
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
	int level=0;
	for(;;){
		long long int limit=visited;
		level++;
		int lvl_scount=0;
		int lvl_tcount=0;
		if (mpi_me==0 || verbosity>1) Warning(info,"exploring level %d",level);
		event_barrier_wait(barrier);
		while(explored<limit){
			TreeUnfold(dbs,explored,src);
			struct src_info ctx;
			ctx.seg=mpi_me;
			ctx.ofs=explored;
			explored++;
			int count=GBgetTransitionsAll(model,src,callback,&ctx);;
			if (count<0) Fatal(1,error,"error in GBgetTransitionsAll");
			if (count==0 && find_dlk){
				Warning(info,"deadlock found: %d.%d",ctx.seg,ctx.ofs);
				deadlock_found(ctx.seg,ctx.ofs);
			}
			lvl_scount++;
			lvl_tcount+=count;
			if ((lvl_scount%1000)==0) {
				Warning(info,"generated %d transitions from %d states",
					lvl_tcount,lvl_scount);
			}
			event_yield(mpi_queue);
		}
		if (verbosity>1) Warning(info,"explored %d states and %d transitions",lvl_scount,lvl_tcount);
		event_idle_detect(work_counter);
		MPI_Allreduce(&visited,&global_visited,1,MPI_LONG_LONG,MPI_SUM,MPI_COMM_WORLD);
		MPI_Allreduce(&explored,&global_explored,1,MPI_LONG_LONG,MPI_SUM,MPI_COMM_WORLD);
		MPI_Allreduce(&transitions,&global_transitions,1,MPI_LONG_LONG,MPI_SUM,MPI_COMM_WORLD);
		if (verbosity>1) event_statistics(mpi_queue);
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
	if (verbosity>1) Warning(info,"My share is %lld states and %lld transitions",explored,transitions);
	if (write_lts){
		for(int i=0;i<mpi_nodes;i++){
			DSclose(&output_src[i]);
			DSclose(&output_label[i]);
			DSclose(&output_dest[i]);
		}
	}
	if (verbosity>1) Warning(info,"transition files closed");
	{
	int *temp=NULL;
	stream_t info_s=NULL;
		if (write_lts && mpi_me==0){
			stream_t ds=arch_write(arch,"TermDB",plain?NULL:"gzip",1);
			int act_count=GBchunkCount(model,ltstype->edge_label_type[0]);
			for(int i=0;i<act_count;i++){
				chunk c=GBchunkGet(model,ltstype->edge_label_type[0],i);
				DSwrite(ds,c.data,c.len);
				DSwrite(ds,"\n",1);
			}
			DSclose(&ds);
			Warning(info,"%d actions",act_count);
			/* Start writing the info file. */
			info_s=arch_write(arch,"info",plain?NULL:"",1);
			DSwriteU32(info_s,31);
			DSwriteS(info_s,"generated by instantiator-mpi");
			DSwriteU32(info_s,mpi_nodes);
			DSwriteU32(info_s,0);
			DSwriteU32(info_s,0);
			DSwriteU32(info_s,act_count);
			DSwriteU32(info_s,-1); //tau
			DSwriteU32(info_s,size-1);
		}
		if (mpi_me==0) temp=(int*)malloc(mpi_nodes*mpi_nodes*sizeof(int));
		MPI_Gather(&explored,1,MPI_INT,temp,1,MPI_INT,0,MPI_COMM_WORLD);
		if (write_lts && mpi_me==0){
			for(int i=0;i<mpi_nodes;i++){
				DSwriteU32(info_s,temp[i]);
			}
		}
		MPI_Gather(tcount,mpi_nodes,MPI_INT,temp,mpi_nodes,MPI_INT,0,MPI_COMM_WORLD);
		if (write_lts && mpi_me==0){
			for(int i=0;i<mpi_nodes;i++){
				for(int j=0;j<mpi_nodes;j++){
					//Warning(info,"%d -> %d : %d",i,j,temp[i+mpi_nodes*j]);
					DSwriteU32(info_s,temp[i+mpi_nodes*j]);
				}
			}
			DSclose(&info_s);
		}
	}
	if (write_lts) arch_close(&arch);
	//char dir[16];
	//sprintf(dir,"gmon-%d",mpi_me);
	//chdir(dir);
	MPI_Finalize();
	armed=0;
	return 0;
}


