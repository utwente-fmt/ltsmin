#ifndef ETF_GREYBOX_H
#define ETF_GREYBOX_H

/**
\file etf-greybox.h
*/

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption etf_options[];

/**
Load an ETF model.
*/
extern void ETFloadGreyboxModel(model_t model,const char*name);


#endif

