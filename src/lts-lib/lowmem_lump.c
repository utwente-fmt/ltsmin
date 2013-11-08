// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>


#include <lts-lib/lts.h>
#include <lts-lib/lowmem.h>
#include <util-lib/rationals.h>
#include <util-lib/fast_hash.h>

static int rehash(int h){
    return SuperFastHash(&h,4,h);
}

static int lump_hashcode(lts_t lts,int *map,int i){
    long j;
    uint32_t temp[2];
    uint32_t contrib[3];
    int last=lts->begin[i+1]-1;
    int hc=map[i];
    //int sigptr=0, *sig=alloca(2*(last-lts->begin[i]));
    //Warning(1,"signature for %d %x %x",i,sig,map);
    for(j=lts->begin[i];j<=last;j++){
        int block=map[lts->dest[j]];
        contrib[2]=block;
        TreeUnfold(lts->edge_idx,lts->label[j],(int*)contrib);
        //Warning(1,"collecting");
        while(j<last && block==map[lts->dest[j+1]]){
            j++;
            TreeUnfold(lts->edge_idx,lts->label[j],(int*)temp);
            uint64_t teller=((uint64_t)contrib[0])*((uint64_t)temp[1]);
                    teller+=((uint64_t)temp[0])*((uint64_t)contrib[1]);
            uint64_t noemer=((uint64_t)contrib[1])*((uint64_t)temp[1]);
            uint64_t gcd=gcd64(teller,noemer);
            contrib[0]=teller/gcd;
            contrib[1]=noemer/gcd;
        }
        hc=SuperFastHash(contrib,12,hc);
    }
    return hc;
}

static int same_lump_sig(lts_t lts,uint32_t* map,uint32_t s1,uint32_t s2){
    uint32_t t1,t2,m1,m2,t,block,rate1[2],rate2[2],temp[2];
    if (map[s1]!=map[s2]) {
        //Warning(1,"source maps different");
        return 0;
    }

    t1=lts->begin[s1];
    t2=lts->begin[s2];
    m1=lts->begin[s1+1];
    m2=lts->begin[s2+1];
    for(;;){
        if(t1==m1 && t2==m2) return 1;
        if(t1==m1 || t2==m2) {
            //Warning(1,"different lengths");
            return 0;
        }
        block=map[lts->dest[t1]];
        if(block!=map[lts->dest[t2]]) return 0;
        t=t1+1;
        TreeUnfold(lts->edge_idx,lts->label[t1],(int*)rate1);
        while(t<m1 && map[lts->dest[t]]==block){
            TreeUnfold(lts->edge_idx,lts->label[t],(int*)temp);
            uint64_t teller=((uint64_t)rate1[0])*((uint64_t)temp[1]);
                    teller+=((uint64_t)temp[0])*((uint64_t)rate1[1]);
            uint64_t noemer=((uint64_t)rate1[1])*((uint64_t)temp[1]);
            uint64_t gcd=gcd64(teller,noemer);
            rate1[0]=teller/gcd;
            rate1[1]=noemer/gcd;
            t++;
        }
        t1=t;
        t=t2+1;
        TreeUnfold(lts->edge_idx,lts->label[t2],(int*)rate2);
        while(t<m2 && map[lts->dest[t]]==block){
            TreeUnfold(lts->edge_idx,lts->label[t],(int*)temp);
            uint64_t teller=((uint64_t)rate2[0])*((uint64_t)temp[1]);
                    teller+=((uint64_t)temp[0])*((uint64_t)rate2[1]);
            uint64_t noemer=((uint64_t)rate2[1])*((uint64_t)temp[1]);
            uint64_t gcd=gcd64(teller,noemer);
            rate2[0]=teller/gcd;
            rate2[1]=noemer/gcd;
            t++;
        }
        t2=t;
        if (rate1[0]!=rate2[0] || rate1[1]!= rate2[1]) return 0;
    }
    return 0;
}

static void lts_lump(lts_t lts){
    int found;
    uint32_t  i, k, j, count, oldbegin;
    lts_set_type(lts,LTS_BLOCK);
    count=0;
    uint32_t rate[2];
    uint32_t temp[2];
    for(i=0;i<lts->states;i++){
        oldbegin=lts->begin[i];
        lts->begin[i]=count;
        for(j=oldbegin;j<lts->begin[i+1];j++){
            found=0;
            for(k=lts->begin[i];k<count;k++){
                if(lts->dest[j]==lts->dest[k]){
                    TreeUnfold(lts->edge_idx,lts->label[k],(int*)rate);
                    TreeUnfold(lts->edge_idx,lts->label[j],(int*)temp);
                    uint64_t teller=((uint64_t)rate[0])*((uint64_t)temp[1]);
                            teller+=((uint64_t)temp[0])*((uint64_t)rate[1]);
                    uint64_t noemer=((uint64_t)rate[1])*((uint64_t)temp[1]);
                    uint64_t gcd=gcd64(teller,noemer);
                    rate[0]=teller/gcd;
                    rate[1]=noemer/gcd;
                    lts->label[k]=TreeFold(lts->edge_idx,(int*)rate);
                    found=1;
                    break;
                }
            }
            if (!found){
                lts->label[count]=lts->label[j];
                lts->dest[count]=lts->dest[j];
                count++;
            }
        }
    }
    lts->begin[lts->states]=count;
    lts_set_size(lts,lts->root_count,lts->states,count);
}

void lowmem_lumping_reduce(lts_t lts){
    int *map,*newmap,*tmpmap;
    int *hash,mask,hc;
    int tmp, j;
    uint32_t i;
    uint32_t count=0,oldcount;
    int iter;
    long long int chain_length;
    long long int hash_lookups;
    uint32_t m;
    //mytimer_t timer=SCCcreateTimer();
    lts_set_type(lts,LTS_BLOCK);
    map=(int*)RTmalloc(lts->states*sizeof(int));
    newmap=(int*)RTmalloc(lts->states*sizeof(int));
    for(i=0;i<lts->states;i++){
        map[i]=lts->properties[i];
    }
    for(i=1<<14;i<lts->states;i=i<<1){
    }
    i=(i>>4);
    hash=(int*)RTmalloc(i*sizeof(int));
    mask=i-1;
    oldcount=TreeCount(lts->prop_idx);
    iter=0;
    for(;;){
        //SCCresetTimer(timer);
        //SCCstartTimer(timer);
        chain_length=0;
        hash_lookups=0;
        iter++;
        // sort transitions (bubble sort)
        for(i=0;i<lts->states;i++){
            for(m=lts->begin[i];m<lts->begin[i+1];m++){
                uint32_t k;
                for(k=m;k>lts->begin[i];k--){
                    if(map[lts->dest[k]]>map[lts->dest[k-1]]) break;
                    tmp=lts->label[k];
                    lts->label[k]=lts->label[k-1];
                    lts->label[k-1]=tmp;
                    tmp=lts->dest[k];
                    lts->dest[k]=lts->dest[k-1];
                    lts->dest[k-1]=tmp;
                }
            }
        }
        //SCCstopTimer(timer);
        //SCCreportTimer(timer,"sorting finished after");
        //SCCstartTimer(timer);
        // check if hash table is big enough.
        while((mask/5)<((int)count/3)){
            Print(infoShort,"Hash table resize prior to insertion!");
            RTfree(hash);
            mask=mask+mask+1;
            hash=(int*)RTmalloc((mask+1)*sizeof(int));
        }
        // clear hash table
        for(i=0;i<=(unsigned int)mask;i++){
            hash[i]=-1;
        }
        // insert states into hash table
        count=0;
        for(i=0;i<lts->states;i++){
            hash_lookups++;
            for(hc=lump_hashcode(lts,map,i);;hc=rehash(hc)){
                chain_length++;
                j=hash[hc&mask];
                if(j==-1) break;
                if(same_lump_sig(lts,(uint32_t*)map,i,j)) break;
            }
            if (j==-1) {
                count++;
                hash[hc&mask]=i;
                newmap[i]=i;
                if((mask/4)<((int)count/3)){
                    Print(infoLong,"Hash table resize during insertion!");
                    RTfree(hash);
                    mask=mask+mask+1;
                    hash=(int*)RTmalloc((mask+1)*sizeof(int));
                    for(j=0;j<=mask;j++){
                        hash[j]=-1;
                    }
                    for(j=0;j<=(int)i;j++) if(newmap[j]==j){
                        hash_lookups++;
                        for(hc=lump_hashcode(lts,map,j);hash[hc&mask]!=-1;hc=rehash(hc)){
                            chain_length++;
                        }
                        hash[hc&mask]=j;
                    }
                }
            } else {
                newmap[i]=j;
            }
        }
        //SCCstopTimer(timer);
        //SCCreportTimer(timer,"iteration took");
        Print(infoShort,"Average hash chain length: %2.3f",((float)chain_length)/((float)hash_lookups));
        Print(infoShort,"block count %d => %d",oldcount,count);
        if (count==oldcount) break;
        oldcount=count;
        tmpmap=map;
        map=newmap;
        newmap=tmpmap;
    }
    Print(infoShort,"Reduction took %d iterations",iter);
    if (count < lts->states){
        lts_set_type(lts,LTS_LIST);
        count=0;
        for(i=0;i<lts->states;i++){
            if(map[i]==(int)i){
                newmap[i]=count;
                hash[count]=lts->properties[i];
                count++;
            } else {
                newmap[i]=lts->states-1;
            }
        }
        RTfree(lts->properties);
        lts->properties=(uint32_t*)hash;
        hash=NULL;
        for(i=0;i<lts->root_count;i++){
            lts->root_list[i]=newmap[map[lts->root_list[i]]];
        }
        for(m=0;m<lts->transitions;m++){
            lts->src[m]=newmap[lts->src[m]];
            lts->dest[m]=newmap[map[lts->dest[m]]];
        }
        RTfree(map);
        RTfree(newmap);

        lts_set_type(lts,LTS_BLOCK);
        lts_set_size(lts,lts->root_count,oldcount,lts->begin[oldcount]);

        lts_lump(lts);
    } else {
        RTfree(hash);
        RTfree(map);
        RTfree(newmap);
    }
}
