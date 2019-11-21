#ifndef PNML_GREYBOX_H
#define PNML_GREYBOX_H

#include <popt.h>

#include <hre/stringindex.h>
#include <hre/user.h>
#include <pins-lib/pins.h>

typedef enum { ARC_IN, ARC_OUT, ARC_LAST } arc_dir_t;

typedef struct {
    int transition;
    int place;
    int num;
    arc_dir_t type;
    int safe;
} arc_t;

typedef struct {
    int start;
    int in_arcs;
    int out_arcs;
    int label;
} transition_t;

typedef struct {
    char *name;
    int num_transitions;
    transition_t *transitions;
    int num_arcs;
    arc_t *arcs;

    string_index_t place_names;
    string_index_t edge_labels;

    int *groups_of_edges;
    int *groups_of_edges_begin;

    guard_t* *guards_info;
    int num_guards;
    arc_t* *guards; // not allowed to access transition!
    int num_in_arcs;

    rt_timer_t timer;
    int* init_state;
    int has_safe_places;
} pn_context_t;

typedef struct {
    pn_context_t *pn_context;
    int error;
    array_manager_t arc_man;
    array_manager_t trans_man;
    array_manager_t init_man;
} andl_context_t;

extern struct poptOption pnml_options[];

extern void PNMLloadGreyboxModel(model_t model, const char* name);
extern void ANDLloadGreyboxModel(model_t model, const char* name);

#endif
