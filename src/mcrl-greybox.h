#ifndef MCRL_GREYBOX_H
#define MCRL_GREYBOX_H

#include "greybox.h"

extern void MCRLinitGreybox(int argc,char *argv[],void* stack_bottom);
/**< Initialize ATerm library and MCRL library up to the loading of a specification. */

extern void MCRLloadGreyboxModel(model_t model,char*name);
/**< @brief Factory method for creating a model.
 */

#endif


