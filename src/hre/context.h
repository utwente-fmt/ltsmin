// -*- tab-width:4 ; indent-tabs-mode:nil -*-
/**
\file context.h
\brief Hybrid runtime contexts.

A context is the rough equivalent of the MPI communicator.
*/

#ifndef HRE_CONTEXT_H
#define HRE_CONTEXT_H

#include <hre/provider.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/tables.h>

/**
\brief Get the global context.

When work on HRE advances, we'll need HRElocal, HREprocess, etc.
This information is provided as a function because it is thread
specific information.
*/
extern hre_context_t HREglobal();

/**
\brief Get the process context.

The members of this context are the threads of the current process.
*/
extern hre_context_t HREprocessGet();

/// create bare object
extern hre_context_t HREctxCreate(int me,int peers,const char* class_name,size_t user_size);

/// check and complete
extern void HREctxComplete(hre_context_t ctx);

/// destroy type
typedef void(*hre_destroy_m)(hre_context_t ctx);

/// set destroy method
extern void HREsetDestroy(hre_context_t ctx,hre_destroy_m method);

/// call destroy method
extern void HREctxDestroy(hre_context_t ctx);

/// Get the class of the context.
extern const char* HREclass(hre_context_t ctx);

/// What is my number?
extern int HREme(hre_context_t ctx);

/// How many peers do I have?
extern int HREpeers(hre_context_t ctx);

/// Often used: am I the leader?
extern int HREleader(hre_context_t ctx);

/// abort method
typedef void (*hre_abort_m)(hre_context_t ctx,int code) __attribute__ ((noreturn));

/// set the abort method.
extern void HREsetAbort(hre_context_t ctx,hre_abort_m method);

/// Abort the running application.
extern void HREctxAbort(hre_context_t ctx,int code) __attribute__ ((noreturn));

/// exit method
typedef void (*hre_exit_m)(hre_context_t ctx,int code) __attribute__ ((noreturn));

/// set the exit method.
extern void HREsetExit(hre_context_t ctx,hre_exit_m method);

/// Exit from the running application.
extern void HREctxExit(hre_context_t ctx,int code) __attribute__ ((noreturn));

typedef enum {
    UInt32,
    UInt64,
    Pointer,
    SizeT
} unit_t;

typedef enum {Sum,Max} operand_t;

extern void HREreduce(hre_context_t ctx,int len,void*in,void*out,unit_t type,operand_t op);

/// Check if any of the peers passes true.
extern int HREcheckAny(hre_context_t ctx,int arg);

extern void HREbarrier(hre_context_t ctx);

typedef void(*hre_yield_m)(hre_context_t ctx);

extern void HREyieldSet(hre_context_t ctx,hre_yield_m method);

/**
\brief Yield control to the runtime library.

Some run times (e.g. MPI) do work in the background.
This functions allows background work to progress.
*/
extern void HREyield(hre_context_t ctx);

typedef void(*hre_yield_while_m)(hre_context_t ctx,int* condition);

extern void HREyieldWhileSet(hre_context_t ctx,hre_yield_while_m method);

/**
\brief Yield control to the runtime library.

Some run times (e.g. MPI) do work in the background.
This functions allows background work to progress.
*/
extern void HREyieldWhile(hre_context_t ctx,int*condition);

typedef void(*hre_cond_signal_m)(hre_context_t ctx, int id);

extern void HREcondSignalSet(hre_context_t ctx, hre_cond_signal_m method);

/**
\brief Signals queue id.

Useful in the case that two processes need to exchange messages, but do
not both have HREyieldWhile in their main loop.
*/
extern void HREcondSignal(hre_context_t ctx, int id);

/**
\defgroup hre-message-api Messaging Passing Interface for Hybrid Runtime Environment.
*/
/*@{*/

typedef struct hre_msg_s * hre_msg_t;

struct hre_msg_s {
    /// context for the free call.
    void* ready_ctx;
    /// called when processing of the block has finished.
    void (*ready)(hre_msg_t self,void* ready_ctx);
    /// next in queue.
    hre_msg_t next;
    /// context to which the message belongs.
    hre_context_t context;
    /// Sender of the message.
    uint32_t source;
    /// Receiver of the message.
    uint32_t target;
    /// communicator with the context.
    uint32_t comm;
    /// Message tag;
    uint32_t tag;
    /// size of the buffer.
    uint32_t size;
    /// head of the used area in the buffer.
    uint32_t head;
    /// tail of the used area in the buffer.
    uint32_t tail;
    /// buffer area;
    char buffer[0];
};

/// Create a new message for use in the given context.
extern hre_msg_t HREnewMessage(hre_context_t context,uint32_t size);

/// Destory the given message.
extern void HREdestroyMessage(hre_msg_t msg);

/**
\brief Deliver a message.
*/
extern void HREdeliverMessage(hre_msg_t msg);

/**
\brief Post sending of a message.

When the transfer is complete ready will be called.
*/
extern void HREpostSend(hre_msg_t msg);

/**
\brief Send a message.

When the call returns the message has at least been copied out of the buffer.
Possibly it has been consumed completely.
 */
extern void HREsend(hre_msg_t msg);

/// Done processing message.
extern void HREmsgReady(hre_msg_t msg);

/**
\brief Message ready callback that decreases a given counter.
*/
extern void hre_ready_decr(hre_msg_t self,void* ready_ctx);

/**
\brief FIFO queue for messages.
*/
typedef struct msg_queue_s {
    hre_msg_t head;
    hre_msg_t tail;
} *msg_queue_t;

/**
\brief Put message in queue.
*/
extern void hre_put_msg(msg_queue_t queue, hre_msg_t msg);

/**
\brief Get message from queue.
*/
extern hre_msg_t hre_get_msg(msg_queue_t queue);

/// Opaque type message buffer.
typedef struct hre_buffer_s *hre_buffer_t;

/**
\brief Create a buffer for direct communication.
*/
extern hre_buffer_t HREbufferCreate(hre_context_t context,uint32_t prio,uint32_t size);

/**
\brief Wait for arrival of a message.
*/
extern hre_msg_t HRErecv(hre_buffer_t buf,int from);

/**
\brief Get the tag to send to a buffer.
*/
extern int HREbufferTag(hre_buffer_t buf);

/// Forward method.
typedef void(*hre_xfer_m)(hre_context_t context,hre_msg_t msg);

/// Set method for posting a send operation.
extern void HREsendSet(hre_context_t context,hre_xfer_m method);

/// Receive semantics options.
typedef enum {
/// IO subsystem is passive: library must ask for message before it can be received.
HRErecvPassive,
/// IO subsystem is active: library must be prepared to receive messages.
HRErecvActive
} hre_recv_t;

/// Set method for posting a receive operation.
extern void HRErecvSet(hre_context_t context,hre_xfer_m method,hre_recv_t semantics);

/// Message Receive Callback.
typedef void(*hre_receive_cb)(void* context,hre_msg_t msg);

/// Create an action.
extern uint32_t HREactionCreate(hre_context_t context,uint32_t prio,uint32_t size,hre_receive_cb response,void* response_arg);

///  Delete an action.
extern void HREactionDelete(hre_context_t context,uint32_t prio,uint32_t tag);

/*}@*/

extern array_manager_t HREcommManager(hre_context_t context);

/**
\brief Set the memory region for messages.

If memory for use in messages has to come from a non-standard region then
the implementor of the context has to use this call to set the correct region.
This will happen, for example if POSIX shared memory is used.
*/
extern void HREmsgRegionSet(hre_context_t context,hre_region_t region);


/**
\brief Allocate an uninitialised shared memory area.

An error is indicated by returning NULL.
*/
extern void* HREshmGet(hre_context_t context,size_t size);

typedef void*(*hre_shm_get_m)(hre_context_t context,size_t size);

/// Set the shared memory allocation method.
extern void HREshmGetSet(hre_context_t context,hre_shm_get_m method);

#endif

