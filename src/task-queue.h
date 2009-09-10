/**
\file task-queue.h

\brief parallel task queueing framework.
*/

#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <mpi-event-loop.h>

/// Abstract type for a task queue.
typedef struct task_queue_s *task_queue_t;

/// Create a queue on top of MPI.
extern task_queue_t TQcreateMPI(event_queue_t mpi_queue);

/// Abstract type for a task.
typedef struct task_s *task_t;

/// function type for task execution.
typedef void(*task_exec_t)(void *context,int from,int len,void*arg);

/// Create a new fixed length argument task.
extern task_t TaskCreateFixed(task_queue_t q,int arglen,void * context,task_exec_t call);

/// Submit a fixed length task.
extern void TaskSubmitFixed(task_t task,int owner,void* arg);

/// Create a new variable length argument task.
extern task_t TaskCreateFlex(task_queue_t q,void * context,task_exec_t call);

/// Submit a fixed length task.
extern void TaskSubmitFlex(task_t task,int owner,int len,void* arg);

/// Wait for completion of submitted tasks.
extern void TQwait(task_queue_t q);

/// Wait for completion of submitted tasks.
///extern void TQwait2(task_queue_t q1,task_queue_t q2);

/// Poll for tasks ready to be executed.
extern void TQyield(task_queue_t q);

/// Wait until at least one task has been executed.
extern void TQblock(task_queue_t q);

/// Wait while the given interger is non-zero.
extern void TQwhile(task_queue_t queue,int*condition);

/**
\brief Destroy a task.

The behaviour of the application is undefined if there
are tasks pending when this funciton is called.
*/
extern void TaskDestroy(task_t task);

/**
\brief Return the size of the group of cooperating threads.
*/
extern int TQthreadCount(task_queue_t queue);

/**
\brief Return the identity of the current thread within the group of cooperating threads.
*/
extern int TQthreadID(task_queue_t queue);

/**
\brief Barrier synchronization on a task queue.
*/
extern void TQbarrier(task_queue_t queue);

/**
\brief Get a duplicate queue with separate buffers.
*/
extern task_queue_t TQdup(task_queue_t queue);
#endif

