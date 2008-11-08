

#ifndef MPI_EVENT_LOOP_H
#define MPI_EVENT_LOOP_H

#include "config.h"
#include <mpi.h>

/**
\file mpi-event-loop.h
\brief Implement a client-server architecture with asynchronous communication and callback functions.

Asynchronous communication is very efficient, but writing huge main loops that use
MPI_Wait or relatives is cumbersome. This library provides the notion of an event queue where
requests can be inserted, together with a callback to be executed once the request has been completed.
*/

/// Abstract type of event queue. 
typedef struct event_queue_s* event_queue_t;

/**
\brief Create a new event queue.
*/
extern event_queue_t event_queue();

/**
\brief Destroy the given queue.
*/
extern void event_queue_destroy(event_queue_t *queue);

/**
\brief Yield to events from queue.

Execute the events in the queue that are currently ready to fire.
*/
extern void event_yield(event_queue_t queue);

/**
When a request has completed continue using context and status.
*/
typedef void(*event_callback)(void* context,MPI_Status *status);

/**
\brief Define the action to be taken in the request completes.
*/
extern void event_post(event_queue_t queue,MPI_Request *request,event_callback cb,void*context);

/**
\brief Keep exectuing until the given request has completed.
*/
extern void event_wait(event_queue_t queue,MPI_Request *request,MPI_Status *status);

/**
\brief Execute events until the mpi send has completed.
*/
extern void event_Send(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
	int dest, int tag, MPI_Comm comm);

/**
\brief Post a new Isend in a queue with a callback.
*/
extern void event_Isend(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
	int dest, int tag, MPI_Comm comm,event_callback cb,void*context);

/**
Execute queue events until message received.
*/
extern void event_Recv(event_queue_t queue, void *buf, int count, MPI_Datatype datatype,
        int source, int tag, MPI_Comm comm, MPI_Status *status);

/**
\brief Post a new Irecv in a queue with a callback.
*/
extern void event_Irecv(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
	int source, int tag, MPI_Comm comm,event_callback cb,void*context);

/// mpi lock type
typedef struct mpi_lock_s *mpi_lock_t;

/**
\brief Create an MPI lock

Create an MPI lock for a communicator, where the messages that maintain the
lock can use tag.
*/
mpi_lock_t mpi_lock_create(MPI_Comm comm,int tag);

/// Block until you have the lock.
void mpi_lock_get(mpi_lock_t lock);

/// Try to get the lock, return the current holder of the lock.
int mpi_lock_try(mpi_lock_t lock);

/// Release the lock.
void mpi_lock_free(mpi_lock_t lock);

/// Check if someone is trying to lock
int mpi_lock_check(mpi_lock_t lock);


#endif


