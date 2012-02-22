// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef DIR_INFO_H
#define DIR_INFO_H

typedef struct dir_info_s {
    char *info;
    int segment_count;
    int initial_seg;
    int initial_ofs;
    int label_count;
    int label_tau;
    int top_count;
    int *state_count;
    int **transition_count;
} *dir_info_t;

extern dir_info_t DIRinfoCreate(int segment_count);

extern void DIRinfoDestroy(dir_info_t info);

/**
 if check_magic is true then the magic number is read and tested.
 otherwise it is assume that the magic number has already been read.
 */
extern dir_info_t DIRinfoRead(stream_t input,int check_magic);

extern void DIRinfoWrite(stream_t output,dir_info_t info);

#endif

