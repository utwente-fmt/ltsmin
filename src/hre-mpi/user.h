// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef HRE_MPI_USER_H
#define HRE_MPI_USER_H

/**
\file user.h

Implementation of HRE for MPI.
*/

/**
\brief Macro to enable all available runtimes.
*/
#ifndef HREenableAll
#define HREenableAll HREenableAllMPI
#endif

/*
Include must happen after defining the HREenableAll macro.
Otherwise MPI is not enabled by HREenableAll.
*/

#include <hre/user.h>
typedef struct event_queue_s* event_queue_t;
/**
Enable the MPI run time.
*/
extern void HREenableMPI();

/**
\brief Select the MPI runtime.

This call allows selecting the mpi runtime without passing options.
If the MPI runtime is the first runtime enabled and this function is called
then running under MPI is guaranteed.
*/
extern void HREselectMPI();

/**
\brief Enable MPI and the standard run times.
*/
extern void HREenableAllMPI();

/**
\brief Provide access to the internal event queue.
*/
extern event_queue_t HREeventQueue(hre_context_t ctx);

#endif

