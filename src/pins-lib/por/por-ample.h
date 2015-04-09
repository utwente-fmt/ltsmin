#ifndef AMPLE_POR
#define AMPLE_POR


#include <pins-lib/por/pins2pins-por.h>


typedef struct ample_ctx_s ample_ctx_t;

extern ample_ctx_t *ample_create_context (por_context *ctx, bool all);

extern bool         ample_is_stubborn (por_context *ctx, int group);

extern int          ample_search_all (model_t self, int *src, TransitionCB cb,
                                      void *user_context);

#endif
