// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>
#include <hre/user.h>
#include <lts-lib/lts.h>

void lts_find_deadlocks(lts_t lts,bitset_t deadlocks){
    bitset_clear_all(deadlocks);
    if (lts->type==LTS_BLOCK){
        for(uint32_t i=0;i<lts->states;i++){
            if(lts->begin[i]==lts->begin[i+1]) bitset_set(deadlocks,i);
        }
    } else {
        bitset_set_range(deadlocks,0,lts->states-1);
        for(uint32_t i=0;i<lts->transitions;i++){
            bitset_clear(deadlocks,lts->src[i]);
        }
    }
}

void lts_find_divergent(lts_t lts,silent_predicate silent,void*silent_context,bitset_t divergent){
    uint32_t* count=(uint32_t*)RTmallocZero(lts->states*sizeof(uint32_t));
    uint32_t* stack=(uint32_t*)RTmalloc(lts->states*sizeof(uint32_t));
    uint32_t stack_ptr=0;
    lts_set_type(lts,LTS_BLOCK_INV);
    Debug("counting outgoing silent steps.");
    for(uint32_t i=0;i<lts->states;i++){
        for(uint32_t j=lts->begin[i];j<lts->begin[i+1];j++){
            uint32_t src=lts->src[j];
            if (silent(silent_context,lts,src,j,i)) count[src]++;
        }
    }
    Debug("queueing trivial non-divergent states");
    for(uint32_t i=0;i<lts->states;i++){
        if (count[i]==0){
            stack[stack_ptr]=i;
            stack_ptr++;
        }
    }
    Debug("propagation of non-divergence");
    while(stack_ptr>0){
        stack_ptr--;
        uint32_t i=stack[stack_ptr];
        for(uint32_t j=lts->begin[i];j<lts->begin[i+1];j++){
            uint32_t src=lts->src[j];
            if (silent(silent_context,lts,lts->src[j],j,i)){
                count[src]--;
                if (count[src]==0){
                    stack[stack_ptr]=src;
                    stack_ptr++;  
                }
            }
        }
    }
    Debug("mark divergent states");
    bitset_clear_all(divergent);
    for(uint32_t i=0;i<lts->states;i++){
        if (count[i]>0){
            bitset_set(divergent,i);
        }
    }
    Debug("clear temp space");
    RTfree(stack);
    RTfree(count);
}

