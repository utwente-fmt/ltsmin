#ifndef TRACE_H
#define TRACE_H

#include <lts-type.h>
#include <greybox.h>

typedef struct trc_s *trc_t;
typedef void *(*trc_get_state_f)(int state_no, int *state);
typedef struct trc_env_s trc_env_t;

extern int trc_get_length(trc_t trace);

extern lts_type_t trc_get_ltstype(trc_t trace);

extern int trc_get_type(trc_t trace, int type, int label, size_t dst_size, 
                        char* dst);

extern int trc_get_edge_label(trc_t trace, int i, int *dst);

extern int trc_get_state_label(trc_t trace, int i, int *dst);

extern int trc_get_state_idx(trc_t trace, int i);

extern void trc_get_state(trc_t trace, int i, int *dst);

extern trc_t trc_read(const char *name);

extern trc_env_t *trc_create(model_t model, trc_get_state_f get, int start_idx);

extern void trc_find_and_write (trc_env_t *env, char *trc_output, 
                                int dst_idx, int level, int *parent_ofs);

#endif
