// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <lts-lib/lts.h>

#define MAP_UNDEF ((uint32_t)-1)

void lts_bfs_reorder(lts_t lts) {
    uint32_t i,j,k;
    uint32_t *map,*repr;
    LTS_TYPE orig_type=lts->type;

    if (lts->state_db!=NULL) {
        Abort("cannot reorder an LTS with state vectors");
    }
    Debug("starting BFS reordering");
    Debug("original LTS has %u roots, %u states and %u transitions",lts->root_count,lts->states,lts->transitions);
    lts_set_type(lts,LTS_BLOCK);
    Debug("sorted lts into blocked format");
    map=(uint32_t*)RTmalloc(lts->states*sizeof(uint32_t));
    repr=(uint32_t*)RTmalloc(lts->states*sizeof(uint32_t));
    for(i=0;i<lts->states;i++){
        map[i]=MAP_UNDEF;
    }
    for(i=0;i<lts->root_count;i++){
        repr[i]=lts->root_list[i];
        map[lts->root_list[i]]=i;
    }
    i=0;
    j=lts->root_count;
    while(i<j){
        for(k=lts->begin[repr[i]];k<lts->begin[repr[i]+1];k++){
            if (map[lts->dest[k]]==MAP_UNDEF) {
                map[lts->dest[k]]=j;
                repr[j]=lts->dest[k];
                j++;
            }
        }
        i++;
    }
    Debug("created map");
    lts_set_type(lts,LTS_LIST);
    Debug("transformed into list representation");
    if (i<lts->states) {
        Debug("only %d out of %d states reachable",i,lts->states);
        lts->states=i;
    }
    j=0;
    for(i=0;i<lts->transitions;i++){
        if (map[lts->src[i]]==MAP_UNDEF) continue;
        lts->src[j]=map[lts->src[i]];
        lts->dest[j]=map[lts->dest[i]];
        j++;
    }
    if (j<lts->transitions) {
        Debug("only %d out of %d transitions reachable",j,lts->transitions);
        lts->transitions=j;
    }
    for(i=0;i<lts->root_count;i++){
        lts->root_list[i]=i;
    }
    if (lts->properties!=NULL){
        Debug("adjusting properties");
        uint32_t *props=lts->properties;
        lts->properties=map;
        for(i=0;i<lts->states;i++){
            lts->properties[i]=props[repr[i]];
        }
        RTfree(repr);
        RTfree(props);
    } else {
        RTfree(repr);
        RTfree(map);
    }
    
    Debug("applied map");
    lts_set_type(lts,orig_type);
    Debug("original format restored");
    lts_set_size(lts,lts->root_count,lts->states,lts->transitions);
    Debug("resulting LTS has %u roots, %u states and %u transitions",lts->root_count,lts->states,lts->transitions);
}

