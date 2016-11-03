// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <mpi.h>
#include <popt.h>
#include <stddef.h>

#include <hre/runtime.h>
#include <hre-mpi/user.h>
#include <hre-mpi/mpi_event_loop.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-reduce-dist/seg-lts.h>
#include <ltsmin-reduce-dist/sigmin-array.h>
#include <ltsmin-reduce-dist/sigmin-set.h>
#include <ltsmin-reduce-dist/sig-array.h>
#include <util-lib/fast_hash.h>
#include <hre/stringindex.h>

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
    { NULL,0,POPT_ARG_INCLUDE_TABLE,sigmin_set_options,0,
        "Options for the set based minimizations.", NULL},
    POPT_TABLEEND
};

static int mpi_nodes;
static int mpi_me;

int main(int argc, char*argv[]){
    char *files[2]={NULL,NULL};
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Perform a distributed bisimulation reduction.\n\nOptions");
    lts_lib_setup();
    HREselectMPI();
    HREinitStart(&argc,&argv,1,2,files,"<input> [<output>]");

    MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);

    event_queue_t mpi_queue=event_queue();
    hre_task_queue_t task_queue=HREcreateQueue(HREglobal());
    rt_timer_t timer=NULL;

    if (mpi_me==0){
        timer=RTcreateTimer();
        Warning(info,"reading from %s",files[0]);
        RTstartTimer(timer);
    }
    seg_lts_t lts=SLTSload(files[0],task_queue);

    Debug("share after loading is %d states, and %d + %d edges (in+out)",
        SLTSstateCount(lts),
        SLTSincomingCount(lts),
        SLTSoutgoingCount(lts));
    Debug("lts layout is %s",SLTSlayoutString(SLTSgetLayout(lts)));

    HREbarrier(HREglobal());

    SLTSsetLayout(lts,Succ_Pred);
    Debug("share after conversion is %d states, and %d + %d edges (in+out)",
        SLTSstateCount(lts),
        SLTSincomingCount(lts),
        SLTSoutgoingCount(lts));
    Debug("lts layout is %s",SLTSlayoutString(SLTSgetLayout(lts)));

    HREbarrier(HREglobal());


    lts_type_t ltstype=SLTSgetType(lts);
    int typeno=lts_type_get_edge_label_typeno(ltstype,0);
    if (mpi_me==0){
        RTstopTimer(timer);
        RTprintTimer(info,timer,"reading the LTS took");
        RTresetTimer(timer);
    }

    if (mpi_me==0){
        Warning(info,"starting reduction");
        RTstartTimer(timer);
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
            value_table_t vt = SLTStable(lts,typeno);

            table_iterator_t it = VTiterator (vt);
            for (int i = 0; IThasNext(it); i++) {
                chunk c = ITnext (it);
                if (c.len==3 && !strncmp(c.data,LTSMIN_EDGE_VALUE_TAU,3)){
                    if (tau>=0) Fatal(1,error,"more than one silent action");
                    tau=i;
                }
                if (c.len==1 && !strncmp(c.data,"i",1)){
                    if (tau>=0) Fatal(1,error,"more than one silent action");
                    tau=i;
                }
            }
            if (tau<0) {
                if (mpi_me==0) Print(infoShort,"no silent action, reverting to strong bisimulation");
                action=STRONG_REDUCTION_SET;
            }
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
        RTstopTimer(timer);
        RTprintTimer(info,timer,"reduction took");
    }
    if (files[1]){
        if (mpi_me==0){
            Warning(info,"computing LTS modulo equivalence");
            RTstartTimer(timer);
        }
        HREbarrier(HREglobal());
        Warning(info,"original LTS %u states and %u outgoing edges",
                SLTSstateCount(lts),SLTSoutgoingCount(lts));
        HREbarrier(HREglobal());
        SLTSapplyMap(lts,map,(uint32_t)tau);
        RTfree(map);
        HREbarrier(HREglobal());
        Warning(info,"mapped LTS %u states and %u outgoing edges",
                SLTSstateCount(lts),SLTSoutgoingCount(lts));
        HREbarrier(HREglobal());
        if (mpi_me==0){
            RTstopTimer(timer);
            RTprintTimer(info,timer,"modulo took");
            Warning(info,"Writing output to %s",files[1]);
        }
        SLTSstore(lts,files[1]);
        if (mpi_me==0){
            RTstopTimer(timer);
            RTprintTimer(info,timer,"writing took");
        }
    }
    if (mpi_me==0) RTdeleteTimer(timer);
    HREexit(LTSMIN_EXIT_SUCCESS);
}


