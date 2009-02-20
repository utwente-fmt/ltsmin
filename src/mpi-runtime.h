#ifndef MPI_RUNTIME_H
#define MPI_RUNTIME_H

#include <mpi.h>
#include <runtime.h>

/**
\file mpi-runtime.h
\brief MPI specific part of the runtime library.
\addtogroup runtime
*/
///@{

/**
Initialize the runtime library with MPI support, after initializing MPI.
*/
extern void RTinitMPI(int*argcp,char**argvp[]);

/**
Initialize the runtime library with MPI support, after initializing MPI with thread support.
*/
extern void RTinitMPIthread(int*argcp,char**argvp[],int requested,int *provided);

///@}
#endif
