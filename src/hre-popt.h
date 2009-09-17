#ifndef HRE_POPT_H
#define HRE_POPT_H

#include <popt.h>

/**
\file hre-popt.h
\brief HRE support for using Popt.

The sequence of events when using popt options is:
 -# Use the HREaddOptions function to add the options for every subsytem.
 -# Call HREparseOptions to parse the options. If there is a maximum number of argument then option parsing is finished.
 -# If the number of arguments is unlimited then the function
   HREnextOption() has to be called te retrieve those options.
   When there are no option left this function cleans up and returns NULL
*/

/**
\brief Register options to be parsed.
*/
extern void HREaddOptions(struct poptOption *options,const char* header);

/**
\brief Initialize the runtime library using popt.
\param argc Number of arguments to be parsed.
\param argv Arguments to be parsed.
\param min_args The minimum number of arguments allowed.
\param max_args The maximum number of arguments allowed, where -1 denotes infinite.
\param args Array of length min(min_args,max_args) in which arguments are returned.
*/
extern void HREparseOptions(
    int argc,char*argv[],
    int min_args,int max_args,char*args[],
    const char* arg_help
);

/**
\brief Return the next argument.

If an unlimited amount of arguments is allowed then the args
array cannot hold them all and this functions serves as an iterator.
*/
extern char* HREnextArg();

/**
\brief Print usage during argument fase of option parsing.
*/
extern void HREprintUsage();

/**
\brief Print help during argument fase of option parsing.
*/
extern void HREprintHelp();

#endif
