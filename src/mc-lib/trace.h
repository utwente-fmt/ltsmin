#ifndef TRACE_H
#define TRACE_H

#include <pins-lib/pins.h>

typedef size_t              ref_t;
static const ref_t          DUMMY_IDX = SIZE_MAX;

typedef void *(*trc_get_state_f)(ref_t state_no, void *arg);
typedef struct trc_env_s    trc_env_t;

extern trc_env_t *trc_create (model_t model, trc_get_state_f get,
                              void *get_state_arg);

extern void trc_write_trace (trc_env_t *env, char *trc_output, ref_t *trace,
                             int level);

extern ref_t *trc_find_trace (ref_t dst_idx, int level, ref_t *parent_ofs, ref_t start_idx,
                              size_t *length);

extern void trc_find_and_write (trc_env_t *env, char *trc_output, ref_t dst_idx,
                                int level, ref_t *parent_ofs, ref_t start_idx);

#endif
