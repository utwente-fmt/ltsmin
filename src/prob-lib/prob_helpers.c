#include <stdlib.h>
#include <stdio.h>
#include <czmq.h>
#include <zmq.h>

#include <hre/runtime.h>
#include <hre/unix.h>

#include <prob_helpers.h>

int get_number(zmsg_t *msg) {
    char *nr_s = zmsg_popstr(msg);
    int nr;
    int rc = sscanf(nr_s,"%d",&nr);
    assert(rc==1);
    RTfree(nr_s);

    return nr;
}

Chunk get_chunk(zmsg_t *msg) {
    Chunk res;

    zframe_t *chunk_f = zmsg_pop(msg);
    int chunk_size = zframe_size(chunk_f);
    char *chunk = zframe_strdup(chunk_f);
    zframe_destroy(&chunk_f);
    
    res.data = chunk;
    res.size = chunk_size;

    return res;
}

ChunkArray get_chunk_array(zmsg_t *msg) {
    ChunkArray res;
    res.size = get_number(msg);
    res.chunks = RTmalloc(sizeof(Chunk) * res.size);
    
    for (size_t i = 0; i < res.size; i++) {
        res.chunks[i] = get_chunk(msg);
    }

    return res;
}

State get_state(zmsg_t *msg) {
    return get_chunk_array(msg);
}


InitialResponse get_init_response(zmsg_t *msg) {

    InitialResponse res;

    res.initial_state = get_state(msg);

    res.transition_groups = get_chunk_array(msg);
    res.variables = get_chunk_array(msg);
    res.variable_types = get_chunk_array(msg);
    res.state_labels = get_chunk_array(msg);


    res.may_write = get_matrix(msg);
    res.must_write = get_matrix(msg);
    res.reads_action = get_matrix(msg);
    res.reads_guard = get_matrix(msg);
    return res;
}


MatrixRow get_row(zmsg_t *msg) {
    MatrixRow res;
    res.transition_group = get_chunk(msg);
    res.variables = get_chunk_array(msg);

    return res;
}


Matrix get_matrix(zmsg_t *msg) {
    Matrix m;
    m.nr_rows = get_number(msg);

    m.rows = (MatrixRow*) calloc(m.nr_rows, sizeof(MatrixRow));

    for (size_t i = 0; i < m.nr_rows; i++) {
        m.rows[i] = get_row(msg);
    }

    return m;
}

void put_state(zmsg_t *msg, State s) {
    zmsg_addstrf(msg, "%zu", s.size);

    for (size_t i = 0; i < s.size; i++) {
        zframe_t *frame = zframe_new(s.chunks[i].data, s.chunks[i].size);
        zmsg_append(msg,&frame);
    }
}


void destroy_chunk_array(ChunkArray *arr) {
    RTfree(arr->chunks);
    arr->chunks = NULL;
    *arr = (ChunkArray) {0};
}

void destroy_state(State *s) {
    destroy_chunk_array(s);
}

void destroy_chunk(Chunk *norris) {
    RTfree(norris->data);
    *norris = (Chunk) {0};
}

void destroy_matrix_row(MatrixRow row) {
    destroy_chunk(&(row.transition_group));
    destroy_chunk_array(&(row.variables));
}

void destroy_matrix(Matrix *m) {

    for (size_t i = 0; i < m->nr_rows; i++) {
        destroy_matrix_row(m->rows[i]);
    }
    RTfree(m->rows);
    *m = (Matrix) {0};
}

void destroy_initial_response(InitialResponse *resp) {
    destroy_state(&(resp->initial_state));

    destroy_chunk_array(&(resp->transition_groups));
    destroy_chunk_array(&(resp->variables));
    destroy_chunk_array(&(resp->state_labels));

    destroy_matrix(&resp->may_write);
    destroy_matrix(&resp->must_write);
    destroy_matrix(&resp->reads_action);
    destroy_matrix(&resp->reads_guard);
//    *resp = (InitialResponse) {0};
}
