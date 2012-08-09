// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#include <hre/config.h>
#include <hre/user.h>
#include <lts-lib/lts.h>

void lts_print(log_t log,lts_t lts){
    int has_labels=lts->label!=NULL;
    int has_props=lts->properties!=NULL;
    for(uint32_t i=0;i<lts->root_count;i++){
        Print(log,"init: %u",lts->root_list[i]);
    }
    switch(lts->type){
    case LTS_BLOCK:
        for(uint32_t i=0;i<lts->states;i++){
            if (has_props){
                Print(log,"state %u: %u",i,lts->properties[i]);
            } else {
                Print(log,"state %u",i);
            }
            for(uint32_t j=lts->begin[i];j<lts->begin[i+1];j++){
                if (has_labels){
                    Print(log,"  -- %u --> %u",lts->label[j],lts->dest[j]);
                } else {
                    Print(log,"  --> %u",lts->dest[j]);
                }
            }
        }
        break;
    case LTS_LIST:
        if (has_props){
            for(uint32_t i=0;i<lts->states;i++){
                Print(log,"state %u: %u",i,lts->properties[i]);
            }
        }
        for(uint32_t i=0;i<lts->transitions;i++){
            if (has_labels){
                Print(log,"%u -- %u --> %u",lts->src[i],lts->label[i],lts->dest[i]);
            } else {
                Print(log,"%u --> %u",lts->src[i],lts->dest[i]);
            }
        }
        break;
    case LTS_BLOCK_INV:
        for(uint32_t i=0;i<lts->states;i++){
            if (has_props){
                Print(log,"state %u: %u",i,lts->properties[i]);
            } else {
                Print(log,"state %u",i);
            }
            for(uint32_t j=lts->begin[i];j<lts->begin[i+1];j++){
                if (has_labels){
                    Print(log,"  <-- %u -- %u",lts->label[j],lts->src[j]);
                } else {
                    Print(log,"  <-- %u",lts->src[j]);
                }
            }
        }
        break;
    }
}

