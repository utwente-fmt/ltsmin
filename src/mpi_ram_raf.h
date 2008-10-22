#ifndef RAF_MPI_H
#define RAF_MPI_H

#include "config.h"
#include <mpi.h>
#include "raf.h"

/** @brief Open a random acces file using MPI-IO. */
extern raf_t MPI_Create_raf(char *name,MPI_Comm comm);

extern raf_t MPI_Load_raf(char *name,MPI_Comm comm);

#endif
