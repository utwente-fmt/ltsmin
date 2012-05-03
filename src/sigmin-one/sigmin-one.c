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
static int copy=0;
static char* lump=UNDEFINED_METHOD;
static int segments=1;
static int silent=0;
static int mkdet=0;
static int cycle=0;

static void no_op(lts_t lts){
    (void)lts;
}

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
    { NULL , 's' , POPT_ARG_VAL , &strong , 0 , "minimize module strong bisimulation" , NULL },
    { "strong" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,
      &strong , 0 , "The short option uses the default variant lowmem." , "<variant>" },
    { NULL , 'b' , POPT_ARG_VAL , &branching , 0 , "minimize module branching bisimulation" , NULL },
    { "branching" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,
      &branching , 0 , "The short option uses the default variant lowmem." , "<variant>" },
    { "copy" , 'c' , POPT_ARG_VAL , &copy , 1 , "perform a load/store copy"  , NULL },
    { "lump" , 'l' , POPT_ARG_VAL , &lump , 0 , "minimize module lumping of CTMC" , NULL },
    { "silent" , 0 , POPT_ARG_VAL , &silent , 1 , "silent step bisimulation" , NULL },
    { "cycle" , 0 , POPT_ARG_VAL , &cycle , 1 , "cycle elimination" , NULL },
    { "determinize" , 0 , POPT_ARG_VAL , &mkdet , 1 , "compute deterministic variant" , NULL },
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
    if (copy){
        if (reduce!=NULL) Abort("reduction specifed twice");
        reduce=no_op;
    }
    if (silent){
        if (reduce!=NULL) Abort("reduction specifed twice");
        reduce=silent_compression;
    }
    if (mkdet){
        if (reduce!=NULL) Abort("reduction specifed twice");
        reduce=lts_mkdet;
    }
    if (cycle){
        if (reduce!=NULL) Abort("reduction specifed twice");
        reduce=lts_cycle_elim;
    }
    if (reduce==NULL){
        Abort("please specify reduction");
    }
    lts_t lts=lts_create();
    Debug("reading %s",files[0]);
    lts_read(files[0],lts);
    Print(infoShort,"input has %u states and %u transitions",
                          lts->states,lts->transitions);
    reduce(lts);
    Print(infoShort,"reduced LTS has %u states and %u transitions",
                          lts->states,lts->transitions);
    if (files[1]){
        Debug("writing %s",files[1]);
        lts_write(files[1],lts,segments);
        Debug("output written");
    } else {
        Debug("no output");
    }
    HREexit(EXIT_SUCCESS);
}
