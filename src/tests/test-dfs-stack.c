/*
 * test-state-buffer.c
 *
 *  Created on: May 12, 2009
 *      Author: laarman
 */

#include <hre/config.h>

#include <stdlib.h>

#include <util-lib/dfs-stack.c>


static const size_t NUM = 10*1024*1024;
static const size_t FRAMES = 100;
static const size_t ARRAY_SIZE = 10;

int main() {
	dfs_stack_t stack = dfs_stack_create (ARRAY_SIZE);
	size_t x;

	printf("Filling stack\n");
	for (x = 0; x<NUM; x++) {
		int ar[ARRAY_SIZE];
		ar[0] = x; ar[ARRAY_SIZE-1] = -x;
		dfs_stack_push(stack, ar);
		if (x%(NUM/FRAMES)==0) {
			printf("entered frame after: %zu - %zu\n", x, -x);
			dfs_stack_enter(stack);
		}
	}
        char tmp[256];
        ssize_t tmpsz = sizeof tmp;
        printf("%s\n", dfs_stack_to_string(stack, tmp, &tmpsz));


	for (x = 0; x<=FRAMES; x++) {
		int* ar =  dfs_stack_peek_top(stack, x);
		printf("peek_top(%zu): %d - %d\n", x, ar[0], ar[ARRAY_SIZE-1]);
	}


	printf("Emptying stack stack\n");
	for (x = 0; x<NUM; x++) {
		int* ar;
		if ((ar = dfs_stack_top(stack))==NULL) {
			dfs_stack_leave(stack);
			ar = dfs_stack_top(stack);
			printf("leave frame before: %d - %d\n", ar[0], ar[ARRAY_SIZE-1]);
		}
		ar = dfs_stack_pop(stack);
	}

	printf("DONE\n");
	//pop(stack);
	dfs_stack_destroy(stack);
	return 0;
}
