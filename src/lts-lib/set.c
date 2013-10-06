
/*
 * $Log: set.c,v $
 * Revision 1.9  2003/05/20 15:12:55  sccblom
 * Added weak bisimulation and trace equivalence.
 *
 * Stefan.
 *
 * Revision 1.8  2002/12/05 15:27:43  sccblom
 * Fixed a number of bugs for branching bisimulation
 * Small improvements for single threaded tool.
 *
 * Revision 1.7  2002/07/19 09:01:16  sccblom
 * Improving set insertion order.
 *
 * Revision 1.6  2002/07/09 16:12:36  sccblom
 * Better timing of programs.
 * Some optimizations in bsim2mpi
 *
 * Revision 1.5  2002/06/26 08:00:25  sccblom
 * Added -Wall option to CFLAGS
 * Fixed a bug in set.c
 * Fixed a bug in mpi_core.c
 *
 * Revision 1.4  2002/05/15 12:21:59  sccblom
 * Added tex subdirectory and MPI prototype.
 *
 * Revision 1.3  2002/02/12 17:22:44  sccblom
 * next version
 *
 * Revision 1.2  2002/02/12 13:33:36  sccblom
 * First test version.
 *
 * Revision 1.1  2002/02/08 17:42:15  sccblom
 * Just saving.
 *
 */

#include <hre/config.h>

#include <stdlib.h>

#include <hre/runtime.h>
#include <lts-lib/set.h>

#define EMPTY_LIST -1


#define sethashcode(set,label,dest) (36425657*set+77673689*label+2341271*dest)

/*
static unsigned int sethashcode(int set,int label,int dest){
    unsigned register int a,b,c;
    a=set;
    b=label;
    c=dest;
  a -= b; a -= c; a ^= (c>>13);
  b -= c; b -= a; b ^= (a<<8); 
  c -= a; c -= b; c ^= (b>>13);
  a -= b; a -= c; a ^= (c>>12);
  b -= c; b -= a; b ^= (a<<16);
  c -= a; c -= b; c ^= (b>>5); 
  a -= b; a -= c; a ^= (c>>3);  
  b -= c; b -= a; b ^= (a<<10); 
  c -= a; c -= b; c ^= (b>>15); 
    return c;
}
*/

#define seteq(l1,d1,l2,d2) ((l1==l2)&&(d1==d2))
#define setleq(l1,d1,l2,d2) ((l1 < l2) || ((l1==l2)&&(d1<=d2)))
#define setless(l1,d1,l2,d2) ((l1 < l2) || ((l1==l2)&&(d1<d2)))


/** dynamic node array **/

struct setnode {
    int    label;
    int    dest;
    int    parent;
    int    tag;
} ;

static struct setnode *setnodes=NULL;
static int setnodesize=0;
static int setnodenext=1;
static int undefined_tag;

#define SET_NODE_BLOCK 15000


static void setchecknode(){
    if (setnodenext>=setnodesize) {
        //Warning(info,"setnoderealloc");
        setnodesize+=SET_NODE_BLOCK;
        setnodes=(struct setnode*)RTrealloc(setnodes,setnodesize*sizeof(struct setnode));
    }
}


/** dynamic hash table **/

struct setbucket {
    int    set;
    int    label;
    int    dest;
    int    bigset;
    int    next;
} ;

static struct setbucket *setbuckets=NULL;
static int setbucketsize=0;
static int setbucketnext=0;

#define SET_BUCKET_BLOCK 25000

static const int listhash[1]={EMPTY_LIST};
static int *sethash=(int*)listhash;
static int sethashmask=0;

#define SET_HASH_CLASS 18


static void setcheckbucket(){
    int i,hc;
    if (setbucketnext>=setbucketsize){
        //Warning(info,"setbucketrealloc");
        setbucketsize+=SET_BUCKET_BLOCK;
        setbuckets=(struct setbucket*)RTrealloc(setbuckets,setbucketsize*sizeof(struct setbucket));
        if ((sethashmask/4)<(setbucketsize/3)){
            //Warning(info,"setrehash");
            if (sethash==listhash) {
                sethashmask=(1<<SET_HASH_CLASS)-1;
                sethash=NULL;
            } else {
                sethashmask=sethashmask+sethashmask+1;
            }
            sethash=(int*)RTrealloc(sethash,(sethashmask+1)*sizeof(int));
            for(i=0;i<=sethashmask;i++){
                sethash[i]=EMPTY_LIST;
            }
            for(i=0;i<setbucketnext;i++){
                hc=sethashcode(setbuckets[i].set,setbuckets[i].label,setbuckets[i].dest) & sethashmask;
                setbuckets[i].next=sethash[hc];
                sethash[hc]=i;
            }
        }
    }
}

/** print **/

void SetPrint(FILE *f,int set){
    int i;
    if (set==EMPTY_SET) {
        fprintf(f,"{}");
    } else {
        fprintf(f,"{-%d->%d",setnodes[set].label,setnodes[set].dest);
        for(i=setnodes[set].parent;i!=EMPTY_SET;i=setnodes[i].parent){
            fprintf(f,",-%d->%d",setnodes[i].label,setnodes[i].dest);
        }
        fprintf(f,"}");
    }
}

void SetPrintIndex(FILE *f,int set,char **index){
    int i;
    if (set==EMPTY_SET) {
        fprintf(f,"{}");
    } else {
        fprintf(f,"{-%s->%d",index[setnodes[set].label],setnodes[set].dest);
        for(i=setnodes[set].parent;i!=EMPTY_SET;i=setnodes[i].parent){
            fprintf(f,",-%s->%d",index[setnodes[i].label],setnodes[i].dest);
        }
        fprintf(f,"}");
    }
}

/** reset **/

void SetFree(){
    Warning(info,"Freeing set structure with %d nodes and %d edges.",setnodenext,setbucketnext);
    RTfree(setnodes);
    setnodes=NULL;
    setnodesize=0;
    setnodenext=1;
    RTfree(setbuckets);
    setbuckets=NULL;
    setbucketsize=0;
    setbucketnext=0;
    RTfree(sethash);
    sethash=(int*)listhash;
    sethashmask=0;
}

/** clear the structure **/

void SetClear(int tag){
    int i;

    setchecknode();
    setcheckbucket();
    undefined_tag=tag;
    setnodes[0].tag=tag;
    //Warning(info,"Clearing set structure with %d nodes and %d edges.",setnodenext,setbucketnext);
    setnodenext=1;
    setbucketnext=0;
    for(i=0;i<=sethashmask;i++) sethash[i]=EMPTY_LIST;
}

/** insertion sub routines **/

static int SetBuild(int set,int label,int dest){
    int i,hc;
    //int depth;

    hc=sethashcode(set,label,dest) & sethashmask;
    //depth=1;
    for(i=sethash[hc];i!=EMPTY_LIST;i=setbuckets[i].next){
        if (setbuckets[i].set==set&&setbuckets[i].label==label&&setbuckets[i].dest==dest) return setbuckets[i].bigset;
        //depth++;
    }
    setchecknode();

    setnodes[setnodenext].label=label;
    setnodes[setnodenext].dest=dest;
    setnodes[setnodenext].parent=set;
    setnodes[setnodenext].tag=undefined_tag;

    setcheckbucket();
    hc=sethashcode(set,label,dest) & sethashmask;

    //if(depth==3) Warning(1,"bucket %d exceeding depth %d",hc,depth);

    setbuckets[setbucketnext].set=set;
    setbuckets[setbucketnext].label=label;
    setbuckets[setbucketnext].dest=dest;
    setbuckets[setbucketnext].bigset=setnodenext;
    setbuckets[setbucketnext].next=sethash[hc];
    sethash[hc]=setbucketnext;
    setbucketnext++;

    return setnodenext++;
}

static int SetFind(int set,int label,int dest){
    if (set!=EMPTY_SET) {
        if (setless(label,dest,setnodes[set].label,setnodes[set].dest)) {
            return SetBuild(SetFind(setnodes[set].parent,label,dest),setnodes[set].label,setnodes[set].dest);
        }
        if (seteq(label,dest,setnodes[set].label,setnodes[set].dest)) {
            return set;
        }
    }
    return SetBuild(set,label,dest);
}

/** insert **/

int SetInsert(int set,int label,int dest){
    int i,hc,s;

    hc=sethashcode(set,label,dest) & sethashmask;
    for(i=sethash[hc];i!=EMPTY_LIST;i=setbuckets[i].next){
        if (setbuckets[i].set==set&&setbuckets[i].label==label&&setbuckets[i].dest==dest) return setbuckets[i].bigset;
    }
    s=SetFind(set,label,dest);
    if (setnodes[s].parent!=set) {
        setcheckbucket();
        hc=sethashcode(set,label,dest) & sethashmask;
        setbuckets[setbucketnext].set=set;
        setbuckets[setbucketnext].label=label;
        setbuckets[setbucketnext].dest=dest;
        setbuckets[setbucketnext].bigset=s;
        setbuckets[setbucketnext].next=sethash[hc];
        sethash[hc]=setbucketnext;
        setbucketnext++;
    }
    return s;
}


/** union **/

int SetUnion(int set1,int set2){
    if (set2==EMPTY_SET) {
        return set1;
    } else {
        return SetInsert(SetUnion(set1,setnodes[set2].parent),setnodes[set2].label,setnodes[set2].dest);
    }
}


int SetGetTag(int set){
    return setnodes[set].tag;
}


void SetSetTag(int set,int tag){
    setnodes[set].tag=tag;
}

int SetGetSize(int set){
    int size=0;
    while(set!=EMPTY_SET){
        set=setnodes[set].parent;
        size++;
    }
    return size;
}

void SetGetSet(int set,int*data){
    int i;
    i=SetGetSize(set)-1;
    while(i>=0){
        data[2*i]=setnodes[set].label;
        data[2*i+1]=setnodes[set].dest;
        i--;
        set=setnodes[set].parent;
    }
}

int SetGetLabel(int set){
    return setnodes[set].label;
}

int SetGetDest(int set){
    return setnodes[set].dest;
}

int SetGetParent(int set){
    return setnodes[set].parent;
}

unsigned int SetGetHash(int set){
    unsigned int hash=0;
    while(set!=EMPTY_SET){
        hash=31*hash+sethashcode(0,setnodes[set].label,setnodes[set].dest);
        set=setnodes[set].parent;
    }
    return hash;
}



