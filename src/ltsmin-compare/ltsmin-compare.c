// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>

#include <hre/user.h>
#include <lts-io/user.h>
#include <lts-lib/lts.h>
#include <lts-lib/lowmem.h>

#define UNDEFINED_METHOD ((char*)1)

static void(*reduce)(lts_t lts)=NULL;
static char* strong=UNDEFINED_METHOD;
static char* branching=UNDEFINED_METHOD;
static char* lump=UNDEFINED_METHOD;
static char* trace=UNDEFINED_METHOD;


static void trace_compare(lts_t lts){
    if (trace==NULL){
        Print(info,"reducing modulo strong bisimulation");
        lowmem_strong_reduce(lts);
    } else {   
        Print(info,"reducing modulo silent step bisimulation");
        if (lts->label!=NULL && lts->properties==NULL){
            lts_silent_compress(lts,tau_step,NULL);
        } else if (lts->label==NULL && lts->properties!=NULL) {
            lts_silent_compress(lts,stutter_step,NULL);
        } else {
            Abort("silent step compression requires either state labels or edge labels");
        }
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
    { NULL , 's' , POPT_ARG_VAL , &strong , 0 , "compare modulo strong bisimulation" , NULL },
    { "strong" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,
      &strong , 0 , "The short option uses the default variant lowmem." , "<variant>" },
    { NULL , 'b' , POPT_ARG_VAL , &branching , 0 , "compare modulo branching bisimulation" , NULL },
    { "branching" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,
      &branching , 0 , "The short option uses the default variant lowmem." , "<variant>" },
    { "lump" , 'l' , POPT_ARG_VAL , &lump , 0 , "compare modulo lumping of CTMC" , NULL },
    { "trace" , 't' , POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL , &trace , 0 ,
      "compare modulo trace equivalence" , "stutter" },
    POPT_TABLEEND
};

int main(int argc, char *argv[]){
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Tool for comparing LTSs\n\nOptions");
    char *files[2];
    lts_lib_setup();
    HREinitStart(&argc,&argv,2,2,files,"<input 1> <input 2>");
    if (strong!=UNDEFINED_METHOD && (strong==NULL || strcmp(strong,"lowmem")==0 )) {
        if (reduce!=NULL) Abort("comparison specified twice");
        reduce=lowmem_strong_reduce;
    }
    if (strong!=UNDEFINED_METHOD && reduce==NULL) {
        Abort("strong bisimulation variant %s not known",strong);
    }
    if (branching!=UNDEFINED_METHOD && (branching==NULL || strcmp(branching,"lowmem")==0 )) {
        if (reduce!=NULL) Abort("comparison specified twice");
        reduce=lowmem_branching_reduce;
    }
    if (branching!=UNDEFINED_METHOD && reduce==NULL) {
        Abort("branching bisimulation variant %s not known",strong);
    }
    if (lump!=UNDEFINED_METHOD) {
        if (reduce!=NULL) Abort("comparison specified twice");
        reduce=lowmem_lumping_reduce;
    }
    if (trace!=UNDEFINED_METHOD) {
        if (reduce!=NULL) Abort("comparison specified twice");
        if (trace !=NULL && strcmp(trace,"stutter")!=0){
            Abort("illegal argument to --trace");
        }
        reduce=trace_compare;
    }
    if (reduce==NULL){
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
    reduce(lts1);
    if (lts1->root_count==1) {
        Print(infoShort,"LTSs are equivalent");
        HREexit(EXIT_SUCCESS);
    } else {
        Print(infoShort,"LTSs are distinguishable");
        HREexit(EXIT_FAILURE);
    }
}
