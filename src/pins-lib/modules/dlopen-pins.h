// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef PLUGIN_GREYBOX_H
#define PLUGIN_GREYBOX_H

/**
\file dlopen-pins.h
*/

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption pins_plugin_options[];

extern void PINSpluginLoadGreyboxModel(model_t model,const char*name);

#endif

