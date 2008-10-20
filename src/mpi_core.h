#ifndef MPI_CORE_H
#define MPI_CORE_H

#include <mpi.h>

typedef void (*core_handler)(void*,MPI_Status*);

/* The following variables are initialized by core_init() */

extern int mpi_nodes;
extern int mpi_me;
extern int* mpi_zeros;
extern int* mpi_ones;
extern int* mpi_indices;
extern char mpi_name[];

/* initialize datastructures */
extern void core_init();

/* registers a handler with the core and returns the tag for sending messages */
extern int core_add(void*,core_handler);

/* handle all available messages */
extern void core_yield();

/* handle messages until at least one message with tag tag was handled */
extern void core_wait(int);

/* Keeps handling messages until all processes have entered core_barrier and then leaves */
extern void core_barrier();

/* waiting for a message exchange round to terminate.
 * use TERM_INIT to initialize the datastructure.
 * use SEND after sending a message
 * use RECV after receiving a message
 * use core_terminate to wait for termination
 */

typedef struct term_struct {
	int dirty;
	int count;
} TERM_STRUCT;

#define TERM_INIT(s) ((s)->dirty=0,(s)->count=0)
#define SEND(s) ((s)->count++)
#define RECV(s) ((s)->dirty=1,(s)->count--)

extern void core_terminate(TERM_STRUCT*);

#endif

