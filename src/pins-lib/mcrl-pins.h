#ifndef MCRL_GREYBOX_H
#define MCRL_GREYBOX_H

#include <popt.h>

#include <pins-lib/pins.h>

/**
\brief Options of the mCRL language module.
 */
extern struct poptOption mcrl_options[];

extern void MCRLloadGreyboxModel(model_t model,const char*name);
/**< @brief Factory method for creating a model.
 */

#endif


