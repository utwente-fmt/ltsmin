#include "archive.h"
#include <stdio.h>
#include "runtime.h"

static archive_t b;
static stream_t s;

void show(char *fmt,char*name){
	printf(fmt,name);
}

void new_item(void*arg,int id,char*name){
	printf("content of %s(%d):\n",name,id);
	s=arch_write(b,name);
}

void end_item(void*arg,int id){
	printf("end of item %d\n",id);
	stream_close(&s);
}

void cp_data(void*arg,int id,void*data,size_t len){
	printf("writing %d\n",id);
	stream_write(s,data,len);
}

int main(int argc, char *argv[]){
	struct archive_enum cb;
	cb.new_item=new_item;
	cb.end_item=end_item;
	cb.data=cp_data;
	runtime_init();
	set_label("gsf test");
	archive_t a=arch_dir(argv[1]);
	b=arch_gsf_write(stream_output(fopen(argv[2],"w")));
	arch_search(a,NULL,show,"item %s\n");
	arch_enum(a,NULL,&cb,NULL);
	arch_close(&a);
	arch_close(&b);
}


