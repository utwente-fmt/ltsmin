// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <hre/dir_ops.h>
#include <hre-io/arch_object.h>
#include <hre-io/user.h>

struct archive_s {
    struct archive_obj procs;
    int buf;
    char dir[LTSMIN_PATHNAME_MAX];
};

static stream_t dir_read(archive_t archive,char *name){
    char fname[LTSMIN_PATHNAME_MAX*2+2];
    sprintf(fname,"%s/%s",archive->dir,name);
    return stream_buffer(file_input(fname),archive->buf);
}

static stream_t dir_read_raw(archive_t archive,char *name,char** code){
    char fname[LTSMIN_PATHNAME_MAX*2+2];
    sprintf(fname,"%s/%s",archive->dir,name);
    if (code) *code=NULL;
    return stream_buffer(file_input(fname),archive->buf);
}

static stream_t dir_write(archive_t archive,char *name,char*code){
    char fname[LTSMIN_PATHNAME_MAX*2+2];
    sprintf(fname,"%s/%s",archive->dir,name);
    if (code) Abort("native compression unsupported, use transparent compression instead.");
    return stream_buffer(file_output(fname),archive->buf);
}

static void dir_close(archive_t *archive){
    RTfree(*archive);
    *archive=NULL;
};

struct arch_enum {
    struct archive_enum_obj procs;
    archive_t archive;
    dir_enum_t e;
    uint32_t next_stream;
};

static int dir_enumerate(arch_enum_t e,struct arch_enum_callbacks *cb,void*arg){
    if (cb->data!=NULL) Abort("cannot enumerate data");
    if (e->e==NULL) return 0;
    char*name;
    while((name=get_next_dir(e->e))){
        if(cb->new_item) {
            int res=cb->new_item(arg,e->next_stream,name);
            e->next_stream++;
            if (res) return res;
        } else {
            e->next_stream++;
        }
    }
    e->e=NULL;
    return 0;
}

static void dir_enum_free(arch_enum_t *e){
    if ((*e)->e) {
        del_dir_enum((*e)->e);
    }
    RTfree(*e);
    *e=NULL;
}

static arch_enum_t dir_enum(archive_t archive,char *regex){
    if (regex!=NULL) Abort("regex not supported");
    arch_enum_t e=(arch_enum_t)RTmalloc(sizeof(struct arch_enum));
    e->e=get_dir_enum(archive->dir);
    e->procs.enumerate=dir_enumerate;
    e->procs.free=dir_enum_free;
    e->archive=archive;
    e->next_stream=0;
    return e;
}

static int dir_contains(archive_t archive,char *name){
    char fname[LTSMIN_PATHNAME_MAX*2+2];
    sprintf(fname,"%s/%s",archive->dir,name);
    return is_a_file(fname);
}


archive_t arch_dir_create(char*dirname,int buf,int del){
    archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
    arch_init(arch);
    if(create_empty_dir(dirname,del)){
        AbortCall("could not create or clear directory %s",dirname);
    }
    strncpy(arch->dir,dirname,LTSMIN_PATHNAME_MAX-1);
    arch->dir[LTSMIN_PATHNAME_MAX-1]=0;
    arch->procs.contains=dir_contains;
    arch->procs.read=dir_read;
    arch->procs.read_raw=dir_read_raw;
    arch->procs.write=dir_write;
    arch->procs.write_raw=dir_write;
    arch->procs.enumerator=dir_enum;
    arch->procs.close=dir_close;
    arch->buf=buf;
    return arch;
}

archive_t arch_dir_open(char*dirname,int buf){
    archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
    arch_init(arch);
    if(!is_a_dir(dirname)){
        AbortCall("directory %s does not exist",dirname);
    }
    strncpy(arch->dir,dirname,LTSMIN_PATHNAME_MAX-1);
    arch->dir[LTSMIN_PATHNAME_MAX-1]=0;
    arch->procs.contains=dir_contains;
    arch->procs.read=dir_read;
    arch->procs.read_raw=dir_read_raw;
    arch->procs.write=dir_write;
    arch->procs.write_raw=dir_write;
    arch->procs.enumerator=dir_enum;
    arch->procs.close=dir_close;
    arch->buf=buf;
    return arch;
}
