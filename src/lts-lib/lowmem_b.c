// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <lts-lib/lts.h>
#include <lts-lib/lowmem.h>

/*
static void printsig(lts_t lts,int tau,int *oldmap,int*newmap,int i){
    int j;
    for(j=lts->begin[i];j<lts->begin[i+1];j++){
        if (lts->label[j]==tau){
            Warning(1,"%s i %d",lts->label_string[lts->label[j]],newmap[lts->dest[j]]);
        } else {
            Warning(1,"%s a %d",lts->label_string[lts->label[j]],oldmap[lts->dest[j]]);
        }
    }
}
*/

// check is the sig of s1 is a subset of the sig of s2 union (tau,s2)...
static int check_silent(lts_t lts,uint32_t tau,int*oldmap,int*newmap,int s1,int s2){
    int t1,t2,m1,m2,t;

    t1=lts->begin[s1];
    t2=lts->begin[s2];
    m1=lts->begin[s1+1];
    m2=lts->begin[s2+1];

    for(;;){
        while(t1<m1){
            if (lts->label[t1]==tau && newmap[lts->dest[t1]]==newmap[s2]) {
                //Warning(1,"skipping tau %d",newmap[lts->dest[t1]]);
                t1++;
            } else {
                break;
            }
        }
        if(t1==m1) return 1;
        //if (lts->label[t1]==tau){
        //    Warning(1,"trying to match tau %d",newmap[lts->dest[t1]]);
        //} else {
        //    Warning(1,"trying to match %s %d",lts->label_string[lts->label[t1]],oldmap[lts->dest[t1]]);
        //}
        while(t2<m2){
            if (lts->label[t1]!=lts->label[t2]) {
                //Warning(1,"discarding %s",lts->label_string[lts->label[t2]]);
                t2++;
                continue;
            }
            if (lts->label[t1]==tau) {
                if(newmap[lts->dest[t1]]==newmap[lts->dest[t2]]) {
                    break;
                } //else {
                //    Warning(1,"discarding tau %d",newmap[lts->dest[t2]]);
                //}
            } else {
                if(oldmap[lts->dest[t1]]==oldmap[lts->dest[t2]]) {
                    break;
                }// else {
                //    Warning(1,"discarding %s %d",lts->label_string[lts->label[t2]],oldmap[lts->dest[t2]]);
                //}
            }
            t2++;
        }
        if(t2==m2) return 0;
        if(lts->label[t1]==tau){
            if(newmap[lts->dest[t1]]!=newmap[lts->dest[t2]]) return 0;
            t=t1+1;
            while(t<m1 && lts->label[t1]==lts->label[t] && newmap[lts->dest[t]]==newmap[lts->dest[t1]]){
                t++;
            }
            t1=t;
            t=t2+1;
            while(t<m2 && lts->label[t2]==lts->label[t] && newmap[lts->dest[t]]==newmap[lts->dest[t2]]){
                t++;
            }
            t2=t;
        } else {
            if(oldmap[lts->dest[t1]]!=oldmap[lts->dest[t2]]) return 0;
            t=t1+1;
            while(t<m1 && lts->label[t1]==lts->label[t] && oldmap[lts->dest[t]]==oldmap[lts->dest[t1]]){
                t++;
            }
            t1=t;
            t=t2+1;
            while(t<m2 && lts->label[t2]==lts->label[t] && oldmap[lts->dest[t]]==oldmap[lts->dest[t2]]){
                t++;
            }
            t2=t;
        }
    }
    return 0;
}
static int samesig(lts_t lts,uint32_t tau,int*oldmap,int*newmap,int s1,int s2){
    int t1,t2,m1,m2,t;

    t1=lts->begin[s1];
    t2=lts->begin[s2];
    m1=lts->begin[s1+1];
    m2=lts->begin[s2+1];
    if (oldmap[s1]!=oldmap[s2]) return 0;
    for(;;){
        if(t1==m1) return (t2==m2);
        if(t2==m2) return 0;
        if(lts->label[t1]!=lts->label[t2]) return 0;
        if(lts->label[t1]==tau){
            if(newmap[lts->dest[t1]]!=newmap[lts->dest[t2]]) return 0;
            t=t1+1;
            while(t<m1 && lts->label[t1]==lts->label[t] && newmap[lts->dest[t]]==newmap[lts->dest[t1]]){
                t++;
            }
            t1=t;
            t=t2+1;
            while(t<m2 && lts->label[t2]==lts->label[t] && newmap[lts->dest[t]]==newmap[lts->dest[t2]]){
                t++;
            }
            t2=t;
        } else {
            if(oldmap[lts->dest[t1]]!=oldmap[lts->dest[t2]]) return 0;
            t=t1+1;
            while(t<m1 && lts->label[t1]==lts->label[t] && oldmap[lts->dest[t]]==oldmap[lts->dest[t1]]){
                t++;
            }
            t1=t;
            t=t2+1;
            while(t<m2 && lts->label[t2]==lts->label[t] && oldmap[lts->dest[t]]==oldmap[lts->dest[t2]]){
                t++;
            }
            t2=t;
        }
    }
    return 0;
}

static int hashcode(lts_t lts,uint32_t tau,int *oldmap,int *newmap,int i){
    int hc;
    hc=0;
    for(uint32_t j=lts->begin[i];j<lts->begin[i+1];j++){
        if(lts->label[j]==tau){
            if(j>lts->begin[i] &&
                lts->label[j]==lts->label[j-1] &&
                newmap[lts->dest[j]]==newmap[lts->dest[j-1]] ){
                continue;
            }
            hc=hc+12531829*lts->label[j]+87419861*newmap[lts->dest[j]];
        } else {
            if(j>lts->begin[i] &&
                lts->label[j]==lts->label[j-1] &&
                oldmap[lts->dest[j]]==oldmap[lts->dest[j-1]] ){
                continue;
            }
            hc=hc+12531829*lts->label[j]+87419861*oldmap[lts->dest[j]];
        }
    }
    return hc;
}

static int rehash(int h){
    return h*12531829 + 87419861;
}

#define DIV 0x7fffffff

void lowmem_branching_reduce(lts_t lts,bitset_t diverging){
    int *oldmap,*newmap,*tmpmap;
    int *hash,mask,hc;
    int i,j,k,tmp;
    int count,oldcount,iter;
    long long int chain_length;
    long long int hash_lookups;
    uint32_t tau;
    
    if (lts->label==NULL) {
        Abort("Cannot apply branching bisimulation to an LTS without edge labels.");
    }
    if (lts->edge_idx!=NULL){
        Abort("Cannot apply branching bisimulation to an LTS with more than one edge label.");
    }
    if (lts->properties!=NULL || lts->state_db!=NULL){
        Abort("Cannot apply branching bisimulation to an LTS with state labels.");
    }

    tau=(uint32_t)lts->tau;
    if (lts->tau<0) {
        Print(infoShort,"No silent steps. Using strong bisimulation algorithm");
        lowmem_strong_reduce(lts);
        return;
    }
    lts_silent_cycle_elim(lts,tau_step,diverging,diverging);
    Print(infoShort,"size after tau cycle elimination is %d states and %d transitions",lts->states,lts->transitions);
    lts_set_type(lts,LTS_BLOCK);
    if (diverging!=NULL){
        for(i=0;i<(int)lts->states;i++){
            for(j=lts->begin[i];j<(int)lts->begin[i+1];j++){
                if (i==(int)lts->dest[j] && lts->label[j]==tau) {
                    lts->label[j]=DIV;
                }
            }
        }
    }
    oldmap=(int*)RTmalloc(lts->states*sizeof(int));
    newmap=(int*)RTmalloc(lts->states*sizeof(int));
    for(i=0;i<(int)lts->states;i++){
        oldmap[i]=lts->states;
    }
    for(i=1<<18;i<(int)lts->states;i=i<<1){
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
        // check if hash table is big enough.
        //Warning(1,"Checking if hash table is big enough.");
        while((mask/5)<(count/3)){
            Print(infoLong,"Hash table resize prior to insertion!");
            RTfree(hash);
            mask=mask+mask+1;
            hash=(int*)RTmalloc((mask+1)*sizeof(int));
        }
        // clear hash table
        for(i=0;i<=mask;i++){
            hash[i]=-1;
        }
        // insert states into hash table
        count=0;
        for(i=lts->states-1;i>=0;i--){
            // sort transitions (bubble sort)
            //Warning(1,"state %d",i);
            for(j=lts->begin[i];j<(int)lts->begin[i+1];j++){
                for(k=j;k>(int)lts->begin[i];k--){
                    if(lts->label[k]>lts->label[k-1]) break;
                    if(lts->label[k]==tau){
                        if((lts->label[k]==lts->label[k-1])&&
                            (newmap[lts->dest[k]]>=newmap[lts->dest[k-1]])) break;
                    } else {
                        if((lts->label[k]==lts->label[k-1])&&
                            (oldmap[lts->dest[k]]>=oldmap[lts->dest[k-1]])) break;
                    }
                    tmp=lts->label[k];
                    lts->label[k]=lts->label[k-1];
                    lts->label[k-1]=tmp;
                    tmp=lts->dest[k];
                    lts->dest[k]=lts->dest[k-1];
                    lts->dest[k-1]=tmp;
                }
            }
            // try to find silent tau step.
            //Warning(1,"checking for silent step");
            for(j=lts->begin[i];j<(int)lts->begin[i+1];j++){
                if (lts->label[j]==tau && oldmap[i]==oldmap[lts->dest[j]]){
                    //Warning(1,"considering %d -> %d",i,lts->dest[j]);
                    //Warning(1,"sig of %d is:",i);
                    //printsig(lts,tau,oldmap,newmap,i);
                    //Warning(1,"sig of %d is:",newmap[lts->dest[j]]);
                    //printsig(lts,tau,oldmap,newmap,newmap[lts->dest[j]]);
                    if(check_silent(lts,tau,oldmap,newmap,i,newmap[lts->dest[j]])){
                        //Warning(1,"silent\n");
                        //Warning(1,"assigning %d",newmap[lts->dest[j]]);
                        newmap[i]=newmap[lts->dest[j]];
                        j=-1;
                        break;
                    }
                    //Warning(1,"visible\n");
                }
            }
            if(j==-1){
                //Warning(1,"found",i);
                continue;
            }
            //Warning(1,"not found",i);
            // we have a terminal state.
            hash_lookups++;
            for(hc=hashcode(lts,tau,oldmap,newmap,i);;hc=rehash(hc)){
                chain_length++;
                j=hash[hc&mask];
                if(j==-1) break;
                if(samesig(lts,tau,oldmap,newmap,i,j)) break;
            }
            if (j==-1) {
                count++;
                hash[hc&mask]=i;
                newmap[i]=i;
                //Warning(1,"new assigning %d",i);
                if((mask/4)<(count/3)){
                    Print(infoLong,"Hash table resize during insertion!");
                    RTfree(hash);
                    mask=mask+mask+1;
                    hash=(int*)RTmalloc((mask+1)*sizeof(int));
                    for(j=0;j<=mask;j++){
                        hash[j]=-1;
                    }
                    for(j=lts->states-1;j>=i;j--) if(newmap[j]==j){
                        hash_lookups++;
                        for(hc=hashcode(lts,tau,oldmap,newmap,j);hash[hc&mask]!=-1;hc=rehash(hc)){
                        }
                        hash[hc&mask]=j;
                    }
                }
            } else {
                //Warning(1,"existing assigning %d",j);
                newmap[i]=j;
            }
        }
        Print(infoLong,"count is %d",count);
        if (count==oldcount) break;
        oldcount=count;
        tmpmap=newmap;
        newmap=oldmap;
        oldmap=tmpmap;
    }
    Print(infoLong,"Average hash chain length: %2.3f",((float)chain_length)/((float)hash_lookups));

    count=0;
    for(i=lts->states-1;i>=0;i--) {
        if (oldmap[i]==i) {
            newmap[i]=count;
            count++;
        } else {
            newmap[i]=newmap[oldmap[i]];
        }
    }
    lts_set_type(lts,LTS_LIST);
    for(i=0;i<(int)lts->root_count;i++){
        lts->root_list[i]=newmap[lts->root_list[i]];
    }
    lts->states=count;
    count=0;
    for(i=0;i<(int)lts->transitions;i++){
        uint32_t s=newmap[lts->src[i]];
        uint32_t l=lts->label[i];
        uint32_t d=newmap[lts->dest[i]];
        if ((l==tau)&&(s==d)) continue;
        lts->src[count]=s;
        lts->label[count]=(l==DIV)?tau:l;
        lts->dest[count]=d;
        count++;
    }
    lts_set_size(lts,lts->root_count,lts->states,count);
    lts_uniq(lts);
    RTfree(hash);
    RTfree(oldmap);
    RTfree(newmap);
    Print(infoShort,"reduction took %d iterations",iter);
}


