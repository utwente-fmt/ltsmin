#ifndef MCRL_GREYBOX_H
#define MCRL_GREYBOX_H

#include "greybox.h"

extern void MCRLinitGreybox(int argc,char *argv[],void* stack_bottom);
/**< Initialize ATerm library and MCRL library up to the loading of a specification. */

/**
\brief Flag that tells the mCRL grey box loader to pass state variable names.
 */
#define STATE_VISIBLE 0x01

extern void MCRLloadGreyboxModel(model_t model,char*name,int flags);
/**< @brief Factory method for creating a model.
 */

#endif


