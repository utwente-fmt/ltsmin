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
stream_t arch_write(archive_t archive,char *name,char*code,int hdr){
	stream_t s=archive->procs.write(archive,name);
	if (code==NULL) {
		return s;
	} else {
		if (hdr) {
			return stream_setup(s,code);
		} else {
			return stream_add_code(s,code);
		}
	}
	return s;
}


arch_enum_t arch_enum(archive_t archive,char *regex){
	return archive->procs.enumerator(archive,regex);
}

void arch_close(archive_t *archive){
	(*archive)->procs.close(archive);
}

struct arch_enum {
	struct archive_enum_obj procs;
};

int arch_enumerate(arch_enum_t enumerator,struct arch_enum_callbacks *cb,void*arg){
	return enumerator->procs.enumerate(enumerator,cb,arg);
}
void arch_enum_free(arch_enum_t* enumerator){
	(*enumerator)->procs.free(enumerator);
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
arch_enum_t arch_illegal_enum(archive_t arch,char*regex){
	(void)arch;(void)regex;
	Fatal(1,error,"This archive cannot create an(other) enumerator.");
	return NULL;
}
void arch_illegal_close(archive_t *arch){
	(void)arch;
	Fatal(0,error,"illegal close on archive");
}
void arch_init(archive_t arch){
	arch->procs.read=arch_illegal_read;
	arch->procs.write=arch_illegal_write;
	arch->procs.enumerator=arch_illegal_enum;
	arch->procs.close=arch_illegal_close;
}



