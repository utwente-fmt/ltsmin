#ifndef OPAAL_GREYBOX_H
#define OPAAL_GREYBOX_H

/**
\file opaal-greybox.h
*/

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption opaal_options[];

/**
Load an dveC model.
*/
extern void opaalLoadDynamicLib(model_t model, const char *name);
extern void opaalLoadGreyboxModel(model_t model,const char*name);
extern void opaalCompileGreyboxModel(model_t model,const char*name);

#endif

