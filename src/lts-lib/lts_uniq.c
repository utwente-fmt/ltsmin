// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <assert.h>

#include <hre/user.h>
#include <lts-lib/lts.h>
#include <util-lib/dynamic-array.h>

static uint32_t merge_uniq(
    uint32_t *dst1,uint32_t *dst2,
    uint32_t *a1,uint32_t *a2,uint32_t a_size,
    uint32_t *b1,uint32_t *b2,uint32_t b_size
){
    uint32_t a_ptr=0;
    uint32_t b_ptr=0;
    uint32_t d_ptr=0;
    for(;;){
        if ((a1[a_ptr]<b1[b_ptr]) || (a1[a_ptr]==b1[b_ptr] && a2[a_ptr]<=b2[b_ptr])){
        // head of a <= head of b
            dst1[d_ptr]=a1[a_ptr];
            dst2[d_ptr]=a2[a_ptr];
            if (a1[a_ptr]==b1[b_ptr] && a2[a_ptr]==b2[b_ptr]) {
                // head of a == head of b
                b_ptr++;
            }
            a_ptr++;
            d_ptr++;
        } else {
        // head of b < head of a
            dst1[d_ptr]=b1[b_ptr];
            dst2[d_ptr]=b2[b_ptr];
            d_ptr++;
            b_ptr++;
        }
        if(a_ptr==a_size){
            while(b_ptr<b_size){
                dst1[d_ptr]=b1[b_ptr];
                dst2[d_ptr]=b2[b_ptr];
                d_ptr++;
                b_ptr++;
            }
            return d_ptr;
        }
        if(b_ptr==b_size){
            while(a_ptr<a_size){
                dst1[d_ptr]=a1[a_ptr];
                dst2[d_ptr]=a2[a_ptr];
                d_ptr++;
                a_ptr++;
            }
            return d_ptr;
        }
    }
}


static uint32_t merge_sort_uniq_tmp(
    uint32_t *dst1,uint32_t *dst2,
    uint32_t *tmp1,uint32_t *tmp2,
    uint32_t *src1,uint32_t *src2,
    uint32_t size
);

// assume size>0 /\ no overlap.
static uint32_t merge_sort_uniq_two(
    uint32_t *dst1,uint32_t *dst2,
    uint32_t *src1,uint32_t *src2,
    uint32_t size
){
    if (size==1) {
        dst1[0]=src1[0];
        dst2[0]=src2[0];
        return 1;
    }
    uint32_t small_half=(size)/2; // first half is small.
    uint32_t big_half=(size+1)/2; // second half is big.
    // sort second half into second half of dst.
    uint32_t size1=merge_sort_uniq_two(&dst1[small_half],&dst2[small_half],&src1[small_half],&src2[small_half],big_half);
    // sort first half in place, using second half as temp.
    uint32_t size2=merge_sort_uniq_tmp(src1,src2,&src1[small_half],&src2[small_half],src1,src2,small_half);
    // merge results
    return merge_uniq(dst1,dst2,&dst1[small_half],&dst2[small_half],size1,src1,src2,size2);
}


// assume size>0 /\ tail of dst may overlap with head of src
static uint32_t merge_sort_uniq_tmp(
    uint32_t *dst1,uint32_t *dst2,
    uint32_t *tmp1,uint32_t *tmp2,
    uint32_t *src1,uint32_t *src2,
    uint32_t size
){
    if (size==1) {
        dst1[0]=src1[0];
        dst2[0]=src2[0];
        return 1;
    }
    uint32_t small_half=(size)/2; // first half is small.
    uint32_t big_half=(size+1)/2; // second half is big.
    // sort second half into tmp.
    uint32_t size1=merge_sort_uniq_two(tmp1,tmp2,&src1[small_half],&src2[small_half],big_half);
    // sort first half into second half.
    uint32_t size2=merge_sort_uniq_two(&src1[big_half],&src2[big_half],src1,src2,small_half);
    // merge to destination.
    return merge_uniq(dst1,dst2,tmp1,tmp2,size1,&src1[big_half],&src2[big_half],size2);
}

void lts_uniq(lts_t lts){
    Debug("removing duplicate transitions");
    int has_labels=lts->label!=NULL && lts->transitions>0;
    if (!has_labels) {
        Debug("creating dummy labels");
        lts->label=(uint32_t*)RTmallocZero(4*lts->transitions);
    }
    uint32_t i,j,count,found;
    array_manager_t tmpman=create_manager(32768);
    uint32_t *tmplbl=NULL;
    ADD_ARRAY(tmpman,tmplbl,uint32_t);
    uint32_t *tmpdst=NULL;
    ADD_ARRAY(tmpman,tmpdst,uint32_t);
    lts_set_type(lts,LTS_BLOCK);
    count=0;
    for(i=0;i<lts->states;i++){
        uint32_t in_size=lts->begin[i+1]-lts->begin[i];
        if(in_size>0){
            // use merge sort on big blocks.
            uint32_t src_ofs=lts->begin[i];
            ensure_access(tmpman,(in_size+1)/2);
            uint32_t out_size=merge_sort_uniq_tmp(
                &lts->label[count],&lts->dest[count],
                tmplbl,tmpdst,
                &lts->label[src_ofs],&lts->dest[src_ofs],
                in_size
            );
            lts->begin[i]=count;
            count+=out_size;
        } else {
            lts->begin[i]=count;
        }
    }
    lts->begin[lts->states]=count;
    destroy_manager(tmpman);

    count=0;
    for(i=0;i<lts->root_count;i++){
        found=0;
        for(j=0;j<count;j++){
            if (lts->root_list[j]==lts->root_list[i]) {
                found=1;
                break;
            }
        }
        if (found) continue;
        lts->root_list[count]=lts->root_list[i];
        count++;
    }
    lts_set_size(lts,count,lts->states,lts->begin[lts->states]);
    if (!has_labels) {
        Debug("removing dummy labels");
        RTfree(lts->label);
        lts->label=NULL;
    }
}

