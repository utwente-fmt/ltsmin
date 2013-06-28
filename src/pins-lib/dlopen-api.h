// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef PINS_DLOPEN_API_H
#define PINS_DLOPEN_API_H

/**
\file dlopen-api.h
*/

#include <popt.h>
#ifdef LTSMIN_CONFIG_INCLUDED
#include <pins-lib/pins.h>
#else
#include <ltsmin/pins.h>
#endif


/**
\brief To abort exectution, this method should be called by the plug in.
*/
extern void ltsmin_abort(int code) __attribute__ ((noreturn));

/**
\brief Variable containing the name of the plugin.
 */
extern char pins_plugin_name[];


/**
\brief Type of initializer.
 */
typedef void(*init_proc)(int argc,char* argv[]);
/**
 Optional initializer procedure;
 */
extern void init(int argc,char* argv[]);


/**
 Type of a single loader record;
 */
typedef struct loader_record {
    const char *extension; //< extension;
    pins_loader_t loader;  //< loader function, see pins.h;
} loader_record_t;
/**
\brief Array of loader records. Required for language module plugins.
 */
extern struct loader_record pins_loaders[];


/**
\brief Optional array with options for the language modules.
 */
extern struct poptOption pins_options[];

/**
\brief Type of a model initialization function.
*/
typedef void (*pins_model_init_proc)(model_t model);
/**
\brief Optional model initializing function. Used for a single model plugin.
*/
extern void pins_model_init(model_t model);

#endif

