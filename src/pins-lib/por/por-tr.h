#ifndef TR_POR
#define TR_POR


#include <stdbool.h>
#include <stdlib.h>
#include <pins-lib/por/pins2pins-por.h>

typedef struct tr_ctx_s tr_ctx_t;

extern tr_ctx_t    *tr_create (por_context* ctx, model_t model);

extern int          tr_por_all (model_t self, int *src, TransitionCB cb,
                                    void *user_context);

extern bool         tr_is_stubborn (por_context *ctx, int group);

#endif
