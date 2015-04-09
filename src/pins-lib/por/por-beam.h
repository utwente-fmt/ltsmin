#ifndef BEAM_POR
#define BEAM_POR


#include <pins-lib/por/pins2pins-por.h>


extern struct poptOption beam_options[];


typedef struct beam_s beam_t;

extern beam_t  *beam_create_context (por_context *ctx);

extern bool     beam_is_stubborn (por_context *ctx, int group);

extern int      beam_search_all (model_t self, int *src, TransitionCB cb,
                                 void *user_context);

#endif
