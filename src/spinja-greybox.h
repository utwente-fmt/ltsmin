#ifndef SPINJA_GREYBOX_H
#define SPINJA_GREYBOX_H

/**
\file spinja-greybox.h
*/

#include <popt.h>
#include <greybox.h>

extern struct poptOption spinja_options[];

/**
Load a spinja model.
*/
extern void SpinJaloadDynamicLib(model_t model, const char *name);
extern void SpinJaloadGreyboxModel(model_t model,const char*name);
extern void SpinJacompileGreyboxModel(model_t model,const char*name);

#endif
