// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <hre/user.h>
#include <util-lib/rationals.h>
#include <lts-lib/lts.h>
#include <hre/stringindex.h>

#define STATE_FMT "S%06u"

void lts_write_imca(const char*imca_name,lts_t lts){
    FILE* imca=fopen(imca_name,"w");
    if (imca == NULL) {
        AbortCall("Could not open %s for writing",imca_name);
    }
    lts_set_type(lts,LTS_BLOCK);
    int reward_pos=lts_type_find_edge_label(lts->ltstype,"reward_numerator");
    int action_pos=lts_type_find_edge_label(lts->ltstype,"action");
    if (action_pos<0) Abort("imca format requires actions");
    int group_pos=lts_type_find_edge_label(lts->ltstype,"group");
    int param_pos=lts_type_find_edge_label(lts->ltstype,"numerator");


    int action_type=lts_type_get_edge_label_typeno(lts->ltstype,action_pos);
    uint32_t tau=(uint32_t)VTputChunk(lts->values[action_type],chunk_str("tau"));
    Warning(info,"tau = %u",tau);
    uint32_t rate=(uint32_t)VTputChunk(lts->values[action_type],chunk_str("rate"));
    Warning(info,"rate = %u",rate);
    
    fprintf(imca,"#INITIALS\n");
    for(uint32_t i=0;i<lts->root_count;i++){
      fprintf(imca,STATE_FMT "\n",lts->root_list[i]);
    }
    fprintf(imca,"#GOALS\n");
    if (lts->properties!=NULL){
        for(uint32_t i=0;i<lts->states;i++){
            if (lts->properties[i]!=0){
                fprintf(imca,STATE_FMT "\n",i);
            }
        }
    }
    fprintf(imca,"#TRANSITIONS\n");
    for(uint32_t i=0;i<lts->states;i++){
        for(uint32_t j=lts->begin[i];j<lts->begin[i+1];){
            uint32_t label[6];
            TreeUnfold(lts->edge_idx,lts->label[j],(int*)label);
            int32_t group=label[group_pos];
            if (label[action_pos]==tau){
                fprintf(imca,STATE_FMT " tau",i);
            } else if (label[action_pos]==rate) {
                if (j==lts->begin[i])
                    fprintf(imca,STATE_FMT " !",i);
            } else {
                chunk label_c=VTgetChunk(lts->values[action_type],label[action_pos]);
                char label_s[label_c.len*2+6];
                chunk2string(label_c,sizeof label_s,label_s);
                fprintf(imca,STATE_FMT " %s",i,label_s);
            }
            if (reward_pos>=0 && label[reward_pos]!=0){
                fprintf(imca," %.15e\n",((float)label[reward_pos])/(float)label[reward_pos+1]);
            } else {
                fprintf(imca,"\n");
            }
            do {
                fprintf(imca,"* " STATE_FMT " %.15e\n",lts->dest[j],((float)label[param_pos])/(float)label[param_pos+1]);
                j++;
                if (j<lts->begin[i+1])
                    TreeUnfold(lts->edge_idx,lts->label[j],(int*)label);
            } while (j<lts->begin[i+1]&&group==label[group_pos]);
        }
    }
    fclose(imca);
}

