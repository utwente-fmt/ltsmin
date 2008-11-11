

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
\brief Keep exectuing while the condition is true.
*/
extern void event_while(event_queue_t queue,int *condition);

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


typedef struct idle_detect_s *idle_detect_t;

extern idle_detect_t event_idle_create(event_queue_t queue,MPI_Comm comm,int tag);

extern void event_idle_send(idle_detect_t detector);

extern void event_idle_recv(idle_detect_t detector);

extern void event_idle_detect(idle_detect_t detector);

typedef struct event_barrier_s *event_barrier_t;

extern event_barrier_t event_barrier_create(event_queue_t queue,MPI_Comm comm,int tag);

extern void event_barrier_wait(event_barrier_t barrier);

extern void event_decr(void*context,MPI_Status *status);

extern void event_statistics(event_queue_t queue);

#endif


