

typedef int(*next_state_proc)(model_t model, int group, int *src, TransitionCB cb, void *arg);

typedef int(*get_new_initial_state_proc)(int prev_initial);

typedef int(*get_worker_initial_state_proc)(int worker_id, int total_workers);

// setting up the called methods
void dlopen_setup (char *name);

// request a new, unvisited, initial state (for full-graph exploration)
int dlopen_get_new_initial_state (int prev_initial);

// retrieve worker-specific initial states (evenly divided over the states)
// used for full-graph exploration
int dlopen_get_worker_initial_state (int worker_id, int total_workers);

// directly communicate with the next state function (bypass hashing)
// return -1 if every state has been visited
int dlopen_next_state (model_t model, int group, int *src, TransitionCB cb, void *arg);
