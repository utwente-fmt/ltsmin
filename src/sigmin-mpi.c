#include <popt.h>
#include <mpi.h>
#include <hre-main.h>
#include <mpi-event-loop.h>
#include <stddef.h>
#include <lts_enum.h>
#include <lts_io.h>
#include <greybox.h>
#include <stringindex.h>
#include <seg-lts.h>
#include <sig-array.h>
#include <fast_hash.h>
#include <sigmin-array.h>
#include <sigmin-set.h>
#include <scctimer.h>

#define STRONG_REDUCTION_SET 1
#define BRANCHING_REDUCTION_SET 2
#define STRONG_REDUCTION_SA 3
#define STRONG_REDUCTION_TCF_SET 4
#define BRANCHING_REDUCTION_TCF_SET 5

static int action=STRONG_REDUCTION_SET;

static  struct poptOption options[] = {
    {"strong",'s',POPT_ARG_VAL,&action,STRONG_REDUCTION_SET,
        "reduce modulo strong bisimulation (default)",NULL},
    {"strong-tcf",0,POPT_ARG_VAL,&action,STRONG_REDUCTION_TCF_SET,
        "reduce tau cycle free LTS modulo strong bisimulation",NULL},
    {"strong-sa",0,POPT_ARG_VAL,&action,STRONG_REDUCTION_SA,
        "reduce modulo strong bisimulation using sig-array method",NULL},
    {"branching",'b',POPT_ARG_VAL,&action,BRANCHING_REDUCTION_SET,
        "reduce modulo branching bisimulation",NULL},
    {"branching-tcf",0,POPT_ARG_VAL,&action,BRANCHING_REDUCTION_TCF_SET,
        "reduce tau cycle free LTS modulo branching bisimulation",NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL ,NULL},
    { NULL,0,POPT_ARG_INCLUDE_TABLE,sigmin_set_options,0,
        "Options for the set based minimizations.", NULL},
    POPT_TABLEEND
};

static int mpi_nodes;
static int mpi_me;

/*
copied from spec2lts-grey.c
 */
static void *new_string_index(void* context){
	(void)context;
	Warning(info,"creating a new string index");
	return SIcreate();
}

static model_t model;
static uint32_t root_seg;
static uint32_t root_ofs;

static seg_lts_t ReadLTS(task_queue_t task_queue,char*filename){
	TQbarrier(task_queue);
	model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);
	char*input_mode;
	lts_input_t input=lts_input_open(filename,model,mpi_me,mpi_nodes,"-is",&input_mode);
	TQbarrier(task_queue);
	if (mpi_me==0) {
		if (mpi_nodes != lts_input_segments(input)){
			Fatal(1,error,"Number of segments %d does not equal number of workers %d",
				lts_input_segments(input),mpi_nodes);
		}
		if (strcmp(input_mode,"-si")&&strcmp(input_mode,"vsi")&&
			strcmp(input_mode,"-is")&&strcmp(input_mode,"vis")){
			Fatal(1,error,"unsupported input mode %s",input_mode);
		}
		Warning(info,"input mode is %s",input_mode);
	}

    TQbarrier(task_queue);
	
    root_seg=lts_root_segment(input);
    root_ofs=lts_root_offset(input);
    seg_lts_t lts=SLTSloadSegment(input,task_queue);
    Warning(debug,"share after loading is %d states, and %d + %d edges (in+out)",
        SLTSstateCount(lts),
        SLTSincomingCount(lts),
        SLTSoutgoingCount(lts));
    Warning(debug,"lts layout is %s",SLTSlayoutString(SLTSgetLayout(lts)));
        
    TQbarrier(task_queue);

    SLTSsetLayout(lts,Succ_Pred);
    Warning(debug,"share after conversion is %d states, and %d + %d edges (in+out)",
        SLTSstateCount(lts),
        SLTSincomingCount(lts),
        SLTSoutgoingCount(lts));
    Warning(debug,"lts layout is %s",SLTSlayoutString(SLTSgetLayout(lts)));

	TQbarrier(task_queue);
    return lts;
}

int main(int argc, char*argv[]){
    char *files[2]={NULL,NULL};
    HRErequireMPI();
    HREinit(&argc,&argv);
    HREaddOptions(options,"Perform a distributed bisimulation reduction.\n\nOptions");
    HREparseOptions(argc,argv,1,2,files,"<input> [<output>]");
    
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);

    event_queue_t mpi_queue=event_queue();
    task_queue_t task_queue=TQcreateMPI(mpi_queue);
    mytimer_t timer=NULL;
    
    if (mpi_me==0){
	    timer=SCCcreateTimer();
	    Warning(info,"reading from %s",files[0]);
	    SCCstartTimer(timer);
    }
    seg_lts_t lts=ReadLTS(task_queue,files[0]);
	if (mpi_me==0){
	    SCCstopTimer(timer);
	    SCClogTimer(info,timer,"reading the LTS took");
	    SCCresetTimer(timer);
    }
    
 	if (mpi_me==0){
	    Warning(info,"starting reduction");
	    SCCstartTimer(timer);
    }
    sig_id_t *map=NULL;
    int tau=-1;
    switch(action){
        case STRONG_REDUCTION_SA:
        case STRONG_REDUCTION_SET:
            break;
        case STRONG_REDUCTION_TCF_SET:
        case BRANCHING_REDUCTION_SET:
        case BRANCHING_REDUCTION_TCF_SET:
        {
            lts_type_t ltstype=SLTSgetType(lts);
            int typeno=lts_type_get_edge_label_typeno(ltstype,0);
            int count=GBchunkCount(model,typeno);
            for(int i=0;i<count;i++){
                chunk c=GBchunkGet(model,typeno,i);
                if (c.len==3 && !strncmp(c.data,"tau",3)){
                    if (tau>=0) Fatal(1,error,"more than one silent action");
                    tau=i;
                }
                if (c.len==1 && !strncmp(c.data,"i",1)){
                    if (tau>=0) Fatal(1,error,"more than one silent action");
                    tau=i;
                }
            }
            if (tau<0) Fatal(1,error,"could not detect silent action");
            break;
        }
        default:
            Fatal(1,error,"internal error: unknown reduction");
    }
    switch(action){
        case STRONG_REDUCTION_SA:
            map=SAcomputeEquivalence(lts,"strong");
            break;
        case STRONG_REDUCTION_SET:
	    map=sigmin_set_strong(mpi_queue,lts);
            break;
        case STRONG_REDUCTION_TCF_SET:
            map=sigmin_set_strong_tcf(mpi_queue,lts,(uint32_t)tau);
            tau=-1;
            break;
        case BRANCHING_REDUCTION_SET:
            map=sigmin_set_branching(mpi_queue,lts,(uint32_t)tau);
            break;
        case BRANCHING_REDUCTION_TCF_SET:
            if (tau<0) Fatal(1,error,"could not detect silent action");
            map=sigmin_set_branching_tcf(mpi_queue,lts,(uint32_t)tau);
            break;
        default:
            Fatal(1,error,"internal error: unknown reduction");
    }
    if (mpi_me==0){
        SCCstopTimer(timer);
        SCClogTimer(info,timer,"reduction took");
    }
    if (files[1]){
        if (mpi_me==0){
            Warning(info,"computing LTS modulo equivalence");
            SCCstartTimer(timer);
        }
        TQbarrier(task_queue);
        Warning(info,"original LTS %u states and %u outgoing edges",
                SLTSstateCount(lts),SLTSoutgoingCount(lts));
        TQbarrier(task_queue);
        SLTSapplyMap(lts,map,(uint32_t)tau);
        uint32_t root_map;
        if (root_seg==(uint32_t)mpi_me) root_map=map[root_ofs];
        MPI_Bcast(&root_map,1,MPI_INT,root_seg,MPI_COMM_WORLD);
        root_seg=root_map%mpi_nodes;
        root_ofs=root_map/mpi_nodes;
        Warning(info,"root is %u/%u",root_seg,root_ofs);
        RTfree(map);
        TQbarrier(task_queue);
        Warning(info,"mapped LTS %u states and %u outgoing edges",
                SLTSstateCount(lts),SLTSoutgoingCount(lts));
        TQbarrier(task_queue);
        if (mpi_me==0){
            SCCstopTimer(timer);
            SCClogTimer(info,timer,"modulo took");
            Warning(info,"Writing output to %s",files[1]);
        }
        lts_output_t output=lts_output_open(files[1],model,mpi_nodes,mpi_me,mpi_nodes,"-ii",NULL);
        lts_output_set_root_idx(output,root_seg,root_ofs);
        TQbarrier(task_queue);
        lts_enum_cb_t output_handle=lts_output_begin(output,mpi_me,mpi_me,mpi_nodes);
        SLTSenum(lts,output_handle);
        /* code that should be made safe and put in the library */
        lts_count_t *count=lts_output_count(output);
        for(int i=0;i<mpi_nodes;i++){
            long long int my_val;
            long long int max_val;
            my_val=count->state[i];
            Warning(info,"state %d is %lld",i,my_val);
            MPI_Allreduce(&my_val,&max_val,1,MPI_LONG_LONG,MPI_MAX,MPI_COMM_WORLD);
            if (mpi_me==0) Warning(info,"max state %d is %lld",i,max_val);
            count->state[i]=max_val;
            for(int j=0;j<mpi_nodes;j++){
                my_val=count->cross[i][j];
                Warning(info,"edge %d -> %d is %lld",i,j,my_val);
                MPI_Allreduce(&my_val,&max_val,1,MPI_LONG_LONG,MPI_MAX,MPI_COMM_WORLD);
                if (mpi_me==0) Warning(info,"max edge %d -> %d is %lld",i,j,max_val);
                count->cross[i][j]=max_val;
            }
        }
        /* end of library code */
        lts_output_end(output,output_handle);
        lts_output_close(&output);
        if (mpi_me==0){
            SCCstopTimer(timer);
            SCClogTimer(info,timer,"writing took");
        }
    }
    if (mpi_me==0) SCCdeleteTimer(timer);
    HREexit(0);
}


