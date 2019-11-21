/*
 * test-state-buffer.c
 *
 *  Created on: May 12, 2009
 *      Author: laarman
 */

#include <hre/config.h>

#include <stdio.h>
#include <stdlib.h>

#include <util-lib/is-balloc.h>

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
	isb_allocator_t b = isba_create(N);
	int prev = NUM;

	if (1) {

		printf("Filling buffer\n");
		for(y=0; y<NUM; y++) {
			test[0] = y;
			test[last] = -y;
			isba_push_int(b, test);
		}

		printf("testing peek\n");

		for(y=0; y<NUM; y++) {
			int neg = isba_peek_int(b, y)[last];
            //printf("%d\n", neg);
			if (y-neg!=NUM-1)
				printf("peek failed: peek(%zu). value: %d expected: %zu\n", y, neg, y-NUM-1);
		}

		printf("Emptying buffer\n");
		size_t size = isba_size_int(b);
        //printf("%o\n", size);
		for(y=0; y<size; y++) {
			res = isba_pop_int(b);
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
		isba_push_int(b, test);
	}
	printf("Testing discard (2)\n");
	for(y=0; y<NUM/2; y++) {
		isba_discard_int(b, 2);
	}

	printf("Testing pop, expected fail:\n");
	res = isba_pop_int(b);
	//fprintf(stdout, "last: %d !! %d \n", res[0], res[1]);
	isba_destroy(b);
	return 0;
}
