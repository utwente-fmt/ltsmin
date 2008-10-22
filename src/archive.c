#include "arch_object.h"
#include "runtime.h"

struct archive_s {
	struct archive_obj procs;
};

int arch_readable(archive_t archive){
	return (archive->procs.read!=arch_illegal_read);
}
stream_t arch_read(archive_t archive,char *name,char*code){
	stream_t s=archive->procs.read(archive,name);
	if (code==NULL) {
		return s;
	} else {
		return stream_setup(s,code);
	}
}

int arch_writable(archive_t archive){
	return (archive->procs.write!=arch_illegal_write);
}
stream_t arch_write(archive_t archive,char *name,char*code){
	stream_t s=archive->procs.write(archive,name);
	if (code==NULL) {
		return s;
	} else {
		return stream_setup(s,code);
	}
}

int arch_searchable(archive_t archive){
	return (archive->procs.list!=arch_illegal_list);
}
void arch_search(archive_t archive,char *regex,string_enum_t cb,void*arg){
	archive->procs.list(archive,regex,cb,arg);
}

int arch_enumerable(archive_t archive){
	return (archive->procs.play!=arch_illegal_play);
}
void arch_enum(archive_t archive,char *regex,struct archive_enum *cb,void*arg){
	archive->procs.play(archive,regex,cb,arg);
}

void arch_close(archive_t *archive){
	(*archive)->procs.close(archive);
}

struct cb_arg {
	int id;
	archive_t arch;
	struct archive_enum *cb;
	void *arg;
};

static void arch_copy(void*arg,char*name){
#define cba ((struct cb_arg *)arg)
	char* code=cba->cb->new_item(cba->arg,cba->id,name);
	stream_t s=arch_read(cba->arch,name,code);
	char buf[4096];
	for(;;){
		int len=stream_read_max(s,buf,4096);
		if(len==0) break;
		cba->cb->data(cba->arg,cba->id,buf,len);
	}
	stream_close(&s);
	cba->cb->end_item(cba->arg,cba->id);
	cba->id++;
#undef cba
}

void arch_play(archive_t arch,char*regex,struct archive_enum *cb,void*arg){
	struct cb_arg cba;
	cba.id=0;
	cba.arch=arch;
	cba.cb=cb;
	cba.arg=arg;
	arch->procs.list(arch,regex,arch_copy,&cba);
}

stream_t arch_illegal_read(archive_t arch,char*name){
	(void)arch;(void)name;
	Fatal(0,error,"illegal read on archive");
	return NULL;
}
stream_t arch_illegal_write(archive_t arch,char*name){
	(void)arch;(void)name;
	Fatal(0,error,"illegal write on archive");
	return NULL;
}
void arch_illegal_list(archive_t arch,char*regex,string_enum_t cb,void*arg){
	(void)arch;(void)regex;(void)cb;(void)arg;
	Fatal(0,error,"illegal list on archive");
}
void arch_illegal_play(archive_t arch,char*regex,struct archive_enum *cb,void*arg){
	(void)arch;(void)regex;(void)cb;(void)arg;
	Fatal(0,error,"illegal play on archive");
}
void arch_illegal_close(archive_t *arch){
	(void)arch;
	Fatal(0,error,"illegal close on archive");
}
void arch_init(archive_t arch){
	arch->procs.read=arch_illegal_read;
	arch->procs.write=arch_illegal_write;
	arch->procs.list=arch_illegal_list;
	arch->procs.play=arch_illegal_play;
	arch->procs.close=arch_illegal_close;
}



