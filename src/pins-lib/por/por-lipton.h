#ifndef LIPTON_POR
#define LIPTON_POR


#include <stdbool.h>
#include <stdlib.h>
#include <pins-lib/por/pins2pins-por.h>

typedef struct lipton_ctx_s lipton_ctx_t;

extern lipton_ctx_t   *lipton_create (por_context* ctx, model_t model);

extern int          lipton_por_all (model_t self, int *src, TransitionCB cb,
                                    void *user_context);

extern bool         lipton_is_stubborn (por_context *ctx, int group);

extern void         lipton_stats (model_t model);

#endif
