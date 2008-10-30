#include "config.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <mpi.h>

#include "generichash.h"
#include "rw.h"
#include "mcrl.h"
#include "step.h"
#include "treedbs.h"
#include "stream.h"
#include "options.h"
#include "runtime.h"
#include "archive.h"
#include "sysdep.h"
#include "mpi_io_stream.h"
#include "mpi_ram_raf.h"

#define MAX_PARAMETERS 256
#define MAX_TERM_LEN 5000

static archive_t arch;
static int compare_terms=0;
static int sequential_init_rewriter=0;
static int verbosity=1;
static char *outputarch=NULL;
static int write_lts=1;
static int nice_value=0;
static int master_no_step=0;
static int no_step=0;
static int loadbalancing=1;
static int plain=0;

static int st_help(char* opt,char*optarg,void *arg){
	(void)opt;(void)optarg;(void)arg;
	SThelp();
	return OPT_EXIT;
}

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: mpirun <nodespec> inst-mpi options file ...",NULL,NULL,NULL},
	{"-v",OPT_NORMAL,inc_int,&verbosity,NULL,"increase the level of verbosity",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,reset_int,&verbosity,NULL,"be silent",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{"-st-help",OPT_NORMAL,st_help,NULL,NULL,
		"print help for the mCRL stepper",
		"WARNING: some options (e.g. -conf-table) will",
		"break the correctness of the distributed instantiator",NULL},
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
	{"-cmp",OPT_NORMAL,set_int,&compare_terms,NULL,
		"compare terms in term vector",
		"useful if the representation of data is not unique",
		"e.g. when a set is represented by an unsorted list",NULL},
	{"-seq-rw",OPT_NORMAL,set_int,&sequential_init_rewriter,NULL,
		"Perform initialisation of rewriters sequentially",
		"rather than in parallel. (The old rw needed this option.)",NULL,NULL},
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

static ATerm label=NULL;
static ATerm src[MAX_PARAMETERS];
static ATerm dest[MAX_PARAMETERS];
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

#define SUBMIT_TAG 7
#define SUBMIT_SIZE sizeof(struct submit_msg)
#define IDLE_TAG 6
#define ATERM_TAG 5
#define INT_TAG 4
#define WORK_TAG 3
#define WORK_SIZE sizeof(struct work_msg)
#define ROUND_TAG 2
#define ROUND_SIZE sizeof(struct round_msg)
#define TERM_TAG 1
#define TERM_SIZE sizeof(struct term_msg)

static int mpi_nodes,mpi_me;

typedef struct {
	ATermTable table;
	ATerm *map;
	stream_t TermDB;
	int size;
	int next;
} ATmapStruct;

typedef ATmapStruct *ATmap;

#define MAP_BLOCK_SIZE 256

static ATmap ATmapCreate(stream_t s){
	ATmap map;
	int i;

	map=(ATmap)malloc(sizeof(ATmapStruct));
	map->table=ATtableCreate(MAP_BLOCK_SIZE,75);
	map->map=(ATerm*)malloc(MAP_BLOCK_SIZE*sizeof(ATerm));
	for(i=0;i<MAP_BLOCK_SIZE;i++){
		map->map[i]=NULL;
	}
	map->size=MAP_BLOCK_SIZE;
	map->next=0;
	map->TermDB=s;
	return map;
}


static ATerm EQ(ATerm t, ATerm s) {
  char eq[1024],buf[1024];
  AFun sort;
  Symbol sym;
  sort = MCRLgetSort(t);
  strcpy(buf,ATgetName(sort));
  sprintf(eq,"eq#%s#%s",buf,buf);
  sym = ATmakeSymbol(eq,2,ATtrue);
  if (MCRLgetType(sym)==MCRLunknown) {
    return NULL;
  } else {
    return (ATerm)ATmakeAppl2(sym,t,s);
  }
}

static int ATerm2int(ATmap map,ATerm t){
	ATerm i;
	int ii;

	i=ATtableGet(map->table,t);
	if(i!=NULL){
		return ATgetInt((ATermInt)i);
	}
	if(mpi_me==0){
		if (compare_terms && (MCRLgetType(ATgetSymbol(t))==MCRLconstructor)) {
			ATerm e;
			for(ii=0;ii<map->next;ii++){
				if(MCRLgetSort(t)!=MCRLgetSort(map->map[ii])) {
					continue;
				}
				e=EQ(map->map[ii],t);
				if (e==NULL) {
					ATwarning("equality between %t and %t cannot be computed",MCRLprint(t),MCRLprint(map->map[ii]));
				}
				e=RWrewrite(e);
				if (e==MCRLterm_true) {
					break;
				}
			}
			if (ii==map->next) {
				map->next++;
			}
		} else {
			ii=map->next;
			map->next++;
		}
		while(map->size<=ii){
			int j;
			map->size+=MAP_BLOCK_SIZE;
			map->map=realloc(map->map,(map->size)*sizeof(ATerm));
			for(j=map->size-MAP_BLOCK_SIZE;j<map->size;j++){
				map->map[j]=NULL;
			}
		}
		if (map->map[ii]==NULL) {
			char *s=ATwriteToString(t);
			DSwrite(map->TermDB,s,strlen(s));
			DSwrite(map->TermDB,"\n",1);
			map->map[ii]=t;
		}
	} else {
		char *s=ATwriteToString(t);
		int len=strlen(s);
		MPI_Status status;
		if (len>MAX_TERM_LEN) ATerror("label too long");
		MPI_Send(s,len,MPI_CHAR,0,ATERM_TAG,MPI_COMM_WORLD);
		MPI_Recv(&ii,1,MPI_INT,0,INT_TAG,MPI_COMM_WORLD,&status);
	}
	i=(ATerm)ATmakeInt(ii);
	ATtablePut(map->table,t,i);
	return ii;
}

static ATerm int2ATerm(ATmap map,int i){
	int len;
	char buf[MAX_TERM_LEN+1];
	ATerm ii,t;
	MPI_Status status;

	if ((i<map->size) && (map->map[i]!=NULL)) {
		return map->map[i];
	}
	if(mpi_me==0) ATerror("query item without entering");
	while(map->size<=i){
		int j;
		map->size+=MAP_BLOCK_SIZE;
		map->map=realloc(map->map,(map->size)*sizeof(ATerm));
		for(j=map->size-MAP_BLOCK_SIZE;j<map->size;j++){
			map->map[j]=NULL;
		}
	}
	MPI_Send(&i,1,MPI_INT,0,INT_TAG,MPI_COMM_WORLD);
	MPI_Recv(&buf,MAX_TERM_LEN,MPI_CHAR,0,ATERM_TAG,MPI_COMM_WORLD,&status);
	MPI_Get_count(&status,MPI_CHAR,&len);
	buf[len]=0;
	t=ATreadFromString(buf);
	map->map[i]=t;
	ii=(ATerm)ATmakeInt(i);
	ATtablePut(map->table,t,ii);
	return t;
}

static void map_server_probe(ATmap map){
	MPI_Status status;
	int ii,len,found;
	char *s,buf[MAX_TERM_LEN+1];
	ATerm t;

	for(;;) {
		MPI_Iprobe(MPI_ANY_SOURCE,ATERM_TAG,MPI_COMM_WORLD,&found,&status);
		if (!found) break;
		MPI_Recv(&buf,MAX_TERM_LEN,MPI_CHAR,MPI_ANY_SOURCE,ATERM_TAG,MPI_COMM_WORLD,&status);
		MPI_Get_count(&status,MPI_CHAR,&len);
		buf[len]=0;
		t=ATreadFromString(buf);
		ii=ATerm2int(map,t);
		MPI_Send(&ii,1,MPI_INT,status.MPI_SOURCE,INT_TAG,MPI_COMM_WORLD);
	}
	for(;;) {
		MPI_Iprobe(MPI_ANY_SOURCE,INT_TAG,MPI_COMM_WORLD,&found,&status);
		if (!found) break;
		MPI_Recv(&ii,1,MPI_INT,MPI_ANY_SOURCE,INT_TAG,MPI_COMM_WORLD,&status);
		s=ATwriteToString(int2ATerm(map,ii));
		len=strlen(s);
		MPI_Send(s,len,MPI_CHAR,status.MPI_SOURCE,ATERM_TAG,MPI_COMM_WORLD);
	}
}


static ATmap map;

static int msgcount;

static void callback(void){
	int i,ai,who;
	int chksum;

	work_msg.label=ATerm2int(map,label);
	for(i=0;i<size;i++){
		ai=ATerm2int(map,dest[i]);
		work_msg.dest[i]=ai;
	}
	chksum=chkbase^hash_4_4((ub4*)(work_msg.dest),size,0);
	who=chksum%mpi_nodes;
	if (who<0) who+=mpi_nodes;
	MPI_Send(&work_msg,WORK_SIZE,MPI_CHAR,who,WORK_TAG,MPI_COMM_WORLD);
	msgcount++;
}

static void WarningHandler(const char *format, va_list args) {
     if (!verbosity) return;
     fprintf(stderr,"%s: ", who);
     ATvfprintf(stderr, format, args);
     fprintf(stderr,"\n");
     }
     
static void ErrorHandler(const char *format, va_list args) {
     fprintf(stderr,"%s: ", who);
     ATvfprintf(stderr, format, args);
     fprintf(stderr,"\n");
     MPI_Abort(MPI_COMM_WORLD,-1);
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
	int first_unused;
	//int parent_next=0;;

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

	/* Set up the ATerm library.
	 */
	ATinit(argc, argv, bottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
 	ATprotect(&label);
	for(i=0;i<MAX_PARAMETERS;i++) {
		src[i]=NULL;
		dest[i]=NULL;
	}	
	ATprotectArray(src,MAX_PARAMETERS);
	ATprotectArray(dest,MAX_PARAMETERS);
	/* Parsing options in 2 turns.
	 * In the first turn worker 0 parses options.
	 * In the second turn all other workers parse the options.
	 * In this way an error in the options is reported
	 * precisely once.
	 */
	if (mpi_me!=0) MPI_Barrier(MPI_COMM_WORLD);
	MCRLsetArguments(&argc, &argv);
	RWsetArguments(&argc, &argv);
	STsetArguments(&argc, &argv);
        first_unused=parse_options(options,argc,argv);
	if (mpi_me==0){
		//for (i=0;i<argc;i++){
		//	ATwarning("opt %d is %s",i,argv[i]);
		//}
		if (argc!=first_unused) ATerror("unused options");
	}
	if (mpi_me==0) {
		MPI_Barrier(MPI_COMM_WORLD);
		ATwarning("term comparison is %s",compare_terms?"enabled":"disabled");
	}
	/* Initializing according to the options just parsed.
	 */
	if (nice_value) {
		if (mpi_me==0) ATwarning("setting nice to %d\n",nice_value);
		nice(nice_value);
	}
	if (mpi_me==0) {
		if (master_no_step) {
			if (loadbalancing && mpi_nodes>1) {
				no_step=1;
				ATwarning("stepping disabled at master node");
			} else {
				ATwarning("ignoring -master-no-step");
			}
		}
	}
	if (mpi_me==0){
		if (write_lts && !outputarch) ATerror("please specify the output archive with -out");
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
	MCRLinitialize();
	if (sequential_init_rewriter){
		for(i=0;i<mpi_me;i++){
			MPI_Barrier(MPI_COMM_WORLD);
		}
	}
	if (!RWinitialize(MCRLgetAdt())) ATerror("Initialize rewriter");
	if (sequential_init_rewriter){
		for(i=mpi_me;i<mpi_nodes;i++){
			MPI_Barrier(MPI_COMM_WORLD);
		}
	}
	STinitialize(noOrdering,&label,dest,callback);
	/***************************************************/
	size=MCRLgetNumberOfPars();
	if (size<2) ATerror("there must be at least 2 parameters");
	if (size>MAX_PARAMETERS) ATerror("please make src and dest dynamic");
	TreeDBSinit(size,1);
	/***************************************************/
	if (mpi_me==0) {
		map=ATmapCreate(arch_write(arch,"TermDB",plain?NULL:"gzip",1));
	} else {
		map=ATmapCreate(NULL);
	}
	/***************************************************/
	STsetInitialState();
	ATwarning("initial state computed at %d",mpi_me);
	for(i=0;i<size;i++){
		temp[size+i]=ATerm2int(map,dest[i]);
	}
	chkbase=hash_4_4((ub4*)(temp+size),size,0);
	ATwarning("initial state translated at %d",mpi_me);
	explored=0;
	transitions=0;
	if(mpi_me==0){
		ATwarning("folding initial state at %d",mpi_me);
		Fold(temp);
		if (temp[1]) ATerror("Initial state wasn't assigned state no 0");
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
		//fprintf(stderr,"%d entering loop\n",mpi_me);
		for(;;){
			/* Check if we have finished exploring our own states */
			if (limit==explored) {
				limit--; // Make certain we run this code precisely once.
				if (mpi_me==0) {
					//ATwarning("initiating termination detection.");
					term_msg.clean=1;
					term_msg.count=0;
					term_msg.unexplored=(visited-explored);
					MPI_Send(&term_msg,TERM_SIZE,MPI_CHAR,mpi_nodes-1,TERM_TAG,MPI_COMM_WORLD);
				}
				if (loadbalancing && !no_step) {
					ATwarning("broadcasting idle");
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
				map_server_probe(map);
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
					//ATwarning("submitting state %d to %d",explored,status.MPI_SOURCE);
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
				for(i=0;i<size;i++){
					src[i]=int2ATerm(map,temp[size+i]);
				}
				src_filled=1;
				work_msg.src_worker=mpi_me;
				work_msg.src_number=explored;
				//ATwarning("exploring state %d",explored);
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
					for(i=0;i<size;i++){
						src[i]=int2ATerm(map,submit_msg.state[i]);
					}
					src_filled=1;
					work_msg.src_worker=submit_msg.segment;
					work_msg.src_number=submit_msg.offset;	
				}
			}
			/* If the source array is loaded then explore it. */
			if (src_filled){
				int count;
				src_filled=0;
				//ATwarning("stepping %d.%d",work_msg.src_worker,work_msg.src_number);
				count=STstep(src);
				if (count<0) ATerror("error in STstep");
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
		ATwarning("explored %d states and %d transitions",lvl_scount,lvl_tcount);
		if (loadbalancing) {
			ATwarning("states accepted %d submitted %d\n",accepted,submitted);
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
	ATwarning("My share is %d states and %d transitions",explored,transitions);
	if (write_lts){
		for(i=0;i<mpi_nodes;i++){
			DSclose(&output_src[i]);
			DSclose(&output_label[i]);
			DSclose(&output_dest[i]);
		}
	}
	{
	int *temp=NULL;
	int tau;
	stream_t info=NULL;
		if (mpi_me==0){
			/* It would be better if we didn't create tau if it is non-existent. */
			tau=ATerm2int(map,MCRLterm_tau);
			DSclose(&(map->TermDB));
			/* Start writing the info file. */
			info=arch_write(arch,"info",plain?NULL:"",1);
			DSwriteU32(info,31);
			DSwriteS(info,"generated by mpi-inst");
			DSwriteU32(info,mpi_nodes);
			DSwriteU32(info,0);
			DSwriteU32(info,0);
			DSwriteU32(info,map->next);
			DSwriteU32(info,tau);
			DSwriteU32(info,MCRLgetNumberOfPars()-1);
			ATwarning("%d terms in TermDB, tau has index %d",map->next,tau);
			temp=(int*)malloc(mpi_nodes*mpi_nodes*sizeof(int));
		}
		MPI_Gather(&explored,1,MPI_INT,temp,1,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			total_states=0;
			for(i=0;i<mpi_nodes;i++){
				total_states+=temp[i];
				DSwriteU32(info,temp[i]);
			}
			total_transitions=0;
		}
		MPI_Gather(tcount,mpi_nodes,MPI_INT,temp,mpi_nodes,MPI_INT,0,MPI_COMM_WORLD);
		if (mpi_me==0){
			for(i=0;i<mpi_nodes;i++){
				for(j=0;j<mpi_nodes;j++){
					total_transitions+=temp[i+mpi_nodes*j];
					//ATwarning("%d -> %d : %d",i,j,temp[i+mpi_nodes*j]);
					DSwriteU32(info,temp[i+mpi_nodes*j]);
				}
			}
			DSclose(&info);
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


