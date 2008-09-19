#include "archive.h"
#include <stdio.h>
#include "runtime.h"


static archive_t b;
static stream_t stream[1024];

void new_item(void*arg,int id,char*name){
	//printf("item %d is %s\n",id,name);
	stream[id]=arch_write(b,name);
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
	runtime_init();
	set_label("gsf2dir");
	archive_t a;
	if(argc>2){
		a=arch_gsf_read(stream_input(fopen(argv[1],"r")));
		b=arch_dir(argv[2],4096);
	} else {
		a=arch_gsf_read(stream_input(stdin));
		b=arch_dir(argv[1],4096);
	}
	arch_enum(a,NULL,&cb,NULL);
	arch_close(&a);
	arch_close(&b);
}


