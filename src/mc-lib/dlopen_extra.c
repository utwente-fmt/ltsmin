

#include <hre/config.h>

#include <hre/user.h>
#include <pins-lib/dlopen-api.h>
#include <mc-lib/dlopen_extra.h>

get_new_initial_state_proc        get_new_initial_state;
get_worker_initial_state_proc     get_worker_initial_state;
next_state_proc                   next_state_p;

void
dlopen_setup (char *name)
{
   void *dlHandle = RTdlopen(name);
   get_new_initial_state    = RTtrydlsym(dlHandle,"get_new_initial_state");
   get_worker_initial_state = RTtrydlsym(dlHandle,"get_worker_initial_state");
   next_state_p             = RTtrydlsym(dlHandle,"next_state");
}

int
dlopen_get_new_initial_state (int prev_initial)
{
    if (get_new_initial_state != NULL)
        return get_new_initial_state(prev_initial);

    HREassert(0, "Cannot use get_new_initial_state function");
    return -1;
}

int
dlopen_get_worker_initial_state (int worker_id, int total_workers)
{
    if (get_worker_initial_state != NULL)
        return get_worker_initial_state(worker_id, total_workers);

    HREassert(0, "Cannot use get_worker_initial_state function");
    return -1;
}


int
dlopen_next_state (model_t model, int group, int *src, TransitionCB cb, void *arg)
{
    if (next_state_p != NULL)
        return next_state_p (model, group, src, cb, arg);

    HREassert(0, "Cannot use next_state function");
    return -1;
}
