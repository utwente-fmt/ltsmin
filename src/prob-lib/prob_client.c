#include <hre/config.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <czmq.h>
#include <zmq.h>

#include <prob_client.h>
#include <prob_helpers.h>

#include <hre/user.h>

struct prob_client {
    zsock_t* zocket;
    uint32_t id_count;
    char* file;
};

prob_client_t
prob_client_create()
{
    prob_client_t pc = (prob_client_t) RTmalloc(sizeof(struct prob_client));
    pc->id_count = 0;

    return pc;
}

void
prob_set_logstream()
{
    zsys_set_logstream(stderr);
}

void
prob_client_destroy(prob_client_t pc)
{
    RTfree(pc->file);
    RTfree(pc);
    pc = NULL;
}

void
prob_connect(prob_client_t pc, const char* file)
{
    pc->file = strdup(file);
    pc->zocket = zsock_new_req(pc->file);
    if (pc->zocket == NULL) Abort("Could not connect to zocket %s", pc->file);
}

void
prob_disconnect(prob_client_t pc)
{
    if (zsock_disconnect(pc->zocket, "%s", pc->file) != 0) Warning(info, "Could not disconnect from zocket %s", pc->file);
    zsock_destroy(&pc->zocket);
}

const char*
prob_get_zocket(prob_client_t pc)
{
    return pc->file;
}

int
receive_number(zmsg_t *response)
{
    char *nr_s = zmsg_popstr(response);
    int nr;
    int rc = sscanf(nr_s, "%d", &nr);
    assert(rc == 1); (void) rc;
    RTfree(nr_s);
    return nr;
}

void
print_chunk_array(const ProBChunkArray arr)
{
    printf("size: %ld\n", arr.size);
    for (size_t i = 0; i < arr.size; i++) {
        puts(arr.chunks[i].data);
    }
}

void print_row(const ProBMatrixRow row) {
    printf("Transition group: %s\n", row.transition_group.data);
    print_chunk_array(row.variables);
}

void print_matrix(const ProBMatrix m) {
    for (size_t i = 0; i < m.nr_rows; i++) {
        print_row(m.rows[i]);
    }
}


ProBInitialResponse
prob_init(prob_client_t pc, int is_por)
{
    Debugf("initializing ProB Zocket\n")
    zmsg_t *request = zmsg_new();
    zmsg_addstr(request, "init");
    zmsg_addstrf(request, "%d", pc->id_count);
    zmsg_addstrf(request, "%d", is_por);

    Debugf("sending message with length %zu, contents are:\n", zmsg_content_size(request));
#ifdef LTSMIN_DEBUG
    if (log_active(debug)) zmsg_print(request);
#endif

    if (zmsg_send(&request, pc->zocket) != 0) Abort("Could not send message");
    zmsg_destroy(&request);

    zmsg_t *response = zmsg_recv(pc->zocket);
    if (response == NULL) Abort("Did not receive valid response");

    Debugf("received message with length %zu, contents are:\n", zmsg_content_size(response));
#ifdef LTSMIN_DEBUG
    if (log_active(debug)) zmsg_print(response);
#endif


    ProBInitialResponse resp = prob_get_init_response(response);

    if (zmsg_size(response) != 0) Abort("Did not receive valid reponse size");

//    puts("transition groups:");
//    print_chunk_array(resp.transition_groups);
//    puts("variables");
//    print_chunk_array(resp.variables);
//    for (size_t i = 0; i < resp.variables.size; i++) {
//        printf("%s (%s)\n", resp.variables.chunks[i].data, resp.variable_types.chunks[i].data);
//    }
//    puts("state labels");
//    print_chunk_array(resp.state_labels);
//
//    puts("May Write Matrix:");
//    print_matrix(resp.may_write);
//    puts("Must Write Matrix:");
//    print_matrix(resp.must_write);
//    puts("Reads Action Matrix:");
//    print_matrix(resp.reads_action);
//    puts("Reads Guard Matrix:");
//    print_matrix(resp.reads_guard);

    zmsg_destroy(&response);
    return resp;
}

// Note: size is part of the result, it will contain the number of states.

static ProBState *
prob_next_x(prob_client_t pc, ProBState s, char *transitiongroup, int *size, char *header) {
    zmsg_t *request = zmsg_new();
    zmsg_addstr(request, header);
    zmsg_addstrf(request, "%d", pc->id_count);
    zmsg_addstr(request, transitiongroup);

    prob_put_state(request, s);

    Debugf("requesting next-state, contents:\n");
#ifdef LTSMIN_DEBUG
    if (log_active(debug)) zmsg_print(request);
#endif

    zmsg_send(&request, pc->zocket);
    zmsg_destroy(&request);
    zmsg_t *response = zmsg_recv(pc->zocket);

    Debugf("response for next-state, contents:\n");
#ifdef LTSMIN_DEBUG
    if (log_active(debug)) zmsg_print(response);
#endif

    drop_frame(response);
    drop_frame(response);

    char *nr_of_states_s = zmsg_popstr(response);
    sscanf(nr_of_states_s, "%d", size);
    RTfree(nr_of_states_s);

    ProBState *successors = RTmalloc(sizeof(ProBState) * (*size));
    int i;
    for (i = 0; i < (*size); i++) {
        successors[i] = prob_get_state(response);
    }
    zmsg_destroy(&response);
    return successors;
}

ProBState *
prob_next_state(prob_client_t pc, ProBState s, char *transitiongroup, int *size)
{
    return prob_next_x(pc, s, transitiongroup, size, "get-next-state");
}

ProBState *
prob_next_state_short_R2W(prob_client_t pc, ProBState s, char *transitiongroup, int *size)
{
    return prob_next_x(pc, s, transitiongroup, size, "X"); // next short R2W
}

ProBState *
prob_next_action(prob_client_t pc, ProBState s, char *transitiongroup, int *size)
{
    return prob_next_x(pc, s, transitiongroup, size, "next-update");
}

ProBState *
prob_next_action_short_R2W(prob_client_t pc, ProBState s, char *transitiongroup, int *size)
{
    return prob_next_x(pc, s, transitiongroup, size, "A"); // next action R2W
}

static int
prob_get_label(prob_client_t pc, ProBState s, char *label, char *header) {
    zmsg_t *request = zmsg_new();
    zmsg_addstr(request, header);
    zmsg_addstrf(request, "%d", pc->id_count);
    zmsg_addstrf(request, "DA%s", label);
    prob_put_state(request, s);
    zmsg_send(&request, pc->zocket);
    zmsg_destroy(&request);
    zmsg_t *response = zmsg_recv(pc->zocket);
    drop_frame(response);
    drop_frame(response);

    char *result_s = zmsg_popstr(response);
    int res;
    sscanf(result_s, "%d", &res);

    RTfree(result_s);
    zmsg_destroy(&response);
    return res;
}

int
prob_get_state_label(prob_client_t pc, ProBState s, char *label)
{
    return prob_get_label(pc, s, label, "get-state-label");
}

int
prob_get_state_label_short(prob_client_t pc, ProBState s, char *label)
{
    return prob_get_label(pc, s, label, "LBLs");
}

void prob_get_label_group(prob_client_t pc, ProBState s, int group, int *res) {
    zmsg_t *request = zmsg_new();
    zmsg_addstr(request, "get-state-label-group");
    zmsg_addstrf(request, "%d", pc->id_count);
    zmsg_addstrf(request, "DA%d", group);
    prob_put_state(request, s);
    zmsg_send(&request, pc->zocket);
    zmsg_destroy(&request);
    zmsg_t *response = zmsg_recv(pc->zocket);
    drop_frame(response);
    drop_frame(response);

    char *result_s;
    for (int i = 0; (result_s = zmsg_popstr(response)) != NULL; i++) {
        int r;
        sscanf(result_s, "%d", &r);
        res[i] = r;
        RTfree(result_s);
    }

    zmsg_destroy(&response);
}

void
prob_terminate(prob_client_t pc)
{
    zmsg_t *request = zmsg_new();
    zmsg_addstr(request, "terminate");
    zmsg_addstrf(request, "%d", pc->id_count);
    zmsg_send(&request, pc->zocket);
    zmsg_t *response = zmsg_recv(pc->zocket);
    zmsg_destroy(&response);
    zmsg_destroy(&request);
}

void
start_ltsmin(void)
{

    prob_client_t pc = prob_client_create();

    ProBInitialResponse initial_resp = prob_init(pc, 0);

    // Some Demo code

    /* Get the successors of the initial state. 
     DA$init_state is the transition group 
     representing the initialization. The 
     prototype does initializing and constant setup in one step.

     I don't know how LTSmin handles this, but DA$init_state is 
     always the only transition group that is possible in the initial state
     and it is never possible to execute DA$init_state in any other state.

     ProB automatically adds it as the first transition group.
     */

    int nr_successors;
    ProBState *successors = prob_next_state(pc, initial_resp.initial_state, "DA$init_state", &nr_successors);
    prob_destroy_initial_response(&initial_resp);

    // Send a get label request using the last state of the previous request
    ProBState foo = successors[nr_successors - 1];

    // DAinvariant is hard wired because I am too lazy to store it in the init code.
    // However, ProB does send the state label. 
    int val = prob_get_state_label(pc, foo, "DAinvariant");
    printf("Result: %d\n", val);

    prob_terminate(pc);

    // Cleanup
    int i;
    for (i = 0; i < nr_successors; i++) {
        prob_destroy_state(&(successors[i]));
    }
    RTfree(successors);

    prob_client_destroy(pc);

}
