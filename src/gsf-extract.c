#include "archive.h"
#include <stdio.h>
#include "runtime.h"

static char *code;
static archive_t dir=NULL;
static stream_t stream[1024];

int new_item(void*arg,int id,char*name){
	printf("item %d is %s\n",id,name);
	if (id>=1024) {
		Fatal(1,error,"sorry, this version is limited to 1024 streams");
	}
	stream[id]=arch_write(dir,name,code);
	return 0;
}

int end_item(void*arg,int id){
	printf("end of item %d\n",id);
	stream_close(&stream[id]);
	return 0;
}

int cp_data(void*arg,int id,void*data,size_t len){
	printf("data for %d\n",id);
	stream_write(stream[id],data,len);
	return 0;
}

int main(int argc, char *argv[]){
	struct arch_enum_callbacks cb;
	cb.new_item=new_item;
	cb.end_item=end_item;
	cb.data=cp_data;
	runtime_init_args(&argc,&argv);
	archive_t gsf;

	int blocksize=prop_get_U32("bs",4096);
	char*in=prop_get_S("in",NULL);
	if(in){
		gsf=arch_gsf_read(file_input(in));
	} else {
		gsf=arch_gsf_read(stream_input(stdin));
	}
	if ((code=prop_get_S("code",NULL))){
		Warning(info,"applying %s to output streams",code);
	}
	dir=arch_fmt(prop_get_S("fmt","%s"),file_input,file_output,blocksize);
	arch_enum_t e=arch_enum(gsf,NULL);
	if (arch_enumerate(e,&cb,NULL)){
		Warning(info,"unexpected non-zero return");
	}
	arch_enum_free(&e);
	arch_close(&dir);
	arch_close(&gsf);
}


