/*
 * test-state-buffer.c
 *
 *  Created on: May 12, 2009
 *      Author: laarman
 */

#include "dfs-stack.c"
#include <stdlib.h>

static const size_t NUM = 10*1024*1024;
static const size_t FRAMES = 100;
static const size_t ARRAY_SIZE = 10;

int main() {
	dfs_stack_t stack = create_stack(ARRAY_SIZE);
	size_t x;

	printf("Filling stack\n");
	for (x = 0; x<NUM; x++) {
		int ar[ARRAY_SIZE];
		ar[0] = x; ar[ARRAY_SIZE-1] = -x;
		push(stack, ar);
		if (x%(NUM/FRAMES)==0) {
			printf("entered frame after: %zu - %zu\n", x, -x);
			enter(stack);
		}
	}
	printf("%s\n", stack_to_string(stack));


	for (x = 0; x<=FRAMES; x++) {
		int* ar =  peek_top(stack, x);
		printf("peek_top(%zu): %d - %d\n", x, ar[0], ar[ARRAY_SIZE-1]);
	}


	printf("Emptying stack stack\n");
	for (x = 0; x<NUM; x++) {
		int* ar;
		if ((ar = top(stack))==NULL) {
			leave(stack);
			ar = top(stack);
			printf("leave frame before: %d - %d\n", ar[0], ar[ARRAY_SIZE-1]);
		}
		ar = pop(stack);
	}

	printf("DONE\n");
	//pop(stack);
	destroy_stack(stack);
	return 0;
}
