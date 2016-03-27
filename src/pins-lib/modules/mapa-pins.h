#ifndef MAPA_GREYBOX_H
#define MAPA_GREYBOX_H

#include <popt.h>

#include <pins-lib/pins.h>

/**
\brief Options of the MAPA language module.
 */
extern struct poptOption mapa_options[];

extern void MAPAloadGreyboxModel(model_t model,const char*name);
/**< @brief Factory method for creating a model.
 */

#endif


