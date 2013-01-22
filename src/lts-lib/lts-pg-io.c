// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <stdbool.h>

#include <hre/user.h>
#include <util-lib/rationals.h>
#include <lts-lib/lts-pg-io.h>
#include <pins-lib/pg-types.h>


void lts_write_pg (const char*name, lts_t lts) {
    Print(infoShort,"writing %s",name);
    FILE* f=fopen(name,"w");
    if (f == NULL) {
        AbortCall("Could not open %s for writing",name);
    }
    lts_set_type(lts,LTS_BLOCK);
    //int N=lts_type_get_state_length(lts->ltstype);
    int L=lts_type_get_state_label_count(lts->ltstype);
    //int K=lts_type_get_edge_label_count(lts->ltstype);
    if (L != 2){
        Abort("Number of state labels is %d, needs to be 2 for parity games.",L);
    }
    Warning(info,"Number of states: %d", lts->states);
    Warning(info,"First pass...");
    // compute max priority
    // determine if there are nodes without successors
    int max_priority = 0;
    int labels[L];
    bool first_edge = true;
    bool write_true = false;
    bool write_false = false;
    for(uint32_t src_idx=0; src_idx<lts->states; src_idx++){
        TreeUnfold(lts->prop_idx, lts->properties[src_idx], labels);
        int priority = labels[PG_PRIORITY];
        if (priority > max_priority) {
            max_priority = priority;
        }
        int player = labels[PG_PLAYER];
        if (lts->begin[src_idx] >= lts->begin[src_idx+1]){
            // no edges
            if (player==PG_AND) {
                write_true = true;
            } else if (player==PG_OR) {
                write_false = true;
            }
            //Warning(info, "State %d has no successors.",src_idx);
        }
    }
    if (max_priority%2!=0)
    {
        // when converting from min to max-priority game,
        // the maximum priority should be even.
        max_priority++;
    }
    Warning(info,"Second pass...");
    bool min_game = false;
    int max_id = lts->states;
    int offset = 0;
    int true_idx = 0;
    int false_idx = 0;
    if (write_true) {
        true_idx = max_id;
        max_id++;
    }
    if (write_false) {
        false_idx = max_id;
        max_id++;
    }
    // write header.
    fprintf(f,"parity %d;\n",max_id-1);
    // write states and edges
    for(uint32_t src_idx=0; src_idx<lts->states; src_idx++){
        if (src_idx > 0){
            fprintf(f,";\n");
        }
        TreeUnfold(lts->prop_idx, lts->properties[src_idx], labels);
        int priority = min_game ? labels[PG_PRIORITY] : max_priority-labels[PG_PRIORITY];
        int player = labels[1];
        fprintf(f, "%d %d %d ", src_idx+offset, priority /* priority */, player /* player */);
        first_edge = true;
        for(uint32_t edge_idx=lts->begin[src_idx]; edge_idx<lts->begin[src_idx+1]; edge_idx++){
            if (!first_edge){
                fprintf(f, ",");
            }
            fprintf(f,"%d",lts->dest[edge_idx]+offset);
            first_edge = false;
        }
        if (first_edge)
        {
            //Warning(info,"State %d has no successors.",src_idx);
            // add transition to true/false node
            fprintf(f,"%d",((player==PG_AND) ? true_idx : false_idx));
        }
    }
    fprintf(f,";\n");
    // write true and false
    if (write_true)
    {
        fprintf(f, "%d %d %d ", true_idx, min_game ? 0 : max_priority /* priority */, PG_AND /* player */);
        fprintf(f,"%d;\n",true_idx);
    }
    if (write_false)
    {
        fprintf(f, "%d %d %d ", false_idx, min_game ? 1 : max_priority-1 /* priority */, PG_OR /* player */);
        fprintf(f,"%d;\n",false_idx);
    }
    fclose(f);
}
