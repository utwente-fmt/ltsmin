// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <fnmatch.h>
#include <stdio.h>
#include <string.h>

#include <hre/dir_ops.h>
#include <hre-io/user.h>
#include <util-lib/string-map.h>

/**************************************************************************/
/* auxiliary functions                                                    */
/**************************************************************************/

typedef struct copy_context {
    archive_t src;
    archive_t dst;
    string_map_t encode;
    int bs;
} *copy_context_t;

static int copy_item(void*arg,int id,const char*name){
    (void)id;
    copy_context_t ctx=(copy_context_t)arg;
    Print(info,"copying %s",name);
    char*compression=SSMcall(ctx->encode,name);
    Print(debug,"compression method is %s",compression);
    stream_t is=arch_read(ctx->src,(char*)name);
    stream_t os=arch_write_apply(ctx->dst,(char*)name,compression);
    char buf[ctx->bs];
    for(;;){
        int len=stream_read_max(is,buf,ctx->bs);
        if (len) stream_write(os,buf,len);
        if(len<ctx->bs) break;
    }
    stream_close(&is);
    stream_close(&os);
    return 0;
}

static void archive_copy(archive_t src,archive_t dst,string_map_t encode,int blocksize,char*pattern){
    struct arch_enum_callbacks cb={.new_item=copy_item};
    struct copy_context ctx;
    ctx.encode=encode;
    ctx.src=src;
    ctx.dst=dst;
    ctx.bs=blocksize;
    arch_enum_t e=arch_enum(src,pattern);
    if (arch_enumerate(e,&cb,&ctx)){
        Abort("unexpected non-zero return");
    }
    arch_enum_free(&e);
}

/**************************************************************************/
/* global variables and options                                           */
/**************************************************************************/

typedef enum {
    GCF_CREATE=1,
    GCF_EXTRACT=2,
    GCF_LIST=3,
    GCF_COPY=4,
    GCF_COMPRESS=5,
    GCF_DECOMPRESS=6,
    GCF_TO_ZIP=7,
    ZIP_TO_GCF=8
} gcf_op_t;

static char* gcf_policy="gzip";
static char* zip_policy="";
static int blocksize=32768;
static int blockcount=32;
static int operation=0;
static int force=0;
static string_map_t compression_policy=NULL;
static string_map_t coding_policy=NULL;
static char* outputdir=NULL;
static int keep=0;

static struct poptOption parameters[] = {
    { "force",'f' ,  POPT_ARG_VAL , &force , 1 , "force creation of a directory for output" , NULL },
    { "block-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blocksize , 0 , "set the size of a block in bytes" , "<bytes>" },
    { "cluster-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blockcount , 0 , "set the number of blocks in a cluster" , "<blocks>"},
    { "compression",'z',POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT,
        &gcf_policy,0,"set the compression policy used in the archive","<policy>"},
    { "keep",'k' , POPT_ARG_VAL , &keep , 1 , "keep original after (de)compression" , NULL },
#ifdef HAVE_ZIP_H
    { "zip-code",0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT,
        &zip_policy,0,"set the compression policy used when writing a ZIP archive","<policy>"},
#endif
    { "output", 'o' , POPT_ARG_STRING , &outputdir , 0, "change the extraction directory" , NULL },
    POPT_TABLEEND
};

static  struct poptOption options[] = {
    { "create", 0 , POPT_ARG_VAL , &operation , GCF_CREATE , "create a new archive" , NULL },
    { "extract", 'x', POPT_ARG_VAL , &operation , GCF_EXTRACT , "extract files from an archive" , NULL },
    { "list",'l', POPT_ARG_VAL , &operation , GCF_LIST , "list the files in the archive" , NULL },
    { "copy" , 0 , POPT_ARG_VAL , &operation , GCF_COPY , "create a new archive by copying an existing archive" , NULL },
    { "compress" , 'c' , POPT_ARG_VAL , &operation , GCF_COMPRESS , "compress all arguments" , NULL},
    { "decompress" , 'd' , POPT_ARG_VAL , &operation , GCF_DECOMPRESS , "decompress all arguments" , NULL},
#ifdef HAVE_ZIP_H
    { "to-zip" , 0 , POPT_ARG_VAL , &operation , GCF_TO_ZIP , "copy given GCF archive to a ZIP archive" , NULL},
    { "from-zip" , 0 , POPT_ARG_VAL , &operation , ZIP_TO_GCF , "copy given ZIP archive to a GCF archive" , NULL},    
#endif
    { NULL , 0 , POPT_ARG_INCLUDE_TABLE , parameters , 0 , "Options", NULL },
    POPT_TABLEEND
};

/**************************************************************************/
/* extract from archive                                                   */
/**************************************************************************/

static void gcf_extract(){
    char *gcf_name=HREnextArg();
    if (gcf_name==NULL) {
        Abort("missing <gcf archive> argument");
    }
    archive_t arch=arch_gcf_read(raf_unistd(gcf_name));
    archive_t dir;
    if (outputdir) {
        dir=arch_dir_create(outputdir,blocksize,force?DELETE_ALL:DELETE_NONE);
    } else {
        dir=arch_dir_open(".",blocksize);
    }
    char*pattern=HREnextArg();
    do {
        archive_copy(arch,dir,NULL,blocksize,pattern);
    } while((pattern=HREnextArg()));
    arch_close(&dir);
    arch_close(&arch);
}

/**************************************************************************/
/* create archive                                                         */
/**************************************************************************/

static void gcf_create(){
    char *gcf_name=HREnextArg();
    if (gcf_name==NULL) {
        Abort("missing <gcf archive> argument");
    }
    archive_t arch=arch_gcf_create(raf_unistd(gcf_name),blocksize,blocksize*blockcount,0,1);
    char*file;
    while((file=HREnextArg())){
        if (is_a_file(file)){
            stream_t is=file_input(file);
            stream_t os=arch_write_apply(arch,file,SSMcall(compression_policy,file));
            char buf[blocksize];
            for(;;){
                int len=stream_read_max(is,buf,blocksize);
                if (len) stream_write(os,buf,len);
                if(len<blocksize) break;
            }
            stream_close(&is);
            stream_close(&os);
        } else {
            Abort("cannot add %s because it is not a file",file);
        }
    }
    arch_close(&arch);
}

/**************************************************************************/
/* copy archive                                                           */
/**************************************************************************/

static void gcf_copy(){
    char* source=HREnextArg();
    if (source==NULL) {
        Abort("missing <source> argument");
    }
    char* target=HREnextArg();
    if (target==NULL) {
        Abort("missing <target> argument");
    }
    if (HREnextArg()){
        Abort("too many arguments");
    }
    archive_t arch_in=arch_gcf_read(raf_unistd(source));
    archive_t arch_out=arch_gcf_create(raf_unistd(target),blocksize,blocksize*blockcount,0,1);
    archive_copy(arch_in,arch_out,compression_policy,blocksize,NULL);
    arch_close(&arch_in);
    arch_close(&arch_out);
}

/**************************************************************************/
/* copy archive to ZIP                                                    */
/**************************************************************************/

static void gcf_copy_zip(){
    char* source=HREnextArg();
    if (source==NULL) {
        Abort("missing <source> argument");
    }
    char* target=HREnextArg();
    if (target==NULL) {
        Abort("missing <target> argument");
    }
    if (HREnextArg()){
        Abort("too many arguments");
    }
    archive_t arch_in=arch_gcf_read(raf_unistd(source));
    arch_zip_create(target,blocksize,coding_policy,arch_in);
    arch_close(&arch_in);
}

/**************************************************************************/
/* (de)compress files and directories                                     */
/**************************************************************************/

static void gcf_compress(){
    char*source;
    char target[LTSMIN_PATHNAME_MAX];
    while((source=HREnextArg())){
        if (is_a_file(source)){
            sprintf(target,"%s.gzf",source);
            stream_t is=file_input(source);
            stream_t os=file_output(target);
            char *code=SSMcall(compression_policy,source);
            DSwriteS(os,code);
            os=stream_add_code(os,code);
            char buf[blocksize];
            for(;;){
                int len=stream_read_max(is,buf,blocksize);
                if (len) stream_write(os,buf,len);
                if(len<blocksize) break;
            }
            stream_close(&is);
            stream_close(&os);
            if (!keep) recursive_erase(source);
        } else if (is_a_dir(source)){
            sprintf(target,"%s.gcf",source);
            archive_t arch_in=arch_dir_open(source,blocksize);
            archive_t arch_out=arch_gcf_create(raf_unistd(target),blocksize,blocksize*blockcount,0,1);
            archive_copy(arch_in,arch_out,compression_policy,blocksize,NULL);
            arch_close(&arch_in);
            arch_close(&arch_out);
            if (!keep) recursive_erase(source);
        } else {
            Abort("source %s is neither a file nor a directory",source);
        }
    }
}

static int has_extension(const char*name,const char*extension){
    size_t len=strlen(name);
    return len>strlen(extension) && strcmp(name+(len-4),extension)==0;
}

static void gcf_decompress(){
    char*source;
    char target[LTSMIN_PATHNAME_MAX];
    while((source=HREnextArg())){
        if (has_extension(source,".gzf")){
            strncpy(target,source,strlen(source)-4);
            target[strlen(source)-4]=0;
            stream_t is=file_input(source);
            stream_t os=file_output(target);
            char *code=DSreadSA(is);
            is=stream_add_code(is,code);
            char buf[blocksize];
            for(;;){
                int len=stream_read_max(is,buf,blocksize);
                if (len) stream_write(os,buf,len);
                if(len<blocksize) break;
            }
            stream_close(&is);
            stream_close(&os);
            if (!keep) recursive_erase(source);
        } else if (has_extension(source,".gcf")||has_extension(source,".zip")){
            strncpy(target,source,strlen(source)-4);
            target[strlen(source)-4]=0;
            archive_t arch_in;
            if (has_extension(source,".gcf")){
                arch_in=arch_gcf_read(raf_unistd(source));
            } else {
                arch_in=arch_zip_read(source,blocksize);
            }
            archive_t arch_out=arch_dir_create(target,blocksize,force?DELETE_ALL:DELETE_NONE);
            archive_copy(arch_in,arch_out,NULL,blocksize,NULL);
            arch_close(&arch_in);
            arch_close(&arch_out);
            if (!keep) recursive_erase(source);
        } else {
            Abort("source %s does not have known extension",source);
        }
    }
}

/**************************************************************************/
/* list archive                                                           */
/**************************************************************************/

struct list_count {
    uint64_t total_orig;
    uint64_t total_compressed;
    int files;
};

static int list_item(void*arg,int no,archive_item_t item){
    (void)no;
    ((struct list_count*)arg)->files++;
    ((struct list_count*)arg)->total_orig+=item->length;
    ((struct list_count*)arg)->total_compressed+=item->compressed;
    if (item->code==NULL || strlen(item->code)==0) {
        if (item->compressed<item->length && item->compressed>0) {
            Printf(infoShort,"%12zd %12zd %s\n",(ssize_t)item->length,(ssize_t)item->compressed,item->name);
        } else {
            Printf(infoShort,"%12zd %12s %s\n",(ssize_t)item->length,"",item->name);
        }
    } else {
        if (item->compressed==item->length || item->compressed==0) {
            Printf(infoShort,"%12zd %12s %s (%s)\n",(ssize_t)item->length,"",item->name,item->code);
        } else {
            Printf(infoShort,"%12zd %12zd %s (%s)\n",(ssize_t)item->length,(ssize_t)item->compressed,item->name,item->code);
        }
    }
    return 0;
}

static void gcf_list(){
    char *gcf_name=HREnextArg();
    if (gcf_name==NULL) {
        Abort("missing <gcf archive> argument");
    }
    if (HREnextArg()){
        Abort("too many arguments");
    }
    raf_t raf=raf_unistd(gcf_name);
    archive_t gcf=arch_gcf_read(raf);
    struct arch_enum_callbacks cb={.stat=list_item};
    struct list_count totals={0,0,0};
    Printf(infoShort,"Archive %s contains:\n",gcf_name);
    Printf(infoShort," stream size   compressed stream name (compression)\n");
    arch_enum_t e=arch_enum(gcf,NULL);
    if (arch_enumerate(e,&cb,&totals)){
        Abort("unexpected non-zero return");
    }
    Printf(infoShort,"totals:\n");
    Printf(infoShort,"%12zd %12zd files: %d (%3.2f%%)\n",
           (ssize_t)totals.total_orig,(ssize_t)totals.total_compressed,totals.files,
        100.0*((float)(totals.total_orig-totals.total_compressed))/((float)totals.total_orig));
    arch_enum_free(&e);
    uint64_t gcf_size=raf_size(raf);
    Printf(infoShort,"gcf file size%12zu (%3.2f%% overhead, %3.2f%% compression)\n",(size_t)gcf_size,
        100.0*((float)(gcf_size-totals.total_compressed))/((float)totals.total_compressed),
        100.0*(((float)totals.total_orig)-((float)gcf_size))/((float)totals.total_orig));
    arch_close(&gcf);
}

static void zip_copy_gcf(){
    char* source=HREnextArg();
    if (source==NULL) {
        Abort("missing <source> argument");
    }
    char* target=HREnextArg();
    if (target==NULL) {
        Abort("missing <target> argument");
    }
    if (HREnextArg()){
        Abort("too many arguments");
    }
    archive_t arch_in=arch_zip_read(source,65536);
    archive_t arch_out=arch_gcf_create(raf_unistd(target),blocksize,blocksize*blockcount,0,1);
    archive_copy(arch_in,arch_out,compression_policy,blocksize,NULL);
    arch_close(&arch_in);
    arch_close(&arch_out);
}

/**************************************************************************/
/* main                                                                   */
/**************************************************************************/

int main(int argc, char *argv[]){
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Tool for creating and extracting GCF archives\n\nOperations");
    HREinitStart(&argc,&argv,0,-1,NULL,"<operation> <arguments>");
    compression_policy=SSMcreateSWP(gcf_policy);
    coding_policy=SSMcreateSWP(zip_policy);
    switch(operation){
    case GCF_CREATE:
        gcf_create();
        break;
    case GCF_EXTRACT:
        gcf_extract();
        break;
     case GCF_LIST:
        gcf_list();
        break;
    case GCF_COPY:
        gcf_copy();
        break;
    case GCF_COMPRESS:
        gcf_compress();
        break;
    case GCF_DECOMPRESS:
        gcf_decompress();
        break;
    case GCF_TO_ZIP:
        gcf_copy_zip();
        break;
    case ZIP_TO_GCF:
        zip_copy_gcf();
        break;
    default:
        Abort("Illegal arguments, type gcf -h for help");
    }
    HREexit(EXIT_SUCCESS);
}


