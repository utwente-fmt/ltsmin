#include "archive.h"
#include <stdio.h>
#include "runtime.h"

void show(char *fmt,char*name){
	printf(fmt,name);
}

void new_item(void*arg,int id,char*name){
	printf("content of %s(%d):\n",name,id);
}

void end_item(void*arg,int id){
	printf("end of item %d\n",id);
}

void cp_data(void*arg,int id,void*data,size_t len){
	fwrite(data,1,len,stdout);
}

int main(int argc, char *argv[]){
	struct archive_enum cb;
	cb.new_item=new_item;
	cb.end_item=end_item;
	cb.data=cp_data;
	runtime_init();
	set_label("dir test");
	archive_t a=arch_dir(argv[1]);
	arch_search(a,NULL,show,"item %s\n");
	arch_enum(a,NULL,&cb,NULL);
	arch_close(&a);
}


