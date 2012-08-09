// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre-io/user.h>
#include <lts-lib/dir-info.h>

dir_info_t DIRinfoCreate(int segment_count){
    dir_info_t info=HRE_NEW(hre_heap,struct dir_info_s);
    info->segment_count=segment_count;
    info->state_count=(int*)HREmallocZero(hre_heap,segment_count*sizeof(int));
    info->transition_count=(int**)HREmallocZero(hre_heap,segment_count*sizeof(int*));
    for(int i=0;i<segment_count;i++) {
        info->transition_count[i]=(int*)HREmallocZero(hre_heap,segment_count*sizeof(int));
    }
    info->label_tau=-1;
    return info;
}

void DIRinfoDestroy(dir_info_t info){
    for(int i=0;i<info->segment_count;i++) {
        HREfree(hre_heap,info->transition_count[i]);
    }
    HREfree(hre_heap,info->transition_count);
    HREfree(hre_heap,info->state_count);
    HREfree(hre_heap,info);
}

dir_info_t DIRinfoRead(stream_t input,int check_magic){
    if(check_magic){
        int version=DSreadS32(input);
        if (version!=31) {
            Abort("input is not a DIR info file");
        }
    }
    char *comment=DSreadSA(input);
    int segment_count=DSreadS32(input);
    dir_info_t info=DIRinfoCreate(segment_count);
    info->info=comment;
    info->initial_seg=DSreadS32(input);
    info->initial_ofs=DSreadS32(input);
    info->label_count=DSreadS32(input);
    info->label_tau=DSreadS32(input);
    info->top_count=DSreadS32(input);
    for(int i=0;i<segment_count;i++){
        info->state_count[i]=DSreadS32(input);
    }
    for(int i=0;i<segment_count;i++){
        for(int j=0;j<segment_count;j++){
            info->transition_count[i][j]=DSreadS32(input);
        }
    }
    return info;
}

void DIRinfoWrite(stream_t output,dir_info_t info){
    DSwriteS32(output,31);
    if (info->info==NULL){
        DSwriteS16(output,0);
    } else {
        DSwriteS(output,info->info);
    }
    DSwriteS32(output,info->segment_count);
    DSwriteS32(output,info->initial_seg);
    DSwriteS32(output,info->initial_ofs);
    DSwriteS32(output,info->label_count);
    DSwriteS32(output,info->label_tau);
    DSwriteS32(output,info->top_count);
    for(int i=0;i<info->segment_count;i++){
        DSwriteS32(output,info->state_count[i]);
    }
    for(int i=0;i<info->segment_count;i++){
        for(int j=0;j<info->segment_count;j++){
            DSwriteS32(output,info->transition_count[i][j]);
        }
    }
}
