// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <lts-io/user.h>
#include <lts-lib/lts.h>
#include <lts-lib/lowmem.h>
#include <ltsmin-lib/ltsmin-standard.h>

typedef enum {Undefined=0,Strong,Branching,Trace,Lumping} task_t;

static task_t task=Undefined;
static int divergence_sensitive=0;
static int stuttering=0;

static void trace_compare(lts_t lts){
    if (stuttering){
        Print(info,"reducing modulo silent step bisimulation");
        if (lts->label!=NULL && lts->properties==NULL){
            lts_silent_compress(lts,tau_step,NULL);
        } else if (lts->label==NULL && lts->properties!=NULL) {
            lts_silent_compress(lts,stutter_step,NULL);
        } else {
            Abort("silent step compression requires either state labels or edge labels");
        }
    } else {   
        Print(info,"reducing modulo strong bisimulation");
        lowmem_strong_reduce(lts);
    }
    Print(info,"result has %u roots, %u states and %u transitions",lts->root_count,lts->states,lts->transitions);
    if (lts->root_count==1) return;
    Print(info,"determinizing");
    lts_mkdet(lts);
    Print(info,"result has %u roots, %u states and %u transitions",lts->root_count,lts->states,lts->transitions);
    if (lts->root_count==1) return;
    Print(info,"reducing modulo strong bisimulation");
    lowmem_strong_reduce(lts);
    Print(info,"result has %u roots, %u states and %u transitions",lts->root_count,lts->states,lts->transitions);
}

static  struct poptOption options[] = {
    { "strong" , 's' , POPT_ARG_VAL , &task , Strong , "minimize module strong bisimulation" , NULL },
    { "branching" , 'b' , POPT_ARG_VAL , &task, Branching , "minimize module branching bisimulation" , NULL },
    { "divergence" , 0 , POPT_ARG_VAL , &divergence_sensitive , 1 , "make branching bisimulation divergence sensitive" , NULL },
    { "lump" , 'l' , POPT_ARG_VAL , &task, Lumping , "minimize module lumping of CTMC" , NULL },
    { "trace" , 't' , POPT_ARG_VAL , &task, Trace, "compare modulo trace equivalence" , NULL },
    { "stutter" , 0 , POPT_ARG_VAL , &stuttering, 1, "allow stuttering during trace equivalence" , NULL },
    POPT_TABLEEND
};

int main(int argc, char *argv[]){
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Tool for comparing LTSs\n\nOptions");
    char *files[2];
    lts_lib_setup();
    HREinitStart(&argc,&argv,2,2,files,"<input 1> <input 2>");
    if (task==Undefined){
        Abort("please specify equivalence");
    }
    lts_t lts1=lts_create();
    Print(infoShort,"reading %s",files[0]);
    lts_read(files[0],lts1);
    Print(infoShort,"first LTS has %u states and %u transitions",
                          lts1->states,lts1->transitions);
    if (lts1->root_count!=1) Abort("First LTS must have 1 initial state"); 
    lts_t lts2=lts_create();
    Print(infoShort,"reading %s",files[1]);
    lts_read(files[1],lts2);
    Print(infoShort,"second LTS has %u states and %u transitions",
                          lts2->states,lts2->transitions);
    if (lts2->root_count!=1) Abort("Second LTS must have 1 initial state"); 
    Print(info,"merging the two LTSs");
    lts_merge(lts1,lts2);
    Print(info,"reducing merged LTS");


    switch(task){
        case Strong:{
            lowmem_strong_reduce(lts1);
            break;
        }
        case Branching:{
            bitset_t divergence=NULL;
            if (divergence_sensitive){
                divergence=bitset_create(256,256);
                lts_find_divergent(lts1,tau_step,NULL,divergence);
            }
            lowmem_branching_reduce(lts1,divergence);
            break;
        }
        case Lumping:{
            lowmem_lumping_reduce(lts1);
            break;
        }
        case Trace:{
            trace_compare(lts1);
            break;
        }
        default: Abort("missing case");
    }    


    if (lts1->root_count==1) {
        Print(infoShort,"LTSs are equivalent");
        HREexit(LTSMIN_EXIT_SUCCESS);
    } else {
        Print(infoShort,"LTSs are distinguishable");
        HREexit(LTSMIN_EXIT_COUNTER_EXAMPLE);
    }
}
