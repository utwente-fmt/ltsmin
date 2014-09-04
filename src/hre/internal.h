// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef HRE_INTERNAL_H
#define HRE_INTERNAL_H

#include <pthread.h>
#include <sys/time.h>
/* Do not change to hre/provider.h:
   include is supposed to fail if current dir is not hre.
*/
#include <provider.h>

/*
Internal details of HRE.
Should not be installed!
Should be used by HRE modules only!
*/


struct action {
    hre_receive_cb action;
    void* arg;
};

struct comm {
    array_manager_t action_man;
    struct action *action;
};

#define MAX_OPTION_GROUPS 31

extern pthread_key_t hre_key;
struct thread_context {
    int main;
    hre_context_t global;
    hre_context_t main_ctx;
    hre_context_t process_ctx;
    void* stack_bottom;
    struct timeval init_tv;
    char label[256];
};

extern struct poptOption hre_feedback_options[];
extern struct poptOption hre_boot_options[];

extern void HREinitPopt();
extern void HREinitFeedback();

/**
\brief start threads and exit when all threads have stopped.
*/
extern void HREpthreadRun(int threads);

/**
\brief Start child processes.

The children return.
The parent will exit when all children have stopped.
*/
extern void HREprocessSetup(int procs);

/// Create a single thread context.
extern hre_context_t HREcreateSingle();


struct hre_context_s{
    int me;
    int peers;
    const char *class_name;
    hre_destroy_m destroy;
    hre_abort_m abort;
    hre_exit_m exit;
    hre_yield_m yield;
    hre_yield_while_m yield_while;
    hre_cond_signal_m cond_signal;
    hre_xfer_m send;
    hre_recv_t recv_type;
    hre_xfer_m recv;
    hre_shm_get_m shm_get;
    hre_region_t msg_region;
    array_manager_t comm_man;
    struct comm *comm;
    uint32_t reduce_tag;
    hre_msg_t *reduce_msg;
    int *reduce_turn;
    int reduce_count[2];
    void* reduce_out[2];
    unit_t reduce_type[2];
    operand_t reduce_op[2];
};

static const size_t system_size=((sizeof(struct hre_context_s)+7)/8)*8;

#define SYS2USR(var) ((hre_context_t)(((char*)(var))+system_size))
#define USR2SYS(var) ((hre_context_t)(((char*)(var))-system_size))

extern void hre_init_reduce(hre_context_t ctx);

extern uint32_t HREactionCreateUnchecked(hre_context_t context,uint32_t comm,uint32_t size,hre_receive_cb response,void* response_arg);

#endif

