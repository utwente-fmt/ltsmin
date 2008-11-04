#include "config.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <mpi.h>

#include "generichash.h"
#include "chunk-table.h"
#include "mcrl-greybox.h"
#include "treedbs.h"
#include "stream.h"
#include "options.h"
#include "runtime.h"
#include "archive.h"
#include "sysdep.h"
#include "mpi_io_stream.h"
#include "mpi_ram_raf.h"
#include "stringindex.h"

#define MAX_PARAMETERS 256
#define MAX_TERM_LEN 5000

static archive_t arch;
static int verbosity=1;
static char *outputarch=NULL;
static int write_lts=1;
static int nice_value=0;
static int master_no_step=0;
static int no_step=0;
static int loadbalancing=1;
static int plain=0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: mpirun <nodespec> inst-mpi options file ...",NULL,NULL,NULL},
	{"-v",OPT_NORMAL,inc_int,&verbosity,NULL,"increase the level of verbosity",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,reset_int,&verbosity,NULL,"be silent",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{"-out",OPT_REQ_ARG,assign_string,&outputarch,"-out <archive>",
		"Specifiy the name of the output archive.",
		"This will be a pattern archive if <archive> contains %s",
		"and a GCF archive otherwise",NULL},
	{"-nolb",OPT_NORMAL,reset_int,&loadbalancing,NULL,
		"disable load balancing",NULL,NULL,NULL},
	{"-nolts",OPT_NORMAL,reset_int,&write_lts,NULL,
		"disable writing of the LTS",NULL,NULL,NULL},
	{"-nice",OPT_REQ_ARG,parse_int,&nice_value,"-nice <val>",
		"all workers will set nice to <val>",
		"useful when running on other people's workstations",NULL,NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",NULL,NULL,NULL},
	{"-master-no-step",OPT_NORMAL,set_int,&master_no_step,NULL,
		"instruct master to act as database only",
		"this improves latency of database lookups",NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static int chkbase;

static char who[24];

static stream_t *output_src=NULL;
static stream_t *output_label=NULL;
static stream_t *output_dest=NULL;

static int *tcount;
static int *src;
static int size;

static struct round_msg {
	int	go;
} round_msg;

static struct term_msg {
	int clean;
	int count;
	int unexplored;
} term_msg;

struct work_msg {
	int src_worker;
	int src_number;
	int label;
	int dest[MAX_PARAMETERS];
} work_msg;

static struct submit_msg {
	int segment;
	int offset;
	int state[MAX_PARAMETERS];
} submit_msg;

#define LEAF_CHUNK_TAG 9
#define LEAF_INT_TAG 8
#define SUBMIT_TAG 7
//define SUBMIT_SIZE (sizeof(struct submit_msg)-(MAX_PARAMETERS-size)*sizeof(int))
#define SUBMIT_SIZE sizeof(struct submit_msg)
#define IDLE_TAG 6
#define ACT_CHUNK_TAG 5
#define ACT_INT_TAG 4
#define WORK_TAG 3
//define WORK_SIZE (sizeof(struct work_msg)-(MAX_PARAMETERS-size)*sizeof(int))
#define WORK_SIZE sizeof(struct work_msg)
#define ROUND_TAG 2
#define ROUND_SIZE sizeof(struct round_msg)
#define TERM_TAG 1
#define TERM_SIZE sizeof(struct term_msg)

static int mpi_nodes,mpi_me;

static string_index_t act_db;
static int next_act=0;

static string_index_t leaf_db;
static int next_leaf=0;

static int chunk2int(string_index_t term_db,int chunk_tag,int int_tag,size_t len,void* chunk){
	if (mpi_me==0) {
		((char*)chunk)[len]=0;
		return SIput(term_db,chunk);
	} else {
		int idx;
		MPI_Status status;
		MPI_Sendrecv(chunk,len,MPI_CHAR,0,chunk_tag,
			&idx,1,MPI_INT,0,int_tag,MPI_COMM_WORLD,&status);
		return idx;
	}
}

static void chunk_call(string_index_t term_db,int chunk_tag,int int_tag,int next_cb,chunk_add_t cb,void* context){
	if (mpi_me==0) {
		char*s=SIget(term_db,next_cb);
		int len=strlen(s);
		cb(context,len,s);
	} else {
		MPI_Status status;
		char chunk[MAX_TERM_LEN+1];
		MPI_Sendrecv(&next_cb,1,MPI_INT,0,int_tag,
			&chunk,MAX_TERM_LEN,MPI_CHAR,0,chunk_tag,MPI_COMM_WORLD,&status);
		int len;
		MPI_Get_count(&status,MPI_CHAR,&len);
		cb(context,len,chunk);
	}
}


chunk_table_t CTcreate(char *name){
	if (!strcmp(name,"action")) {
		return (void*)1;
	}
	if (!strcmp(name,"leaf")) {
		return (void*)2;
	}
	Fatal(1,error,"CT support incomplete canniot deal with table %s",name);
	return NULL;
}

void CTsubmitChunk(chunk_table_t table,size_t len,void* chunk,chunk_add_t cb,void* context){
	if(table==(void*)1){
		int res=chunk2int(act_db,ACT_CHUNK_TAG,ACT_INT_TAG,len,chunk);
		while(next_act<=res){
			chunk_call(act_db,ACT_CHUNK_TAG,ACT_INT_TAG,next_act,cb,context);
			next_act++;
		}
	} else {
		int res=chunk2int(leaf_db,LEAF_CHUNK_TAG,LEAF_INT_TAG,len,chunk);
		while(next_leaf<=res){
			chunk_call(leaf_db,LEAF_CHUNK_TAG,LEAF_INT_TAG,next_leaf,cb,context);
			next_leaf++;
		}
	}
}

void CTupdateTable(chunk_table_t table,uint32_t wanted,chunk_add_t cb,void* context){
	if(table==(void*)1){
		while(next_act<=(int)wanted){
			chunk_call(act_db,ACT_CHUNK_TAG,ACT_INT_TAG,next_act,cb,context);
			next_act++;
		}
	} else {
		while(next_leaf<=(int)wanted){
			chunk_call(leaf_db,LEAF_CHUNK_TAG,LEAF_INT_TAG,next_leaf,cb,context);
			next_leaf++;
		}
	}
}

static void map_server_probe(int id){
	MPI_Status status;
	int ii,len,found;
	char *s,buf[MAX_TERM_LEN+1];
	int chunk_tag,int_tag;
	string_index_t term_db;
	if (id==1){
		chunk_tag=ACT_CHUNK_TAG;
		int_tag=ACT_INT_TAG;
		term_db=act_db;
	} else {
		chunk_tag=LEAF_CHUNK_TAG;
		int_tag=LEAF_INT_TAG;
		term_db=leaf_db;
	}
	for(;;) {
		MPI_Iprobe(MPI_ANY_SOURCE,chunk_tag,MPI_COMM_WORLD,&found,&status);
		if (!found) break;
		MPI_Recv(&buf,MAX_TERM_LEN,MPI_CHAR,MPI_ANY_SOURCE,chunk_tag,MPI_COMM_WORLD,&status);
		MPI_Get_count(&status,MPI_CHAR,&len);
		buf[len]=0;
		ii=SIput(term_db,buf);
		MPI_Send(&ii,1,MPI_INT,status.MPI_SOURCE,int_tag,MPI_COMM_WORLD);
	}
	for(;;) {
		MPI_Iprobe(MPI_ANY_SOURCE,int_tag,MPI_COMM_WORLD,&found,&status);
		if (!found) break;
		MPI_Recv(&ii,1,MPI_INT,MPI_ANY_SOURCE,int_tag,MPI_COMM_WORLD,&status);
		s=SIget(term_db,ii);
		len=strlen(s);
		MPI_Send(s,len,MPI_CHAR,status.MPI_SOURCE,chunk_tag,MPI_COMM_WORLD);
	}
}

static int msgcount;

static void callback(void*context,int*labels,int*dst){
	(void)context;
	int i,who;
	int chksum;

	work_msg.label=labels[0];
	for(i=0;i<size;i++){
		work_msg.dest[i]=dst[i];
	}
	chksum=chkbase^hash_4_4((ub4*)(work_msg.dest),size,0);
	who=chksum%mpi_nodes;
	if (who<0) who+=mpi_nodes;
	MPI_Send(&work_msg,WORK_SIZE,MPI_CHAR,who,WORK_TAG,MPI_COMM_WORLD);
	msgcount++;
}


static int temp[2*MAX_PARAMETERS];
static char name[100];

int main(int argc, char*argv[]){
	int i,j,clean,found,visited,explored,explored_this_level,transitions,limit,level,begin,accepted;
	int lvl_scount,lvl_tcount,*lvl_temp=NULL,submitted,src_filled;
	MPI_Status status;
	void *bottom;

	long long int total_states=0,lvl_states=0;
	long long int total_transitions=0,lvl_transitions=0;

	bottom=(void*)&argc;
        MPI_Init(&argc, &argv);
        MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
	sprintf(who,"inst-mpi(%2d)",mpi_me);
	RTinit(argc,&argv);
	set_label(who);
	if (mpi_me==0){
		lvl_temp=(int*)malloc(mpi_nodes*sizeof(int));
	}

	tcount=(int*)malloc(mpi_nodes*sizeof(int));

	msgcount=0;
	clean=1;

	Warning(info,"initializing grey box module");
	MCRLinitGreybox(argc,argv,bottom);
	if (mpi_me!=0) MPI_Barrier(MPI_COMM_WORLD);
	parse_options(options,argc,argv);
	if (mpi_me==0) MPI_Barrier(MPI_COMM_WORLD);
	Warning(info,"creating model for %s",argv[argc-1]);
	GBcreateModel(argv[argc-1]);
	Warning(info,"model created");

	/* Initializing according to the options just parsed.
	 */
	if (nice_value) {
		if (mpi_me==0) Warning(info,"setting nice to %d\n",nice_value);
		nice(nice_value);
	}
	if (mpi_me==0) {
		if (master_no_step) {
			if (loadbalancing && mpi_nodes>1) {
				no_step=1;
				Warning(info,"stepping disabled at master node");
			} else {
				Warning(info,"ignoring -master-no-step");
			}
		}
	}
	if (mpi_me==0){
		if (write_lts && !outputarch) Fatal(1,error,"please specify the output archive with -out");
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (strstr(outputarch,"%s")) {
		arch=arch_fmt(outputarch,mpi_io_read,mpi_io_write,prop_get_U32("bs",65536));
	} else {
		uint32_t bs=prop_get_U32("bs",65536);
		uint32_t bc=prop_get_U32("bc",128);
		arch=arch_gcf_create(MPI_Create_raf(outputarch,MPI_COMM_WORLD),bs,bs*bc,mpi_me,mpi_nodes);
	}
	/***************************************************/
	if (write_lts) {
		output_src=(stream_t*)malloc(mpi_nodes*sizeof(FILE*));
		output_label=(stream_t*)malloc(mpi_nodes*sizeof(FILE*));
		output_dest=(stream_t*)malloc(mpi_nodes*sizeof(FILE*));
		for(i=0;i<mpi_nodes;i++){
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
	size=GBgetStateLength(NULL);
	if (size<2) Fatal(1,error,"there must be at least 2 parameters");
	if (size>MAX_PARAMETERS) Fatal(1,error,"please make src and dest dynamic");
	TreeDBSinit(size,1);
	/***************************************************/
	if (mpi_me==0) {
		act_db=SIcreate();
		leaf_db=SIcreate();
	}
	/***************************************************/
	GBgetInitialState(NULL,temp+size);
	Warning(info,"initial state computed at %d",mpi_me);
	chkbase=hash_4_4((ub4*)(temp+size),size,0);
	Warning(info,"initial state translated at %d",mpi_me);
	explored=0;
	transitions=0;
	if(mpi_me==0){
		Warning(info,"folding initial state at %d",mpi_me);
		Fold(temp);
		if (temp[1]) Fatal(1,error,"Initial state wasn't assigned state no 0");
		visited=1;
	} else {
		visited=0;
	}
	/***************************************************/
	round_msg.go=1;
	level=0;
	while(round_msg.go){
		limit=visited;
		begin=explored;
		accepted=0;
		submitted=0;
		explored_this_level=0;
		level++;
		lvl_scount=0;
		lvl_tcount=0;
		Warning(info,"entering loop\n");
		for(;;){
			/* Check if we have finished exploring our own states */
			if (limit==explored) {
				limit--; // Make certain we run this code precisely once.
				if (mpi_me==0) {
					//Warning(info,"initiating termination detection.");
					term_msg.clean=1;
					term_msg.count=0;
					term_msg.unexplored=(visited-explored);
					MPI_Send(&term_msg,TERM_SIZE,MPI_CHAR,mpi_nodes-1,TERM_TAG,MPI_COMM_WORLD);
				}
				if (loadbalancing && !no_step) {
					Warning(info,"broadcasting idle");
					for(i=0;i<mpi_nodes;i++) if (i!= mpi_me) {
						MPI_Send(&level,1,MPI_INT,i,IDLE_TAG,MPI_COMM_WORLD);
					}
				}
			}
			/* Handle incoming transitions. */
			for(;;) {
				MPI_Iprobe(MPI_ANY_SOURCE,WORK_TAG,MPI_COMM_WORLD,&found,&status);
				if (!found) break;
				MPI_Recv(&work_msg,WORK_SIZE,MPI_CHAR,MPI_ANY_SOURCE,WORK_TAG,MPI_COMM_WORLD,&status);
				msgcount--;
				clean=0;
				for(i=0;i<size;i++){
					temp[size+i]=work_msg.dest[i];
				}
				Fold(temp);
				if (temp[1]>=visited) {
					visited=temp[1]+1;
				}
				if (write_lts){
					DSwriteU32(output_src[work_msg.src_worker],work_msg.src_number);
					DSwriteU32(output_label[work_msg.src_worker],work_msg.label);
					DSwriteU32(output_dest[work_msg.src_worker],temp[1]);
				}
				tcount[work_msg.src_worker]++;
				transitions++;
			}
			/* Let master check for term database queries. */
			if (mpi_me==0){
				map_server_probe(1);
				map_server_probe(2);
			}
			/* In case of load balancing offload as many states as possible. */
			if (loadbalancing) while (limit>explored) {
				MPI_Iprobe(MPI_ANY_SOURCE,IDLE_TAG,MPI_COMM_WORLD,&found,&status);
				if (!found) {
					break;
				} else {
					int idle_level;
					MPI_Recv(&idle_level,1,MPI_INT,MPI_ANY_SOURCE,IDLE_TAG,MPI_COMM_WORLD, &status);
					if (idle_level != level) {
						//fprintf(stderr,"%2d: discarding idle %d at %d\n",mpi_me,idle_level,level);
						continue;
					}
					temp[1]=explored;
					Unfold(temp);
					submit_msg.segment=mpi_me;
					submit_msg.offset=explored;
					for(i=0;i<size;i++){
						submit_msg.state[i]=temp[size+i];
					}
					//Warning(info,"submitting state %d to %d",explored,status.MPI_SOURCE);
					MPI_Send(&submit_msg,SUBMIT_SIZE,MPI_CHAR,status.MPI_SOURCE,SUBMIT_TAG,MPI_COMM_WORLD);
					msgcount++;
					explored++;
					submitted++;
				}
			}
			/* Load the next state into the source array from our own database. */
			if (!no_step && limit>explored) {
				temp[1]=explored;
				Unfold(temp);
				src=temp+size;
				src_filled=1;
				work_msg.src_worker=mpi_me;
				work_msg.src_number=explored;
				//Warning(info,"exploring state %d",explored);
				explored++;
			} else {
				src_filled=0;
			}
			/* Load the next state into the source array from the network. */
			if (!no_step && !src_filled && loadbalancing) {
				MPI_Iprobe(MPI_ANY_SOURCE,SUBMIT_TAG,MPI_COMM_WORLD,&found,&status);
				if (found) {
					MPI_Recv(&submit_msg,SUBMIT_SIZE,MPI_CHAR,MPI_ANY_SOURCE,SUBMIT_TAG,MPI_COMM_WORLD,&status);
					msgcount--;
					//fprintf(stderr,"%2d: receiving state from %d\n",mpi_me,status.MPI_SOURCE);
					accepted++;
					MPI_Send(&level,1,MPI_INT,status.MPI_SOURCE,IDLE_TAG,MPI_COMM_WORLD);
					src=submit_msg.state;
					src_filled=1;
					work_msg.src_worker=submit_msg.segment;
					work_msg.src_number=submit_msg.offset;	
				}
			}
			/* If the source array is loaded then explore it. */
			if (src_filled){
				int count;
				src_filled=0;
				//Warning(info,"stepping %d.%d",work_msg.src_worker,work_msg.src_number);
				count=GBgetTransitionsAll(NULL,src,callback,NULL);;
				if (count<0) Fatal(1,error,"error in STstep");
				lvl_scount++;
				lvl_tcount+=count;
				if ((lvl_scount%1000)==0) {
					if (verbosity) fprintf(stderr,"%2d: generated %d transitions from %d states\n",
						mpi_me,lvl_tcount,lvl_scount);
				}
				continue;
			}
			/* Termination messages have lower priority than everything else, so we
			 * check those last of all.
			 */
			MPI_Iprobe(MPI_ANY_SOURCE,TERM_TAG,MPI_COMM_WORLD,&found,&status);
			if (found) {
				MPI_Recv(&term_msg,TERM_SIZE,MPI_CHAR,MPI_ANY_SOURCE,TERM_TAG,MPI_COMM_WORLD,&status);
				if (mpi_me==0){
					if(clean && term_msg.clean && (msgcount+term_msg.count)==0){
						//fprintf(stderr,"level %d terminated with %d unexplored\n",level,term_msg.unexplored);
						round_msg.go=(term_msg.unexplored!=0);
						for(i=1;i<mpi_nodes;i++){
							MPI_Send(&round_msg,ROUND_SIZE,MPI_CHAR,i,ROUND_TAG,MPI_COMM_WORLD);
						}
						break;
					} else {
						//fprintf(stderr,"new round\n");
						clean=1;
						term_msg.clean=1;
						term_msg.count=0;
						term_msg.unexplored=(visited-explored);
						MPI_Send(&term_msg,TERM_SIZE,MPI_CHAR,mpi_nodes-1,TERM_TAG,MPI_COMM_WORLD);
					}
				} else {
					//fprintf(stderr,"%d forwarding token\n",mpi_me);
					term_msg.clean=term_msg.clean&&clean;
					term_msg.count+=msgcount;
					term_msg.unexplored+=(visited-explored);
					clean=1;
					MPI_Send(&term_msg,TERM_SIZE,MPI_CHAR,mpi_me-1,TERM_TAG,MPI_COMM_WORLD);
				}
			}
			/* If this level was finished start with the next. */
			MPI_Iprobe(MPI_ANY_SOURCE,ROUND_TAG,MPI_COMM_WORLD,&found,&status);
			if (found) {
				MPI_Recv(&round_msg,ROUND_SIZE,MPI_CHAR,0,ROUND_TAG,MPI_COMM_WORLD,&status);
				break;
			}
			/* Nothing to do anymore, so we block until the next message. */
			MPI_Probe(MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
		}
		/* The level was finished. */
		Warning(info,"explored %d states and %d transitions",lvl_scount,lvl_tcount);
		if (loadbalancing) {
			Warning(info,"states accepted %d submitted %d\n",accepted,submitted);
		}
		/* Compute the global counts at the master. */
		MPI_Gather(&lvl_scount,1,MPI_INT,lvl_temp,1,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			lvl_states=0;
			for(i=0;i<mpi_nodes;i++){
				lvl_states+=lvl_temp[i];
			}
			total_states+=lvl_states;
		}
		MPI_Gather(&lvl_tcount,1,MPI_INT,lvl_temp,1,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			lvl_transitions=0;
			for(i=0;i<mpi_nodes;i++){
				lvl_transitions+=lvl_temp[i];
			}
			total_transitions+=lvl_transitions;
			/*** The ATerm library does not recognize %lld ***/
			if (verbosity) fprintf(stderr,"level %d has %lld states and %lld transitions\n",level-1,lvl_states,lvl_transitions);
		}
		lvl_scount=visited-explored;
		MPI_Gather(&lvl_scount,1,MPI_INT,lvl_temp,1,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			lvl_states=0;
			for(i=0;i<mpi_nodes;i++){
				lvl_states+=lvl_temp[i];
			}
			/*** The ATerm library does not recognize %lld ***/
			if (verbosity) fprintf(stderr,"after level %d there are %lld explored and %lld transitions %lld unexplored\n",
				level-1,total_states,total_transitions,lvl_states);
		}
	}
	/* State space was succesfully generated. */
	Warning(info,"My share is %d states and %d transitions",explored,transitions);
	if (write_lts){
		for(i=0;i<mpi_nodes;i++){
			DSclose(&output_src[i]);
			DSclose(&output_label[i]);
			DSclose(&output_dest[i]);
		}
	}
	Warning(info,"transition files closed");
	{
	int *temp=NULL;
	int tau;
	stream_t info_s=NULL;
		if (mpi_me==0){
			/* It would be better if we didn't create tau if it is non-existent. */
			stream_t ds=arch_write(arch,"TermDB",plain?NULL:"gzip",1);
			int act_count=0;
			for(;;){
				char*s=SIget(act_db,act_count);
				if (s==NULL) break;
				act_count++;
				DSwrite(ds,s,strlen(s));
				DSwrite(ds,"\n",1);
			}
			DSclose(&ds);
			tau=SIlookup(act_db,"tau");
			Warning(info,"%d actions, tau has index %d",act_count,tau);
			/* Start writing the info file. */
			info_s=arch_write(arch,"info",plain?NULL:"",1);
			DSwriteU32(info_s,31);
			DSwriteS(info_s,"generated by instantiator-mpi");
			DSwriteU32(info_s,mpi_nodes);
			DSwriteU32(info_s,0);
			DSwriteU32(info_s,0);
			DSwriteU32(info_s,act_count);
			DSwriteU32(info_s,tau);
			DSwriteU32(info_s,size-1);
			temp=(int*)malloc(mpi_nodes*mpi_nodes*sizeof(int));
		}
		MPI_Gather(&explored,1,MPI_INT,temp,1,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			total_states=0;
			for(i=0;i<mpi_nodes;i++){
				total_states+=temp[i];
				DSwriteU32(info_s,temp[i]);
			}
			total_transitions=0;
		}
		MPI_Gather(tcount,mpi_nodes,MPI_INT,temp,mpi_nodes,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			for(i=0;i<mpi_nodes;i++){
				for(j=0;j<mpi_nodes;j++){
					total_transitions+=temp[i+mpi_nodes*j];
					//Warning(info,"%d -> %d : %d",i,j,temp[i+mpi_nodes*j]);
					DSwriteU32(info_s,temp[i+mpi_nodes*j]);
				}
			}
			DSclose(&info_s);
			if (verbosity) {
				fprintf(stderr,"generated %lld states and %lld transitions\n",total_states,total_transitions);
				fprintf(stderr,"state space has %d levels\n",level);
			}
		}
	}
	arch_close(&arch);
	MPI_Finalize();

	return 0;
}


