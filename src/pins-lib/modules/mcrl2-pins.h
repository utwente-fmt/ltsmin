#ifndef MCRL2_GREYBOX_H
#define MCRL2_GREYBOX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption mcrl2_options[];

extern void MCRL2initGreybox(int argc,const char *argv[],void *stack_bottom);
/**< Initialize ATerm library and MCRL2 library up to the loading of a specification. */

extern void MCRL2loadGreyboxModel(model_t model,const char*name);
/**< @brief Factory method for creating a model. */

extern void MCRL2CompileGreyboxModel(model_t model, const char *filename);
/**< @brief Factory method for compiling a model. */

#ifdef __cplusplus
}
#endif

#endif


