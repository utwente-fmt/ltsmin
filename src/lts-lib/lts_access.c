// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <lts-lib/lts.h>
#include <hre/user.h>

uint32_t lts_state_get_label(lts_t lts,uint32_t state_no,uint32_t label_no){
    if (lts->prop_idx!=NULL){
        return (uint32_t)TreeDBSGet(lts->prop_idx,lts->properties[state_no],(int)label_no);
    } else if (label_no==0) {
        return lts->properties[state_no];
    } else {
        Abort("illegal index %d",label_no);
    }
}




