#ifndef MCRL2_GREYBOX_H
#define MCRL2_GREYBOX_H

#include <popt.h>
#include "greybox.h"

extern struct poptOption mcrl2_options[];

extern void MCRL2initGreybox(int argc,char *argv[],void* stack_bottom);
/**< Initialize ATerm library and MCRL2 library up to the loading of a specification. */

extern void MCRL2loadGreyboxModel(model_t model,const char*name);
/**< @brief Factory method for creating a model.
 */

#endif


