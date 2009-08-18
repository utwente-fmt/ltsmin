
#ifndef DLTS_H
#define DLTS_H

#include <sys/types.h>
#include "config.h"

typedef struct dlts {
	char *dirname;
	char *info;
	int segment_count;
	int root_seg;
	int root_ofs;
	int label_count;
	int tau;
	int *state_count;
	int **transition_count;
	char **label_string;
	int ***src;
	int ***label;
	int ***dest;
} *dlts_t;

extern dlts_t dlts_create();

extern void dlts_free(dlts_t lts);

extern void dlts_getinfo(dlts_t lts);

extern void dlts_getTermDB(dlts_t lts);

extern void dlts_load_src(dlts_t lts,int from,int to);

extern void dlts_free_src(dlts_t lts,int from,int to);

extern void dlts_load_label(dlts_t lts,int from,int to);

extern void dlts_free_label(dlts_t lts,int from,int to);

extern void dlts_load_dest(dlts_t lts,int from,int to);

extern void dlts_free_dest(dlts_t lts,int from,int to);

#endif



