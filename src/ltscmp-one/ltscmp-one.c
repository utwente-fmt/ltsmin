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

static  struct poptOption options[] = {
    { NULL , 's' , POPT_ARG_VAL , &strong , 0 , "minimize module strong bisimulation" , NULL },
    { "strong" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,
      &strong , 0 , "The short option uses the default variant lowmem." , "<variant>" },
    { NULL , 'b' , POPT_ARG_VAL , &branching , 0 , "minimize module branching bisimulation" , NULL },
    { "branching" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,
      &branching , 0 , "The short option uses the default variant lowmem." , "<variant>" },
    { "lump" , 'l' , POPT_ARG_VAL , &lump , 0 , "minimize module lumping of CTMC" , NULL },
    POPT_TABLEEND
};

int main(int argc, char *argv[]){
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Tool for comparing LTSs\n\nOptions");
    char *files[2];
    lts_lib_setup();
    HREinitStart(&argc,&argv,2,2,files,"<input 1> <input 2>");
    if (strong!=UNDEFINED_METHOD && (strong==NULL || strcmp(strong,"lowmem")==0 )) {
        if (reduce!=NULL) Abort("reduction specifed twice");
        reduce=lowmem_strong_reduce;
    }
    if (strong!=UNDEFINED_METHOD && reduce==NULL) {
        Abort("strong bisimulation variant %s not known",strong);
    }
    if (branching!=UNDEFINED_METHOD && (branching==NULL || strcmp(branching,"lowmem")==0 )) {
        if (reduce!=NULL) Abort("reduction specifed twice");
        reduce=lowmem_branching_reduce;
    }
    if (branching!=UNDEFINED_METHOD && reduce==NULL) {
        Abort("branching bisimulation variant %s not known",strong);
    }
    if (lump!=UNDEFINED_METHOD) {
        if (reduce!=NULL) Abort("reduction specifed twice");
        reduce=lowmem_lumping_reduce;
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
