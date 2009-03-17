#ifndef NIPS_GREYBOX_H
#define NIPS_GREYBOX_H

#include <popt.h>
#include "greybox.h"

extern struct poptOption nips_options[];

extern void NIPSinitGreybox(int argc,char *argv[]);
/**< Initialize NIPS library up to the loading of a specification. */

extern void NIPSloadGreyboxModel(model_t model,const char*name);
/**< @brief Factory method for creating a model.
 */

#endif


