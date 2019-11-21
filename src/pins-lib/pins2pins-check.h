#ifndef PINS2PINS_CHECK
#define PINS2PINS_CHECK

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption check_options[];

extern int PINS_CORRECTNESS_CHECK;

/**
\brief Add wrapper that checks dependency matrices.
*/
extern model_t GBaddCheck (model_t model);

#endif // PINS2PINS_CHECK
