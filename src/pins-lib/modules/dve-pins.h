#ifndef DIVINE2_GREYBOX_H
#define DIVINE2_GREYBOX_H

/**
\file dveC-greybox.h
*/

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption dve_options[];

/**
Load an dveC model.
*/
extern void DVE2loadDynamicLib(model_t model, const char *name);
extern void DVE2loadGreyboxModel(model_t model,const char*name);
extern void DVE2compileGreyboxModel(model_t model,const char*name);

#endif

