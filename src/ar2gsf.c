#include "archive.h"
#include <stdio.h>
#include "runtime.h"

static archive_t gsf;
static stream_t stream[1024];

char* new_item(void*arg,int id,char*name){
	//printf("item %d is %s\n",id,name);
	if (id>=1024) {
		Fatal(1,error,"sorry, this version is limited to 1024 streams");
	}
	stream[id]=arch_write(gsf,name,NULL);
	return NULL;
}

void end_item(void*arg,int id){
	//printf("end of item %d\n",id);
	stream_close(&stream[id]);
}

void cp_data(void*arg,int id,void*data,size_t len){
	//printf("data for %d\n",id);
	stream_write(stream[id],data,len);
}


int main(int argc, char *argv[]){
	struct archive_enum cb;
	cb.new_item=new_item;
	cb.end_item=end_item;
	cb.data=cp_data;
	runtime_init_args(&argc,&argv);

	archive_t dir=NULL;
	char *ar;
	int blocksize=prop_get_U32("bs",4096);
	if ((ar=prop_get_S("gcf",NULL))){
		raf_t raf=raf_unistd(ar);
		dir=arch_gcf_read(raf);
	}
	if (dir==NULL && (ar=prop_get_S("dir",NULL))){
		dir=arch_dir(ar,blocksize);
	}
	if (dir==NULL && (ar=prop_get_S("fmt",NULL))){
		dir=arch_fmt(ar,file_input,file_output,blocksize);
	}
	if (dir==NULL) {
		Fatal(1,error,"please specify input with gcf=... , dir=... or fmt=...");
	}
	char*out=prop_get_S("out",NULL);
	Warning(info,"copying %s to %s",ar,out?out:"stdout");
	if(out){
		gsf=arch_gsf_write(file_output(out));
	} else {
		gsf=arch_gsf_write(stream_output(stdout));
	}
	arch_enum(dir,NULL,&cb,NULL);
	arch_close(&dir);
	arch_close(&gsf);
}


