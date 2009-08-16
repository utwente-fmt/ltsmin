#include <runtime.h>
#include <ltsmin-syntax.h>
#include <ltsmin-grammar.h>

static int selection=0;

static  struct poptOption options[] = {
    { "dummy" , 0 , POPT_ARG_VAL , &selection , DUMMY , "select dummy parser" , NULL },
    { "tokens" , 0 , POPT_ARG_VAL , &selection , TOKENS , "input is a token list" , NULL },
    { "expr" , 0 , POPT_ARG_VAL , &selection , EXPR , "input is an expression" , NULL },
//    { "create",'c', POPT_ARG_VAL , &operation , GCF_FILE , "create a new archive (default)" , NULL },
//    { "create-dz",0, POPT_ARG_VAL , &operation , GCF_DIR , "create a compressed directory instead of an archive file" , NULL },
//    { "extract",'x', POPT_ARG_VAL , &operation , GCF_EXTRACT , "extract files from an archive" , NULL },
//    { "force",'f' ,  POPT_ARG_VAL , &force , 1 , "force creation of a directory for output" , NULL },
//    { "block-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blocksize , 0 , "set the size of a block in bytes" , "<bytes>" },
//    { "cluster-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blockcount , 0 , "set the number of blocks in a cluster" , "<blocks>"},
//    { "compression",'z',POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT,
//        &policy,0,"set the compression policy used in the archive","<policy>"},
    POPT_TABLEEND
};


int main(int argc, char *argv[]){
    char* file_name;
    RTinitPopt(&argc,&argv,options,1,1,&file_name,NULL,"<input>",
                "test the LTSmin parser\n\nOptions");
    if (selection==0) Fatal(1,error,"please select one of the parsers");
    FILE *in;
    in = fopen( file_name, "r" );
    ltsmin_parse_env_t env=LTSminParseEnvCreate();
    LTSminStateVarIndex(env,"x");
    LTSminEdgeVarIndex(env,"action");
    Warning(info,"parsing...");
    ltsmin_parse_stream(selection,env,stream_input(in));
    Warning(info,"...done");
    return 0;
}
