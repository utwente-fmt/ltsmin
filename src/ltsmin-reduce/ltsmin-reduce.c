// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <lts-io/user.h>
#include <lts-lib/lts.h>
#include <lts-lib/lowmem.h>
#include <ltsmin-lib/ltsmin-standard.h>

#define UNDEFINED_METHOD ((char*)1)

typedef enum {Undefined=0,Strong,Branching,Cycle,Determinize,Copy,Silent,Lumping} task_t;

static task_t task=Undefined;
static int segments=1;
static int divergence_sensitive=0;

static void lts_cycle_elim(lts_t lts){
    if (lts->label!=NULL && lts->properties==NULL){
        lts_silent_cycle_elim(lts,tau_step,NULL,NULL);
    } else if (lts->label==NULL && lts->properties!=NULL) {
        lts_silent_cycle_elim(lts,stutter_step,NULL,NULL);
    } else {
        Abort("cycle elimination requires either state labels or edge labels");
    }
}

static void silent_compression(lts_t lts){
    if (lts->label!=NULL && lts->properties==NULL){
        lts_silent_compress(lts,tau_step,NULL);
    } else if (lts->label==NULL && lts->properties!=NULL) {
        lts_silent_compress(lts,stutter_step,NULL);
    } else {
        Abort("silent step compression requires either state labels or edge labels");
    }
}

static  struct poptOption options[] = {
    { "strong" , 's' , POPT_ARG_VAL , &task , Strong , "minimize module strong bisimulation" , NULL },
    { "branching" , 'b' , POPT_ARG_VAL , &task, Branching , "minimize module branching bisimulation" , NULL },
    { "divergence" , 0 , POPT_ARG_VAL , &divergence_sensitive , 1 , "make branching bisimulation divergence sensitive" },
    { "copy" , 'c' , POPT_ARG_VAL , &task , Copy , "perform a load/store copy"  , NULL },
    { "lump" , 'l' , POPT_ARG_VAL , &task, Lumping , "minimize module lumping of CTMC" , NULL },
    { "silent" , 0 , POPT_ARG_VAL , &task, Silent  , "silent step bisimulation" , NULL },
    { "cycle" , 0 , POPT_ARG_VAL , &task , Cycle , "cycle elimination" , NULL },
    { "determinize" , 0 , POPT_ARG_VAL , &task , Determinize , "compute deterministic variant" , NULL },
    { "segments" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &segments , 0 ,
      "set the number of segment for the output file" , "<N>" },
    POPT_TABLEEND
};

int main(int argc, char *argv[]){
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Tool for signature minimization\n\nOptions");
    char *files[2];
    lts_lib_setup();
    HREinitStart(&argc,&argv,1,2,files,"<input> [<output>]");
    if (task==Undefined){
        Abort("Please select the reduction to apply.");
    }
    lts_t lts=lts_create();
    Debug("reading %s",files[0]);
    lts_read(files[0],lts);
    Print(infoShort,"input has %u states and %u transitions",
                          lts->states,lts->transitions);
    switch(task){
        case Strong:{
            lowmem_strong_reduce(lts);
            break;
        }
        case Branching:{
            bitset_t divergence=NULL;
            if (divergence_sensitive){
                divergence=bitset_create(256,256);
                lts_find_divergent(lts,tau_step,NULL,divergence);
            }
            lowmem_branching_reduce(lts,divergence);
            break;
        }
        case Lumping:{
            lowmem_lumping_reduce(lts);
            break;
        }
        case Copy:{
            break;
        }
        case Silent:{
            silent_compression(lts);
            break;
        }
        case Determinize:{
            lts_mkdet(lts);
            break;
        }
        case Cycle:{
            lts_cycle_elim(lts);
            break;
        }
        default: Abort("missing case");
    }    
    Print(infoShort,"reduced LTS has %u states and %u transitions",
                          lts->states,lts->transitions);
    if (files[1]){
        Debug("writing %s",files[1]);
        lts_write(files[1],lts,segments);
        Debug("output written");
    } else {
        Debug("no output");
    }
    HREexit(LTSMIN_EXIT_SUCCESS);
}
