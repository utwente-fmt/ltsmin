// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <lts-lib/lts.h>
#include <lts-lib/lowmem.h>

static int has_labels;

static int samesig(lts_t lts,int*map,int s1,int s2){
    uint32_t t1,t2,m1,m2,t;

    if(map[s1]!=map[s2]) return 0;
    t1=lts->begin[s1];
    t2=lts->begin[s2];
    m1=lts->begin[s1+1];
    m2=lts->begin[s2+1];
    for(;;){
        if(t1==m1) return (t2==m2);
        if(t2==m2) return 0;
        if(has_labels && lts->label[t1]!=lts->label[t2]) return 0;
        if(map[lts->dest[t1]]!=map[lts->dest[t2]]) return 0;
        t=t1+1;
        while(t<m1 && (!has_labels  || lts->label[t1]==lts->label[t]) && map[lts->dest[t]]==map[lts->dest[t1]]){
            t++;
        }
        t1=t;
        t=t2+1;
        while(t<m2 && (!has_labels  || lts->label[t2]==lts->label[t]) && map[lts->dest[t]]==map[lts->dest[t2]]){
            t++;
        }
        t2=t;
    }
    return 0;
}

static int hashcode(lts_t lts,int *map,int i){
    int hc;
    size_t j;
    hc=map[i];
    for(j=lts->begin[i];j<lts->begin[i+1];j++){
        if(j>lts->begin[i] &&
            (!has_labels  || lts->label[j]==lts->label[j-1]) &&
            map[lts->dest[j]]==map[lts->dest[j-1]] ){
            continue;
        }
        if (has_labels) hc+=12531829*lts->label[j];
        hc+=87419861*map[lts->dest[j]];
    }
    return hc;
}

static int rehash(int h){
    return h*12531829 + 87419861;
}

void lowmem_strong_reduce(lts_t lts){
    has_labels=lts->label!=NULL;
    int has_props=lts->properties!=NULL;
    int *map,*newmap,*tmpmap;
    int *hash,mask,hc;
    int tmp, j;
    uint32_t i;
    int count,oldcount,iter;
    long long int chain_length;
    long long int hash_lookups;
    uint32_t m, k;

    lts_set_type(lts,LTS_BLOCK);
    map=(int*)RTmalloc(lts->states*sizeof(int));
    newmap=(int*)RTmalloc(lts->states*sizeof(int));
    if (has_props){
        for(i=0;i<lts->states;i++){
            map[i]=lts->properties[i];
        }
    } else {
        for(i=0;i<lts->states;i++){
            map[i]=0;
        }
    }
    for(i=1<<18;i<lts->states;i=i<<1){
    }
    i=(i>>8);
    hash=(int*)RTmalloc(i*sizeof(int));
    mask=i-1;
    oldcount=0;
    count=0;
    chain_length=0;
    hash_lookups=0;
    iter=0;
    for(;;){
        iter++;
        // sort transitions (bubble sort)
        for(i=0;i<lts->states;i++){
            for(m=lts->begin[i];m<lts->begin[i+1];m++){
                for(k=m;k>lts->begin[i];k--){
                    if (has_labels){
                        if(lts->label[k]>lts->label[k-1]) break;
                        if((lts->label[k]==lts->label[k-1])&&(map[lts->dest[k]]>=map[lts->dest[k-1]])) break;
                        tmp=lts->label[k];
                        lts->label[k]=lts->label[k-1];
                        lts->label[k-1]=tmp;
                    } else if (map[lts->dest[k]]>=map[lts->dest[k-1]]) break;
                    tmp=lts->dest[k];
                    lts->dest[k]=lts->dest[k-1];
                    lts->dest[k-1]=tmp;
                }
            }
        }
        // check if hash table is big enough.
        while((mask/5)<(count/3)){
            Print(infoLong,"Hash table resize prior to insertion!");
            RTfree(hash);
            mask=mask+mask+1;
            hash=(int*)RTmalloc((mask+1)*sizeof(int));
        }
        // clear hash table
        for(i=0;(int)i<=mask;i++){
            hash[i]=-1;
        }
        // insert states into hash table
        count=0;
        for(i=0;i<lts->states;i++){
            hash_lookups++;
            for(hc=hashcode(lts,map,i);;hc=rehash(hc)){
                chain_length++;
                j=hash[hc&mask];
                if(j==-1) break;
                if(samesig(lts,map,i,j)) break;
            }
            if (j==-1) {
                count++;
                hash[hc&mask]=i;
                newmap[i]=i;
                if((mask/4)<(count/3)){
                    Print(infoLong,"Hash table resize during insertion!");
                    RTfree(hash);
                    mask=mask+mask+1;
                    hash=(int*)RTmalloc((mask+1)*sizeof(int));
                    for(j=0;j<=mask;j++){
                        hash[j]=-1;
                    }
                    for(j=0;j<=(int)i;j++) if(newmap[j]==j){
                        hash_lookups++;
                        for(hc=hashcode(lts,map,j);hash[hc&mask]!=-1;hc=rehash(hc)){
                        }
                        hash[hc&mask]=j;
                    }
                }
            } else {
                newmap[i]=j;
            }
        }
        Print(infoLong,"count is %d",count);
        if (count==oldcount) break;
        oldcount=count;
        tmpmap=map;
        map=newmap;
        newmap=tmpmap;
    }
    Print(infoLong,"Average hash chain length: %2.3f",((float)chain_length)/((float)hash_lookups));
    count=0;
    for(i=0;i<lts->states;i++){
        if(map[i]==(int)i){
            newmap[i]=count;
            count++;
        } else {
            newmap[i]=newmap[map[i]];
        }
    }
    RTfree(map);
    lts_quotient(lts,(uint32_t*)newmap);
    RTfree(newmap);
}

void lts_quotient(lts_t lts,uint32_t *map){
    int has_props=lts->properties!=NULL;
    lts_set_type(lts,LTS_LIST);
    uint32_t *temp_props=NULL;
    if(has_props){
        temp_props=(uint32_t*)RTmalloc(lts->states*4);
    }
    uint32_t new_states=0;
    for(uint32_t i=0;i<lts->states;i++){
        if(has_props) temp_props[map[i]]=lts->properties[i];
        if(map[i]>=new_states) {
            new_states=map[i]+1;
        }
    }
    lts_set_size(lts,lts->root_count,new_states,lts->transitions);
    if (has_props){
        for(uint32_t i=0;i<lts->states;i++){
            lts->properties[i]=temp_props[i];
        }
        RTfree(temp_props);
    }
    for(uint32_t i=0;i<lts->root_count;i++){
        lts->root_list[i]=map[lts->root_list[i]];
    }
    for(uint32_t m=0;m<lts->transitions;m++){
        lts->src[m]=map[lts->src[m]];
        lts->dest[m]=map[lts->dest[m]];
    }
    lts_uniq(lts);
}

