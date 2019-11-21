#ifndef PBES_GREYBOX_H
#define PBES_GREYBOX_H


/**
\file pbes-greybox.h
*/

#ifdef __cplusplus
extern "C" {
#endif

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption pbes_options[];

//extern const char* vectorToString(model_t model, int* src);

/**
\brief Load a PBES.
\param name filename of the PBES to load.
*/
extern void PBESloadGreyboxModel(model_t model,const char*name);

#ifdef __cplusplus
}
#endif

#endif
