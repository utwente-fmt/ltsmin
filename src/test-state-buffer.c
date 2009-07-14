/*
 * test-state-buffer.c
 *
 *  Created on: May 12, 2009
 *      Author: laarman
 */


#include "state-buffer.c"
#include <stdlib.h>

static const size_t NUM = 10*1024*1024;
static const int N = 5;

/*
 \brief small test
 */
int main() {
    int last = N-1;
	int test[N];
	int* res;
	//int x;
	size_t y;
	state_buffer_t b = create_buffer(N);
	int prev = NUM;

	if (1) {

		printf("Filling buffer\n");
		for(y=0; y<NUM; y++) {
			test[0] = y;
			test[last] = -y;
			push_int(b, test);
		}

		printf("testing peek\n");

		for(y=0; y<NUM; y++) {
			int neg = peek_int(b, y)[last];
            //printf("%d\n", neg);
			if (y-neg!=NUM-1)
				printf("peek failed: peek(%zu). value: %d expected: %zu\n", y, neg, y-NUM-1);
		}

		printf("Emptying buffer\n");
		size_t size = buffer_size_int(b);
        //printf("%o\n", size);
		for(y=0; y<size; y++) {
			res = pop_int(b);
            //printf("%d\n", res[0]);
			if(res[0]+1!=prev) {
				fprintf(stdout, "jump: %d !! %d \n", res[0], prev);
			}
			//if (x==1024*1024*10||x==1024*1024*20||x==1024*1024*30||x==1024*1024*40||
			//		x==1024*1024*50||x==1024*1024*60||x==1024*1024*70||x==1024*1024*80)
			//	sleep(1);
			prev=res[0];
		}

	}



	printf("Filling buffer\n");
	for(y=0; y<NUM; y++) {
		test[0] = y;
		test[last] = -y;
		push_int(b, test);
	}
	printf("Testing discard (2)\n");
	for(y=0; y<NUM/2; y++) {
		discard_int(b, 2);
	}

	printf("Testing pop, expected fail:\n");
	res = pop_int(b);
	fprintf(stdout, "last: %d !! %d \n", res[0], res[1]);
	destroy_buffer(b);
	return 0;
}
