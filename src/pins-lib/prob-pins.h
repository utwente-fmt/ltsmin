#ifndef PROB_GREYBOX_H
#define PROB_GREYBOX_H

#include <popt.h>
#include <pins-lib/pins.h>

extern struct poptOption prob_options[];

extern void ProBcreateZocket(model_t model, const char* name);

extern void ProBstartProb(model_t model, const char* name);

extern void ProBloadGreyboxModel(model_t model, const char* name);

#endif
