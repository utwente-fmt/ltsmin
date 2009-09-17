#ifndef TRACE_H
#define TRACE_H

#include <treedbs.h>
#include <lts-type.h>
#include <stringindex.h>

typedef struct trace_s {
    int len;
    lts_type_t ltstype;
    string_index_t* values;
    treedbs_t state_db;
    treedbs_t map_db; // NULL als aantal defined state labels == 0
    treedbs_t edge_db; // NULL als aantal edges labels == 0
    int *state_lbl; // NULL als geen definined state labels
    int *edge_lbl; // NULL als geen edge labels
} *trace_t;

extern trace_t read_trace(const char *name);

#endif
