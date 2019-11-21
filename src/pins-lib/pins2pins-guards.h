#ifndef PINS2PINS_GUARDS
#define PINS2PINS_GUARDS

#include <popt.h>

#include <pins-lib/pins.h>

extern struct poptOption guards_options[];

extern int              PINS_GUARD_EVAL;

/**
\brief Add wrapper that checks guards for next_all calls.
*
* DO NOT USE THIS LAYER. IMPLEMENTING THIS FUNCTIONALITY SHOULD BE DONE BY THE
* FRONTEND.
*
* This is a deprecated wrapper. Faster guard-based implementations of the
* next-all function can better be achieved in the language module itself.
* The --pins-guards flag is therefore hidden.
*/
extern model_t GBaddGuards (model_t model);

#endif // PINS2PINS_GUARDS
