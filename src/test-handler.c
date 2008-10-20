#include "runtime.h"

void test_ok(){
	Warning(NULL,"ok");
}

void test_throw(){
	Fatal(12,NULL,"oops\n");
}

int pass=1;
int save=1;

void test_catch(){
	jmp_try
		if(pass) test_ok(); else test_throw();
	jmp_catch(e)
		Warning(NULL,"caught %d",e);
		if(save){
			Warning(NULL,"continuing");
		} else {
			throw_int(e+3);
		}
	jmp_end
}

void run_thread(void (*proc)()){
	jmp_try
		proc();
	jmp_catch(e)
		Warning(error,"exception %d\n",e);
	jmp_end
}

int main(int argc,char**argv){
	runtime_init();
	set_label("handler test");
	run_thread(test_catch);
	pass=0;
	run_thread(test_catch);
	save=0;
	run_thread(test_catch);
	test_catch();
	return 1;
}

