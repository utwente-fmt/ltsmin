#ifndef NIPS_GREYBOX_H
#define NIPS_GREYBOX_H

#include "greybox.h"

extern void NIPSinitGreybox(int argc,char *argv[]);
/**< Initialize NIPS library up to the loading of a specification. */

extern void NIPSloadGreyboxModel(model_t model,char*name);
/**< @brief Factory method for creating a model.
 */

#endif


