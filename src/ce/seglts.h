
#ifndef SEGLTS_H
#define SEGLTS_H


typedef struct seginfo {
	char *info;
	int segment_count;
	int initial_seg;
	int initial_ofs;
	int label_count;
	int label_tau;
	int top_count;
	int *state_count;
	int **transition_count;
} *seginfo_t;

extern int SLTSCreateInfo(seginfo_t *info,int segment_count);

extern int SLTSReadInfo(seginfo_t *info,char *name);

extern int SLTSWriteInfo(seginfo_t info,char *name);

extern char *SLTSerror();

#endif

