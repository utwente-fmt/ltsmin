#ifndef helpers_included
#define helpers_included

#include <czmq.h>
#include <zmq.h>

typedef struct Chunk {
    char *data;
    int size;
} Chunk;

typedef struct ChunkArray {
    Chunk *chunks;
    size_t size;
} ChunkArray;

typedef ChunkArray State;

typedef struct MatrixRow {
    Chunk transition_group;
    ChunkArray variables;
} MatrixRow;

typedef struct Matrix {
    size_t nr_rows;
    MatrixRow *rows; // nr_rows many
} Matrix;

typedef struct InitialResponse {
    State initial_state;
    
    ChunkArray transition_groups;
    ChunkArray variables;
    ChunkArray variable_types;
    ChunkArray state_labels;

    Matrix may_write;
    Matrix must_write;
    Matrix reads_action;
    Matrix reads_guard;
} InitialResponse;


State get_state(zmsg_t *msg);
Matrix get_matrix(zmsg_t *msg);
InitialResponse get_init_response(zmsg_t *msg);

void put_state(zmsg_t *msg, State s);
void destroy_initial_response(InitialResponse *resp);
void destroy_state(State *s);
void destroy_chunk_array(ChunkArray *arr);
void destroy_matrix(Matrix *m);

#endif
