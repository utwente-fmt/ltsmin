
#ifndef DLTS_H
#define DLTS_H

#include "config.h"
#include <sys/types.h>
#include "archive.h"

typedef struct dlts {
	archive_t arch;
	char *decode;
	char *info;
	int segment_count;
	uint32_t root_seg;
	uint32_t root_ofs;
	int label_count;
	uint32_t tau;
	uint32_t *state_count;
	uint32_t **transition_count;
	char **label_string;
	uint32_t ***src;
	uint32_t ***label;
	uint32_t ***dest;
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



