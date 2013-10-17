// -*- tab-width:4 ; indent-tabs-mode:nil -*-

/**
\file hre/queue.h

\brief Reactive system with distributed termination detection for HRE.

Extends the action mechanism of HRE with distributed termination detection.
The implementation uses message combining to improve performance when many small
messages have to be sent.
*/

#ifndef HRE_TASK_QUEUE_H
#define HRE_TASK_QUEUE_H

#include <hre/context.h>

/// Abstract type for a task queue.
typedef struct hre_task_queue_s *hre_task_queue_t;

/// Abstract type for a task.
typedef struct hre_task_s *hre_task_t;

/// function type for task execution.
typedef void(*hre_task_exec_t)(void *context,int from,int len,void*arg);

/**
\brief Create a new task.

If the given arglen is 0 then the task has flexible sized arguments.
If arglen is greater than 0 then arglen is the fixed length of the argument.
*/
extern hre_task_t TaskCreate(hre_task_queue_t q,uint32_t prio,uint32_t buffer_size,hre_task_exec_t call,void * call_ctx,int arglen);


/**
\brief Add a fifo buffer to a task.

In normal operation tasks are not allow to call themselves because this
could cause deadlocks. This operation add a fifo buffer to a task, which
makes it safe for a task to call itself, at the price of potentiallly very big fifo
queues.
 */
extern void TaskEnableFifo(hre_task_t task);

/// Submit a fixed length task.
extern void TaskSubmitFixed(hre_task_t task,int owner,void* arg);

/// Submit a variable length task.
extern void TaskSubmitFlex(hre_task_t task,int owner,int len,void* arg);

/**
\brief Destroy a task.

The behaviour of the application is undefined if there
are tasks pending when this funciton is called.
*/
extern void TaskDestroy(hre_task_t task);

/**
\brief Wait for completion of all submitted tasks.

This procedure returns 0 upon termination and 1 upon cancellation of the wait.
*/
extern int TQwait(hre_task_queue_t q);

/// Cancel waiting for a queue.
extern void TQwaitCancel(hre_task_queue_t q);

/// Wait while the given interger is non-zero.
extern void TQwhile(hre_task_queue_t queue,int*condition);

/**
\brief Create a new task queue.

*/
extern hre_task_queue_t HREcreateQueue(hre_context_t bare);

/**
\brief Get the context on which the queue is based.
*/
extern hre_context_t TQcontext(hre_task_queue_t queue);

#endif

