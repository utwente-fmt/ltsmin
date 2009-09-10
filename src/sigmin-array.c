#include <mpi.h>
#include <mpi-runtime.h>
#include <stddef.h>
#include <seg-lts.h>
#include <sig-array.h>
#include <fast_hash.h>
#include <stringindex.h>

static int mpi_nodes;
static int mpi_me;

struct sig_id_arg {
    sig_id_t id;
    e_idx_t edge;
};

static void sig_id_set(void *context,int from,int len,void*arg){
    (void)len;
    sig_array_t sa=(sig_array_t)context;
    struct sig_id_arg* task=(struct sig_id_arg*)arg;
    SigArraySetDestID(sa,from,task->edge,task->id);
}

struct table_query {
    s_idx_t state;
    char sig[];
};
struct state_id_arg {
    sig_id_t id;
    s_idx_t state;
};

static task_t state_id_update=NULL;

void table_query(void *context,int from,int len,void*arg){
    string_index_t idx=(string_index_t)context;
    int ofs=offsetof(struct table_query,sig);
    len=len-ofs;
    char data[len+1];
    for(int i=0;i<len;i++) data[i]=((char*)(arg+ofs))[i];
    data[len]=0;
    //Warning(debug,"got sig %s",data);
    struct state_id_arg res;
    res.id=SIputC(idx,data,len)*mpi_nodes+mpi_me;
    res.state=((struct table_query*)arg)->state;
    //Warning(debug,"setting id of %d.%d to %lld",from,res.state,res.id);
    TaskSubmitFixed(state_id_update,from,&res);
}

void state_id_set(void *context,int from,int len,void*arg){
    (void)len;(void)from;
    sig_array_t sa=(sig_array_t)context;
    struct state_id_arg* task=(struct state_id_arg*)arg;
    //Warning(debug,"setting id of %d to %lld",task->state,task->id);
    SigArraySetID(sa,task->state,task->id);
}

sig_id_t *SAcomputeEquivalence(seg_lts_t lts,const char* equivalence){
    task_queue_t task_queue=SLTSgetQueue(lts);
    task_queue_t task_queue_2=TQdup(task_queue);
    mpi_me=SLTSsegmentNumber(lts);
    mpi_nodes=SLTSsegmentCount(lts);
    
	if (mpi_me==0) Warning(info,"creating signature array for %s",equivalence);
	
	sig_array_t sa=SigArrayCreate(lts,mpi_me,equivalence);
	
	TQbarrier(task_queue);
	
	s_idx_t N=SLTSstateCount(lts);
	for (s_idx_t i=0;i<N;i++) SigArraySetID(sa,i,99);
	
	task_t sig_id_update=TaskCreateFixed(task_queue,sizeof(struct sig_id_arg),sa,sig_id_set);

	TQbarrier(task_queue);

    string_index_t idx=SIcreate();
	int old_count=0;
	int new_count=1;

    task_t sig_lookup=TaskCreateFlex(task_queue,idx,table_query);
    state_id_update=TaskCreateFixed(task_queue_2,sizeof(struct state_id_arg),sa,state_id_set);

    uint32_t *in_begin=SLTSmapInBegin(lts);
    uint32_t *in_seg=SLTSmapInField(lts,0);
    uint32_t *in_edge=SLTSmapInField(lts,1);

    int round=0;
	while(old_count!=new_count){
	    round++;
	    old_count=new_count;
	    Warning(info,"starting round %d",round);
	    set_label("round %d (%2d/%2d)",round,mpi_me,mpi_nodes);
	    SigArrayStartRound(sa);
    	TQbarrier(task_queue);
	    for(;;) {
	        sig_event_t event=SigArrayNext(sa);
	        switch(event.what){
            case ID_READY:{
                struct sig_id_arg task;
                task.id=SigArrayGetID(sa,event.where);
                for(uint32_t i=in_begin[event.where];i<in_begin[event.where+1];i++){
                    task.edge=in_edge[i];
                    TaskSubmitFixed(sig_id_update,in_seg[i],&task);
                }
                continue;
                }
            case SIG_READY: {
                chunk sig=SigArrayGetSig(sa,event.where);
                int size=(offsetof(struct table_query,sig))+sig.len;
                char buffer[size];
                struct table_query* query=(struct table_query*)buffer;
                query->state=event.where;
                for(int i=0;i<(int)sig.len;i++) query->sig[i]=sig.data[i];
                uint32_t hash=SuperFastHash(sig.data,sig.len,0x0739c2d6);
                uint32_t owner=hash%mpi_nodes;
                TaskSubmitFlex(sig_lookup,owner,size,buffer);
                continue;
            }
            case COMPLETED:
                Warning(debug,"completed");
                break;
	        }
	        break;
	    }
    	TQwait(task_queue);
    	TQwait(task_queue_2);
    	int tmp=SIgetCount(idx);
    	Warning(debug,"share of sig table is %d",tmp);
    	MPI_Allreduce(&tmp,&new_count,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
    	if (mpi_me==0) Warning(info,"finished round %d with %d blocks",round,new_count);
    	SIreset(idx);
    	TQbarrier(task_queue);
    }

	TQbarrier(task_queue);
	
	if (mpi_me==0) Warning(info,"destroying sig array");
	
    sig_id_t *relation;
	SigArrayDestroy(sa,&relation);
	
	if (mpi_me==0) Warning(info,"done");
	return relation;
}




