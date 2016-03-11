// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <lts-io/internal.h>
#include <ltsmin-lib/ltsmin-standard.h>

typedef enum {AUT_FMT, BCG_FMT, DIR_FMT, GCD_FMT, GCF_FMT, FSM_FMT } lts_format_t;

static lts_format_t get_fmt(const char*name){
    char *extension=strrchr(name,'.');
    if(!extension) Abort("filename %s has no extension",name);
    extension++;
    if (!strcmp(extension,"aut")) return AUT_FMT;
    if (!strcmp(extension,"gcd")) return GCD_FMT;
    if (!strcmp(extension,"gcf")) return GCF_FMT;
    if (!strcmp(extension,"dir")) return DIR_FMT;
    if (!strcmp(extension,"fsm")) return FSM_FMT;
    if (!strcmp(extension,"bcg"))  {
        #if HAVE_BCG_USER_H
        return BCG_FMT;
        #else
        Abort("BCG support was not enabled at compile time.");
        #endif
    }
    Abort("Unsupported file format %s",extension);
}

lts_file_t lts_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings){
    lts_format_t fmt=get_fmt(name);
    switch(fmt){
        case AUT_FMT: return aut_file_create(name,ltstype,segments,settings);
        case BCG_FMT: return bcg_file_create(name,ltstype,segments,settings);
        case GCF_FMT: return gcf_file_create(name,ltstype,segments,settings);
        case GCD_FMT: return gcd_file_create(name,ltstype,segments,settings);
        case DIR_FMT: return dir_file_create(name,ltstype,segments,settings);
        case FSM_FMT: return fsm_file_create(name,ltstype,segments,settings);
        default: Abort("no submodule can create %s",name);
    }
}


lts_file_t lts_file_open(const char* name){
    lts_format_t fmt=get_fmt(name);
    switch(fmt){
        case AUT_FMT: return aut_file_open(name);
        case BCG_FMT: return bcg_file_open(name);
        case GCD_FMT: return gcd_file_open(name);
        case GCF_FMT: return gcf_file_open(name);
        case DIR_FMT: return dir_file_open(name);
        default: Abort("no submodule can open %s",name);
    }
}

void lts_file_copy(lts_file_t src,lts_file_t dst){
    if (!lts_push_supported(src)) Abort("src is not readable");
    if (!lts_pull_supported(dst)) Abort("dst is not writable");
    if (lts_write_supported(dst)){
        lts_file_push(src,dst);
    } else if (lts_read_supported(src)) {
        lts_file_pull(dst,src);
    } else {
        Abort("please use load/store copy instead");
    }
}

lts_type_t single_action_type(){
    lts_type_t ltstype=lts_type_create();
    lts_type_set_state_length(ltstype,0);
    int typeno = lts_type_add_type(ltstype,LTSMIN_EDGE_TYPE_ACTION_PREFIX,NULL);
    HREassert (typeno == 0);
    lts_type_set_edge_label_count(ltstype,1);
    lts_type_set_edge_label_name(ltstype,0,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    lts_type_set_edge_label_type(ltstype,0,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    lts_type_set_state_label_count(ltstype,0);
    return ltstype;
}

int lts_io_blocksize=32768;
int lts_io_blockcount=32;

static  struct poptOption options[] = {
    { "block-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT ,
      &lts_io_blocksize , 0 , "set the size of a block in bytes" , "<bytes>" },
    { "cluster-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT ,
      &lts_io_blockcount , 0 , "set the number of blocks in a cluster" , "<blocks>"},
    POPT_TABLEEND
};


void lts_lib_setup(){
    HREaddOptions(options,"LTS IO options");
}
