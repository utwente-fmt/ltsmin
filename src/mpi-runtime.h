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
\brief Call popt, after initializing the runtime library with MPI support, after initializing MPI.
 */
extern void RTinitPoptMPI(int *argc_p,char**argv_p[],const struct poptOption * options,
	int min_args,int max_args,char*args[]	,
	const char* pgm_prefix,const char* arg_help,const char* extra_help
);

/**
Initialize the runtime library with MPI support, after initializing MPI with thread support.
*/
extern void RTinitMPIthread(int*argcp,char**argvp[],int requested,int *provided);

extern void RTfiniMPI();

///@}
#endif
