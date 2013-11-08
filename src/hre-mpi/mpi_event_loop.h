// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef MPI_EVENT_LOOP_H
#define MPI_EVENT_LOOP_H

#include <mpi.h>

/**
\file mpi-event-loop.h
\brief Implement a client-server architecture with asynchronous communication and callback functions.

Asynchronous communication is very efficient, but writing huge main loops that use
MPI_Wait or relatives is cumbersome. This library provides the notion of an event queue where
requests can be inserted, together with a callback to be executed once the request has been completed.
*/

/// Abstract type of event queue.
#ifndef HRE_MPI_USER_H
typedef struct event_queue_s* event_queue_t;
#endif

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
\brief Block until at least one event is executed.

Like yield except that at least one event has to be executed.
*/
extern void event_block(event_queue_t queue);

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
\brief Execute events until the mpi synchronous send has completed.
*/
extern void event_Ssend(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
	int dest, int tag, MPI_Comm comm);

/**
\brief Post a new Isend in a queue with a callback.
*/
extern void event_Isend(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
	int dest, int tag, MPI_Comm comm,event_callback cb,void*context);

/**
\brief Post a new Issend in a queue with a callback.
*/
extern void event_Issend(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
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

/** \defgroup termination_detection Termination Detection
Detect when all messages sent have been received.

Given one main thread per worker and several coroutines that can send messages to each other.
Once the main threads have no more work left, the coroutines may still be active, so
the main threads must wait for all of the coroutines to become idle.

The main threads are active until they call detect.
The coroutines are active iff they are executing.
The system is idle if all threads and coroutines are idle
and there are no messages pending.
  */
//@{

/// handle
typedef struct idle_detect_s *idle_detect_t;

/// create
extern idle_detect_t event_idle_create(event_queue_t queue,MPI_Comm comm,int tag);

/// To be called for every tracked message sent.
extern void event_idle_send(idle_detect_t detector);

/// to be called for every tracked message received.
extern void event_idle_recv(idle_detect_t detector);

/** Wait for all other to become idle as well.
*/
extern int event_idle_detect(idle_detect_t detector);

/**
Set the exit code of idle_detect.
*/
extern void event_idle_set_code(idle_detect_t detector,int code);

//@}

typedef struct event_barrier_s *event_barrier_t;

extern event_barrier_t event_barrier_create(event_queue_t queue,MPI_Comm comm,int tag);

extern void event_barrier_wait(event_barrier_t barrier);

extern void event_decr(void*context,MPI_Status *status);

extern void event_statistics(log_t log,event_queue_t queue);

#endif


