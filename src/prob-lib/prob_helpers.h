#ifndef helpers_included
#define helpers_included

#include <czmq.h>
#include <zmq.h>

enum SubMessageType {
    initial_state = 1,
    transition_group_list,
    variables_name_list,
    variables_type_list,
    state_label_matrix,
    may_write_matrix,
    must_write_matrix,
    reads_action_matrix,
    reads_guard_matrix
};

typedef struct ProBChunk {
    char *data;
    int size;
} ProBChunk;

typedef struct ProBChunkArray {
    ProBChunk *chunks;
    size_t size;
} ProBChunkArray;

typedef ProBChunkArray ProBState;

typedef struct MatrixRow {
    ProBChunk transition_group;
    ProBChunkArray variables;
} ProBMatrixRow;

typedef struct ProBMatrix {
    size_t nr_rows;
    ProBMatrixRow *rows; // nr_rows many
} ProBMatrix;

typedef struct ProBInitialResponse {
    ProBState initial_state;
    
    ProBChunkArray transition_groups;
    ProBChunkArray variables;
    ProBChunkArray variable_types;
    ProBMatrix state_labels;

    ProBMatrix may_write;
    ProBMatrix must_write;
    ProBMatrix reads_action;
    ProBMatrix reads_guard;
} ProBInitialResponse;


ProBState prob_get_state(zmsg_t *msg);
ProBMatrix prob_get_matrix(zmsg_t *msg);
ProBInitialResponse prob_get_init_response(zmsg_t *msg);

void prob_put_state(zmsg_t *msg, ProBState s);
void prob_destroy_initial_response(ProBInitialResponse *resp);
void prob_destroy_state(ProBState *s);
void prob_destroy_chunk_array(ProBChunkArray *arr);
void prob_destroy_matrix(ProBMatrix *m);

void drop_frame(zmsg_t *msg);

#endif
