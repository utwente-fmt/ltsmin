// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>

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
    if (i<lts->states) {
        Abort("only %d out of %d states reachable",i,lts->states);
    }
    RTfree(repr);
    Debug("created map");
    lts_set_type(lts,LTS_LIST);
    Debug("transformed into list representation");
    for(i=0;i<lts->transitions;i++){
        lts->src[i]=map[lts->src[i]];
        lts->dest[i]=map[lts->dest[i]];
    }
    for(i=0;i<lts->root_count;i++){
        lts->root_list[i]=i;
    }
    if (lts->properties!=NULL){
        uint32_t *props=lts->properties;
        lts->properties=repr;
        for(i=0;i<lts->states;i++){
            lts->properties[map[i]]=props[i];
        }
        RTfree(props);
    } else {
        RTfree(map);
    }
    Debug("applied map");
    lts_set_type(lts,orig_type);
    Debug("original format restored");
}

