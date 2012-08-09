#include <hre/config.h>

#include <unistd.h>
#include <sys/types.h>

#include <util-lib/bitset.h>

static void setclear(){
	int i;
	bitset_t set=bitset_create(16,16);

    printf("setting 5 and 60\n");
	bitset_set(set,5);
	bitset_set(set,60);
	for(i=0;i<80;i++) printf("%s",bitset_test(set,i)?"1":"0");
	printf("\n");
	
	int N=10;
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("set %d\n",N);
    bitset_set(set,N);
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("clear %d\n",N);
	bitset_clear(set,N);
	printf("%d in set is %d\n",N,bitset_test(set,N));

	N=100;
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("set %d\n",N);
	bitset_set(set,N);
	printf("%d in set is %d\n",10,bitset_test(set,10));
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("clear %d\n",N);
	bitset_clear(set,N);
	printf("%d in set is %d\n",N,bitset_test(set,N));

	N=200;
	printf("%d in set is %d\n",N,bitset_test(set,N));
	bitset_fprint(stdout,set);
	printf("set %d\n",N);
	bitset_set(set,N);
	bitset_fprint(stdout,set);
	printf("%d in set is %d\n",10,bitset_test(set,10));
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("clear %d\n",N);
	bitset_clear(set,N);
	printf("%d in set is %d\n",N,bitset_test(set,N));
	bitset_fprint(stdout,set);
	printf("\n");

	N=1000000;
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("set %d\n",N);
	bitset_set(set,N);
	printf("%d in set is %d\n",10,bitset_test(set,10));
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("clear %d\n",N);
	bitset_clear(set,N);
	printf("%d in set is %d\n",N,bitset_test(set,N));

	N=10000;
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("set %d\n",N);
	bitset_set(set,N);
	printf("%d in set is %d\n",10,bitset_test(set,10));
	printf("%d in set is %d\n",N,bitset_test(set,N));
	printf("clear %d\n",N);
	bitset_clear(set,N);
	printf("%d in set is %d\n",N,bitset_test(set,N));

    printf("setting 5 and 60\n");
	bitset_set(set,5);
	bitset_set(set,60);
	for(i=0;i<80;i++) printf("%s",bitset_test(set,i)?"1":"0");
	printf("\n");
	
	
	N=1222333;
	printf("set %d\n",N);
	bitset_set(set,N);
	
	printf("set:{");
	for(element_t e=0;bitset_next_set(set,&e);e++){
	    printf(" %u",e);
	}
	printf(" }\n");
	
	bitset_destroy(set);
}

/*
static void prevnext(){
	bitset_t set=bitset_create(16,16);
	element_t e;
	int i;

	bitset_set(set,0x1);
	bitset_set(set,0x8);
	bitset_set(set,0x10);
	bitset_set(set,0x80);
	bitset_set(set,0x100);
	for(i=0;i<0x110;i++) printf("%s",bitset_test(set,i)?"1":"0");
	printf("\n");
	
	e=0;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;
	printf("e=%x\n",e);
	printf("has next is %d\n",bitset_next_set(set,&e));
	printf("next is %x\n",e);
	e++;


	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;
	printf("e=%x\n",e);
	printf("has prev is %d\n",bitset_prev_set(set,&e));
	printf("prev is %x\n",e);
	e--;

	bitset_fprint(stdout,set);
	bitset_destroy(set);

}

static void ops(){
	bitset_t set1,set2;

	set1=bitset_create(16,16);
	set2=bitset_create(16,32);
	bitset_set(set1,7);
	bitset_set(set2,40);
	bitset_invert(set2);
	printf("set1:\n");
	bitset_fprint(stdout,set1);
	printf("set2:\n");
	bitset_fprint(stdout,set2);
	bitset_union(set1,set2);
	printf("set1 | set2:\n");
	bitset_fprint(stdout,set1);
	printf("\n");
	bitset_destroy(set1);
	bitset_destroy(set2);

	set1=bitset_create(16,16);
	set2=bitset_create(16,32);
	bitset_set(set1,7);
	bitset_set(set1,39);
	bitset_set(set1,40);
	bitset_set(set1,41);
	bitset_set(set2,40);
	bitset_invert(set2);
	printf("set1:\n");
	bitset_fprint(stdout,set1);
	printf("set2:\n");
	bitset_fprint(stdout,set2);
	bitset_intersect(set1,set2);
	printf("set1 & set2:\n");
	bitset_fprint(stdout,set1);
	printf("\n");
	bitset_destroy(set1);
	bitset_destroy(set2);
	
}

static void range(){
    printf("** range test **\n");
    bitset_t set=bitset_create(32,16);
    bitset_set(set,4);
	printf("{4}:\n");
	bitset_fprint(stdout,set);

    bitset_clear_all(set);
    bitset_set_range(set,4,7);
	printf("[4,7]:\n");
	bitset_fprint(stdout,set);

	bitset_clear_all(set);
    bitset_set_range(set,4,80);
	printf("[4,80]:\n");
	bitset_fprint(stdout,set);
	
	bitset_clear_all(set);
    bitset_set_range(set,66,80);
	printf("[66,80]:\n");
	bitset_fprint(stdout,set);

	bitset_clear_all(set);
    bitset_set_range(set,4,150);
	printf("[4,150]:\n");
	bitset_fprint(stdout,set);
		
	bitset_clear_all(set);
    bitset_set_range(set,4,450);
	printf("[4,450]:\n");
	bitset_fprint(stdout,set);
		
    bitset_destroy(set);
    
    set=bitset_create(32,32);
    
	bitset_clear_all(set);
    bitset_set_range(set,4,450);
	printf("[4,450]:\n");
	bitset_fprint(stdout,set);
    
    bitset_destroy(set);
}
*/

int main(){
	setclear();
//	prevnext();
//	ops();
//	range();
	return 0;
}
