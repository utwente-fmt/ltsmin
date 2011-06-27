// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>

#include <stdlib.h>
#include <stdbool.h>

#include <hre/user.h>
#include <lts-lib/rationals.h>
#include <lts-lib/lts.h>

//FIXME
void lts_write_pg(const char*name,lts_t lts){
    Print(infoShort,"writing %s",name);
    FILE* f=fopen(name,"w");
    if (f == NULL) {
        AbortCall("Could not open %s for writing",name);
    }
    lts_set_type(lts,LTS_BLOCK);
    int N=lts_type_get_state_length(lts->ltstype);
    int L=lts_type_get_state_label_count(lts->ltstype);
    int K=lts_type_get_edge_label_count(lts->ltstype);
    if (L != 2){
        Abort("Number of state labels is %d, needs to be 2 for parity games.",L);
    }
    Warning(info,"Number of states: %d", lts->states);
    // compute max priority
    int max_priority = 0;
    int labels[L];
    for(uint32_t src_idx=0; src_idx<lts->states; src_idx++){
        TreeUnfold(lts->prop_idx, lts->properties[src_idx], labels);
        int priority = labels[0];
        if (priority > max_priority) {
            max_priority = priority;
        }
    }
    bool min_game = false;
    // write header.
    fprintf(f,"parity %d;\n",lts->states+1);
    // write states and edges
    bool first_edge = true;
    for(uint32_t src_idx=0; src_idx<lts->states; src_idx++){
        if (src_idx > 0){
            fprintf(f,";\n");
        }
        TreeUnfold(lts->prop_idx, lts->properties[src_idx], labels);
        int priority = min_game ? labels[0] : max_priority-labels[0];
        fprintf(f, "%d %d %d ", src_idx, priority /* priority */, labels[1] /* player */);
        first_edge = true;
        for(uint32_t edge_idx=lts->begin[src_idx]; edge_idx<lts->begin[src_idx+1]; edge_idx++){
            if (!first_edge){
                fprintf(f, ",");
            }
            fprintf(f,"%d",lts->dest[edge_idx]);
            first_edge = false;
        }
    }
    fprintf(f,";\n");
    fclose(f);
}
