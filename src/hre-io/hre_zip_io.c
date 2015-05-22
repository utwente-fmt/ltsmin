// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#ifdef HAVE_ZIP_H
#include <zip.h>
#endif

/* #include <hre/unix.h> should be used when reading zip read-only... */
#include <hre-io/arch_object.h>
#include <hre-io/stream_object.h>
#include <hre-io/user.h>
#include <hre/stringindex.h>

#ifndef HAVE_ZIP_H

archive_t arch_zip_read(const char* name,int buf){
    (void)name; (void)buf;
    Abort("This build does not include ZIP support.");
}

void arch_zip_create(const char* name,int buf_size,string_map_t policy,archive_t contents){
    (void)name; (void)buf_size; (void)policy; (void)contents;
    Abort("This build does not include ZIP support.");
}

#else


#ifdef LIBZIP_VERSION
#if LIBZIP_VERSION_MAJOR > 0 || (LIBZIP_VERSION_MAJOR == 0 && LIBZIP_VERSION_MINOR >= 10)
#define zip_file_no_t zip_int64_t 
#else
#error "libzip version not supported"
#endif
#else
#define zip_file_no_t int
#endif

struct archive_s {
    struct archive_obj procs;
    struct zip *archive;
    string_index_t stream_index;
    int buf;
};

struct stream_s {
    struct stream_obj procs;
    struct zip_file *file;
};

static void zip_stream_read_close(stream_t *s){
    zip_fclose((*s)->file);
    free(*s);
    *s=NULL;
}

static size_t zip_stream_read_max(stream_t stream,void*buf,size_t count){
    ssize_t res=zip_fread(stream->file,buf,count);
    if (res<0) Abort("zip_fread failed");
    return (size_t)res;
}

static stream_t zip_read_stream(archive_t arch,zip_file_no_t  id){
    stream_t s=(stream_t)HREmalloc(NULL,sizeof(struct stream_s));
    stream_init(s);
    s->procs.close=zip_stream_read_close;
    s->procs.read_max=zip_stream_read_max;
    s->procs.read=stream_default_read;
    s->file=zip_fopen_index(arch->archive,id,0);
    return stream_buffer(s,arch->buf);
}

static stream_t hre_zip_read(archive_t archive,char *name){
    int id=SIlookup(archive->stream_index,name);
    if (id==SI_INDEX_FAILED) Abort("stream %s not found",name);
    stream_t s=zip_read_stream(archive,id);
    const char*tmp=zip_get_file_comment(archive->archive,id,NULL,0);
    if (tmp!=NULL){
        s=stream_add_code(s,tmp);
    }
    return s;
}

static stream_t hre_zip_read_raw(archive_t archive,char *name,char**code){
    int id=SIlookup(archive->stream_index,name);
    if (id==SI_INDEX_FAILED) Abort("stream %s not found",name);
    stream_t s=zip_read_stream(archive,id);
    if (code) {
        const char*tmp=zip_get_file_comment(archive->archive,id,NULL,0);
        if (tmp!=NULL){
            *code=strdup(tmp);
        } else {
            *code=NULL;
        }
    }
    return s;
}

static void hre_zip_close(archive_t *archive){
    if (zip_close((*archive)->archive) < 0) {
        Abort("cannot close zip archive: %s\n",zip_strerror((*archive)->archive));
    }
    free(*archive);
    *archive=NULL;
}

struct arch_enum {
    struct archive_enum_obj procs;
    archive_t archive;
    zip_file_no_t next;
    zip_file_no_t count;
};

static int zip_enumerate(arch_enum_t e,struct arch_enum_callbacks *cb,void*arg){
    if (cb->data!=NULL) Abort("cannot enumerate data");
    while(e->next<e->count){
        struct zip_stat sb;
        int res=zip_stat_index(e->archive->archive,e->next,0,&sb);
        if (res<0) {
            Abort("cannot stat zip archive: %s\n",zip_strerror(e->archive->archive));
        }
        if(cb->new_item) {
            res=cb->new_item(arg,e->next,sb.name);
            if (res) {
                e->next++;
                return res;
            }
        }
        if(cb->stat) {
            struct archive_item_s item;
            item.name=sb.name;
            item.code=zip_get_file_comment(e->archive->archive,e->next,NULL,0);
            item.length=sb.size;
            item.compressed=sb.comp_size;
            res=cb->stat(arg,e->next,&item);
            if (res) {
                e->next++;
                return res;
            }
            
        }
        e->next++;
    }
    return 0;
}

static void zip_enum_free(arch_enum_t *e){
    free(*e);
    *e=NULL;
}

static arch_enum_t zip_enum(archive_t archive,char *regex){
    if (regex!=NULL) Abort("regex not supported");
    arch_enum_t e=(arch_enum_t)HREmalloc(NULL,sizeof(struct arch_enum));
    e->procs.enumerate=zip_enumerate;
    e->procs.free=zip_enum_free;
    e->archive=archive;
    e->next=0;
#ifdef LIBZIP_VERSION
    e->count=zip_get_num_entries(archive->archive,0);
#else
    e->count=zip_get_num_files(archive->archive);
#endif
    return e;
}

static int zip_contains(archive_t archive,char *name){
    int id=SIlookup(archive->stream_index,name);
    return (id!=SI_INDEX_FAILED);
}

archive_t arch_zip_read(const char* name,int buf){
    archive_t arch=(archive_t)HREmalloc(NULL,sizeof(struct archive_s));
    arch_init(arch);
    int err;
    arch->archive=zip_open(name,ZIP_CHECKCONS,&err);
    if (arch->archive==NULL){
        char errstr[1024];
        zip_error_to_str(errstr, sizeof(errstr), err, errno);
        Abort("cannot open zip archive `%s': %s\n",name , errstr);
    }
    arch->stream_index=SIcreate();
#ifdef LIBZIP_VERSION
    int count=zip_get_num_entries(arch->archive,0);
#else
    int count=zip_get_num_files(arch->archive);
#endif
    for(int i=0;i<count;i++){
        struct zip_stat sb;
        int res=zip_stat_index(arch->archive,i,0,&sb);
        if (res<0) {
            Abort("cannot stat zip archive: %s\n",zip_strerror(arch->archive));
        }
        SIputAt(arch->stream_index,sb.name,i);
        Print(infoShort,"stream %d is %s",i,sb.name);
    }
    arch->procs.contains=zip_contains;
    arch->procs.read=hre_zip_read;
    arch->procs.read_raw=hre_zip_read_raw;
    arch->procs.enumerator=zip_enum;
    arch->procs.close=hre_zip_close;
    arch->buf=buf;
    return arch;
}

typedef struct copy_zip_context {
    archive_t src;
    char*name;
    struct zip *dst;
    stream_t is;
    long long int rd;
    string_map_t policy;
} *copy_zip_context_t;

static copy_zip_context_t copy_zip_setup(struct zip *dst,archive_t src,const char*name){
    copy_zip_context_t ctx=RT_NEW(struct copy_zip_context);
    ctx->src=src;
    ctx->dst=dst;
    ctx->name=HREstrdup(name);
    ctx->is=NULL;
    ctx->rd=0;
    return ctx;
}

#ifdef LIBZIP_VERSION
#if LIBZIP_VERSION_MAJOR > 0 || (LIBZIP_VERSION_MAJOR == 0 && LIBZIP_VERSION_MINOR >= 10)
#define zip_file_no_t zip_int64_t 
static zip_int64_t
copy_zip_function(void *state, void *data, zip_uint64_t len, enum zip_source_cmd cmd)
#else
#error "libzip version not supported"
#endif
#else
#define zip_file_no_t int
static ssize_t
copy_zip_function(void *state, void *data, size_t len, enum zip_source_cmd cmd)
#endif
{
    copy_zip_context_t ctx=(copy_zip_context_t)state;
    switch(cmd){
    case ZIP_SOURCE_OPEN:
        Debug("open %s",ctx->name);
        stream_t stream=arch_read(ctx->src,ctx->name);
        char *code=SSMcall(ctx->policy,ctx->name);
        ctx->is=stream_add_decode(stream,code);
        return 0;
    case ZIP_SOURCE_READ:
        if (ctx->is!=NULL) {
            int res=stream_read_max(ctx->is,data,len);
            ctx->rd+=res;
            //Debug("read %d/%d from %s (%lld)",res,len,ctx->name,ctx->rd);
            if (res<(ssize_t)len) {
                // workaround:
                // reading from empty stream returns data.
                // libzip does not stop reading if res < len, but only is res=0!
                Debug("close %s",ctx->name);
                stream_close(&ctx->is);
            }
            return res;
        } else {
            return 0;
        }
    case ZIP_SOURCE_CLOSE:
        if (ctx->is!=NULL) {
            Debug("close %s",ctx->name);
            stream_close(&ctx->is);
        }
        return 0;
    case ZIP_SOURCE_STAT:
    {
        struct zip_stat *st=(struct zip_stat *)data;
        zip_stat_init(st);
        st->size=0;
        st->mtime=time(NULL);
        return sizeof(struct zip_stat);
    }
    case ZIP_SOURCE_ERROR:
        Abort("error without error");
    case ZIP_SOURCE_FREE:
        RTfree(ctx->name);
        RTfree(ctx);
        return 0;
    default:
        Abort("missing case");
    }
}

static int copy_zip_item(void*arg,int id,const char*name){
    (void)id;
    copy_zip_context_t ctx=(copy_zip_context_t)arg;
    Print(infoLong,"queueing %s",name);
    copy_zip_context_t new_ctx=copy_zip_setup(ctx->dst,ctx->src,name);
    struct zip_source* src=zip_source_function(ctx->dst,copy_zip_function,new_ctx);
    zip_file_no_t file_no=zip_add(ctx->dst,name,src);
    if (file_no<0){
        Abort("cannot add to zip: %s\n", zip_strerror(ctx->dst));
    }
    char *code=SSMcall(ctx->policy,name);
    if (zip_set_file_comment(ctx->dst,file_no,code,strlen(code))<0){
        Abort("cannot set encoding as comment: %s\n", zip_strerror(ctx->dst));
    }
    return 0;
}


void arch_zip_create(const char* target,int buf_size,string_map_t policy,archive_t arch_in){
    (void)buf_size;
    // clean old zip if any.
    if (unlink(target)&&errno!=ENOENT){
        AbortCall("could not remove existing %s",target);
    }
    int err;
    struct zip *za;
    if ((za=zip_open(target, ZIP_CREATE , &err)) == NULL) {
        char errstr[1024];
        zip_error_to_str(errstr, sizeof(errstr), err, errno);
        Abort("cannot open zip archive `%s': %s\n",target , errstr);
    }
    // setup copy operations.
    struct arch_enum_callbacks cb={.new_item=copy_zip_item};
    struct copy_zip_context ctx;
    ctx.policy=policy;
    ctx.src=arch_in;
    ctx.dst=za;
    ctx.is=NULL;
    ctx.name=NULL;
    arch_enum_t e=arch_enum(arch_in,NULL);
    if (arch_enumerate(e,&cb,&ctx)){
        Abort("unexpected non-zero return");
    }
    arch_enum_free(&e);
    // execute copy.
    if (zip_close(za) < 0) {
        Abort("cannot write zip archive `%s': %s\n", target, zip_strerror(za));
    }
}

#endif


