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

static void no_op(lts_t lts){
    (void)lts;
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
    if (reduce==NULL){
        Abort("please specify reduction");
    }
    lts_t lts=lts_create();
    Debug("reading %s",files[0]);
    lts_read(files[0],lts);
    Print(infoShort,"input has %u states and %u transitions",
                          lts->states,lts->transitions);
    for(unsigned int i=0;i<lts->root_count;i++){
        Debug("root %d is %u",i,lts->root_list[i]);
    }
    Print(info,"the lts type is:");
    lts_type_print(info,lts->ltstype);
    reduce(lts);
    Print(infoShort,"reduced LTS has %u states and %u transitions",
                          lts->states,lts->transitions);
    for(unsigned int i=0;i<lts->root_count;i++){
        Debug("root %d is %u",i,lts->root_list[i]);
    }
    if (files[1]){
        Debug("writing %s",files[1]);
        lts_write(files[1],lts,segments);
        Debug("output written");
    } else {
        Debug("no output");
    }
    HREexit(EXIT_SUCCESS);
}
