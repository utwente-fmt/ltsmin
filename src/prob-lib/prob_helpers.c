#include <hre/config.h>

#include <stdlib.h>
#include <stdio.h>
#include <czmq.h>
#include <zmq.h>

#include <hre/runtime.h>
#include <hre/unix.h>

#include <prob_helpers.h>

void
drop_frame(zmsg_t *msg)
{
    zframe_t *frame = zmsg_pop(msg);
    assert(frame);
    zframe_destroy(&frame);
}

int get_number(zmsg_t *msg) {
    char *nr_s = zmsg_popstr(msg);
    int nr;
    int rc = sscanf(nr_s,"%d",&nr);
    assert(rc==1); (void) rc;
    RTfree(nr_s);

    return nr;
}

ProBChunk get_chunk(zmsg_t *msg) {
    ProBChunk res;

    zframe_t *chunk_f = zmsg_pop(msg);
    int chunk_size = zframe_size(chunk_f);
    char *chunk = zframe_strdup(chunk_f);
    zframe_destroy(&chunk_f);
    
    res.data = chunk;
    res.size = chunk_size;

    return res;
}

ProBChunkArray get_chunk_array(zmsg_t *msg) {
    ProBChunkArray res;
    res.size = get_number(msg);
    res.chunks = RTmalloc(sizeof(ProBChunk) * res.size);
    
    for (size_t i = 0; i < res.size; i++) {
        res.chunks[i] = get_chunk(msg);
    }

    return res;
}

ProBState prob_get_state(zmsg_t *msg) {
    return get_chunk_array(msg);
}

ProBInitialResponse prob_get_init_response(zmsg_t *msg) {
    ProBInitialResponse res;
    memset(&res, 0, sizeof(res));
    drop_frame(msg); // message type
    drop_frame(msg); // ID

    char *submsg_header;
    while ((submsg_header = zmsg_popstr(msg)) != NULL) {
        enum SubMessageType header = atoi(submsg_header);
        free(submsg_header);

        int len = get_number(msg);

        switch (header) {
            case initial_state:
                res.initial_state = prob_get_state(msg); break;
            case transition_group_list:
                res.transition_groups = get_chunk_array(msg); break;
            case variables_name_list:
                res.variables = get_chunk_array(msg); break;
            case variables_type_list:
                res.variable_types = get_chunk_array(msg); break;
            case state_label_matrix:
                res.state_labels = prob_get_matrix(msg); break;
            case may_write_matrix:
                res.may_write = prob_get_matrix(msg); break;
            case must_write_matrix:
                res.must_write = prob_get_matrix(msg); break;
            case reads_action_matrix:
                res.reads_action = prob_get_matrix(msg); break;
            case reads_guard_matrix:
                res.reads_guard = prob_get_matrix(msg); break;
            case guard_info_matrix:
                res.guard_info = prob_get_matrix(msg); break;
            case guard_label_matrix:
                res.guard_labels = prob_get_matrix(msg); break;
            case necessary_enabling_set:
                res.necessary_enabling_set = prob_get_matrix(msg); break;
            case necessary_disabling_set:
                res.necessary_disabling_set = prob_get_matrix(msg); break;
            case do_not_accord_matrix:
                res.do_not_accord = prob_get_matrix(msg); break;
            case may_be_coenabled_matrix:
                res.may_be_coenabled = prob_get_matrix(msg); break;
            case ltl_label_matrix:
                res.ltl_labels = prob_get_matrix(msg); break;
            default:
                for (; len > 0; len--) {
                    drop_frame(msg);
                }
        }
    }
    

    return res;
}

ProBMatrixRow get_row(zmsg_t *msg) {
    ProBMatrixRow res;
    res.transition_group = get_chunk(msg);
    res.variables = get_chunk_array(msg);

    return res;
}


ProBMatrix prob_get_matrix(zmsg_t *msg) {
    ProBMatrix m;
    m.nr_rows = get_number(msg);

    m.rows = (ProBMatrixRow*) calloc(m.nr_rows, sizeof(ProBMatrixRow));

    for (size_t i = 0; i < m.nr_rows; i++) {
        m.rows[i] = get_row(msg);
    }

    return m;
}

void prob_put_state(zmsg_t *msg, ProBState s) {
    zmsg_addstrf(msg, "%zu", s.size);

    for (size_t i = 0; i < s.size; i++) {
        zframe_t *frame = zframe_new(s.chunks[i].data, s.chunks[i].size);
        zmsg_append(msg,&frame);
    }
}


void prob_destroy_chunk_array(ProBChunkArray *arr) {
    RTfree(arr->chunks);
    arr->chunks = NULL;
    *arr = (ProBChunkArray) {NULL, 0};
}

void prob_destroy_state(ProBState *s) {
    prob_destroy_chunk_array(s);
}

void destroy_chunk(ProBChunk *norris) {
    RTfree(norris->data);
    *norris = (ProBChunk) {NULL, 0};
}

void destroy_matrix_row(ProBMatrixRow row) {
    destroy_chunk(&(row.transition_group));
    prob_destroy_chunk_array(&(row.variables));
}

void prob_destroy_matrix(ProBMatrix *m) {

    for (size_t i = 0; i < m->nr_rows; i++) {
        destroy_matrix_row(m->rows[i]);
    }
    RTfree(m->rows);
    *m = (ProBMatrix) {0, NULL};
}

void prob_destroy_initial_response(ProBInitialResponse *resp) {
    prob_destroy_state(&(resp->initial_state));

    prob_destroy_chunk_array(&(resp->transition_groups));
    prob_destroy_chunk_array(&(resp->variables));
    prob_destroy_matrix(&(resp->state_labels));

    prob_destroy_matrix(&resp->may_write);
    prob_destroy_matrix(&resp->must_write);
    prob_destroy_matrix(&resp->reads_action);
    prob_destroy_matrix(&resp->reads_guard);
//    *resp = (InitialResponse) {0};
}
