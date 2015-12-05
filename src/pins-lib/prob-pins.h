#ifndef PROB_GREYBOX_H
#define PROB_GREYBOX_H

#include <popt.h>
#include <pins-lib/pins.h>

extern struct poptOption prob_options[];

extern void ProBloadGreyboxModel(model_t model, const char* name);

#endif
