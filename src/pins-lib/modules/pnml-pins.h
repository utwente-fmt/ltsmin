#ifndef PNML_GREYBOX_H
#define PNML_GREYBOX_H

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption pnml_options[];

extern void PNMLloadGreyboxModel(model_t model, const char* name);

#endif
