// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef PINS_DLOPEN_API_H
#define PINS_DLOPEN_API_H

/**
\file dlopen-api.h
*/

#include <popt.h>

/**
 Variable containing the name of the plugin.
 */
extern char pins_plugin_name[];

/**
 Type of initializer.
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
 Array of loader record. Required for a language module plugin.
 */
extern struct loader_record pins_loaders[];

/**
 Optional array with plugin options.
 */
extern struct poptOption pins_options[];

#endif

