#include <hre/config.h>

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#ifdef __APPLE__
#include <alloca.h>
#endif

#include <hre/user.h>
#include <vset-lib/vdom_object.h>
#include <vset-lib/vector_set.h>
#include <hre-io/user.h>
#include <util-lib/simplemap.h>

static uint32_t mdd_nodes;
static uint32_t uniq_size;
static uint32_t cache_size;

/** fibonacci number of the size of the node table. */
static uint32_t nodes_fib=30;
/** Maximum fibonacci number before overflow. */
static const uint32_t FIB_MAX = 46;
/** difference between the fibonacci numbers of the sizes of the node table and the cache. */
static int cache_fib=1;

static uint32_t fib(uint32_t n){
    uint32_t tmp1=0;
    uint32_t tmp2=1;
    while(n>0){
        uint32_t tmp=tmp1;
        tmp1=tmp2;
        tmp2+=tmp;
        n--;
    }
    return tmp1;
}

struct poptOption listdd_options[]= {
    { "ldd32-step", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &nodes_fib, 0, "set intial step in node size", "<step>"},
    { "ldd32-cache",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cache_fib , 0 , "set Fibonacci difference between operation op cache and nodes (may be negative)","<diff>"},
    POPT_TABLEEND
};

static uint32_t free_node=1;
static uint32_t* unique_table=NULL;
struct mdd_node {
    uint32_t next;
    uint32_t val;
    uint32_t down;
    uint32_t right;
};

static struct mdd_node *node_table=NULL;
struct op_rec {
    uint32_t op;
    uint32_t arg1;
    union {
        double count;
        struct {
            uint32_t arg2;
            uint32_t res;
        } other;
        uint64_t count_part;
        void* any;
    } res;
};
static struct op_rec *op_cache=NULL;
#define OP_UNUSED 0
#define OP_COUNT 1
#define OP_UNION 2
#define OP_MINUS 3
#define OP_PROJECT 4
#define OP_NEXT 5
#define OP_PREV 6
#define OP_COPY_MATCH 7
#define OP_INTERSECT 8
#define OP_SAT 9
#define OP_RELPROD 10
#define OP_UNIVERSE 11
#define OP_CCOUNT1 12
#define OP_CCOUNT2 13
#define OP_VISIT 14
#define MAX_OP 14

static int next_cache_op = MAX_OP;

struct vector_domain {
    struct vector_domain_shared shared;
    // single global structure for now.
};

struct vector_set {
    vdom_t dom;
    vset_t next; // double linked list of protected mdd's;
    vset_t prev; //
    uint32_t mdd;
    uint32_t p_id;
    int p_len;
    int proj[];
};

struct vector_relation {
    vdom_t dom;
    expand_cb expand;
    void *expand_ctx;
    vrel_t next; // double linked list of protected mdd's;
    vrel_t prev; //
    uint32_t mdd;
    uint32_t p_id;
    int p_len;
    int proj[];
};

static inline uint32_t hash(uint32_t a,uint32_t b,uint32_t c){
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

static vset_t protected_sets=NULL;
static vrel_t protected_rels=NULL;

static uint32_t* mdd_load_node_ids=NULL;
static uint32_t mdd_load_node_count=0;

static uint32_t mdd_used;

/** fibonacci number of the size of the stack. */
static uint32_t stack_fib=22;

static uint32_t stack_size;
static uint32_t *mdd_stack = NULL;
static uint32_t mdd_top=0;

static void
mdd_create_stack()
{
    stack_size = fib(stack_fib);
    mdd_stack = RTmalloc(sizeof(uint32_t[stack_size]));
    Warning(info, "initial stack size %" PRIu32, stack_size);
}

static void
mdd_push(uint32_t mdd)
{
    if (mdd_top==stack_size) {
        uint32_t stack_size_new = fib(++stack_fib);

        if (stack_size_new < stack_size)
            Abort("stack overflow");

        mdd_stack = RTrealloc(mdd_stack, sizeof(uint32_t[stack_size_new]));
        stack_size = stack_size_new;
        Warning(debug, "new stack size %" PRIu32, stack_size);
    }
    mdd_stack[mdd_top]=mdd;
    mdd_top++;
}

static uint32_t
mdd_pop()
{
    if (mdd_top==0) Abort("stack underflow");
    mdd_top--;
    return mdd_stack[mdd_top];
}

static void mdd_mark(uint32_t mdd){
    if (mdd<=1) return;
    if (node_table[mdd].next&0x80000000) return;
    mdd_used++;
    node_table[mdd].next=node_table[mdd].next|0x80000000;
    mdd_mark(node_table[mdd].down);
    mdd_mark(node_table[mdd].right);
}

static void mdd_clear_and_count(uint32_t mdd,uint32_t *count){
    if (mdd<=1) return;
    if (node_table[mdd].next&0x80000000) {
        node_table[mdd].next=node_table[mdd].next&0x7fffffff;
        (*count)++;
        mdd_clear_and_count(node_table[mdd].down,count);
        mdd_clear_and_count(node_table[mdd].right,count);
    }
}

static uint32_t
mdd_node_count(uint32_t mdd)
{
    if (mdd <= 1) return 1; // emptyset or singleton

    uint32_t res = 0;
    mdd_mark(mdd);
    mdd_clear_and_count(mdd, &res);
    return res+2; // real nodes plus emptyset(0) and singleton epsilon(1).
}

static uint32_t mdd_sweep_bucket(uint32_t mdd){
    if (mdd==0) return 0;
    if (mdd==1) Abort("data corruption");
    if (node_table[mdd].next&0x80000000){
        node_table[mdd].next=node_table[mdd].next&0x7fffffff;
        node_table[mdd].next=mdd_sweep_bucket(node_table[mdd].next);
        return mdd;
    } else {
        uint32_t tmp=node_table[mdd].next;
        node_table[mdd].next=free_node;
        free_node=mdd;
        return mdd_sweep_bucket(tmp);
    }
}

static void mdd_collect(uint32_t a,uint32_t b){
    mdd_used=0;
    mdd_mark(a);
    mdd_mark(b);
    vset_t set=protected_sets;
    while(set!=NULL){
        mdd_mark(set->mdd);
        mdd_mark(set->p_id);
        set=set->next;
    }
    vrel_t rel=protected_rels;
    while(rel!=NULL){
        mdd_mark(rel->mdd);
        mdd_mark(rel->p_id);
        rel=rel->next;
    }
    for(uint32_t i=0;i<mdd_top;i++){
        mdd_mark(mdd_stack[i]);
    }
    for(uint32_t i=0;i<mdd_load_node_count;i++){
        mdd_mark(mdd_load_node_ids[i]);
    }
    /* There used to be code marking results of projection and
       next, to allow them to remain in the cache. On the
       few tests done, there did not seem to be a speedup
       but the memory use went up considerably.
       Still it may be useful for saturation.
    */
    Warning(debug, "ListDD garbage collection: %" PRIu32" of %" PRIu32" nodes used",
            mdd_used, mdd_nodes);
    int resize=0;
    // The two assignments below are not needed, but silence compiler warnings
    uint32_t new_cache_size = cache_size;
    struct op_rec *new_cache = op_cache;
    uint32_t copy_count=0;
    if (mdd_used == UINT32_MAX)
        Abort("Node table full at maximum size");
    if (mdd_used > fib(nodes_fib-1) && mdd_nodes != UINT32_MAX) {
        Warning(debug,"insufficient free nodes, resizing");
        resize=1;
        nodes_fib++;
        new_cache_size=fib(nodes_fib+cache_fib);
        new_cache=RTalign(CACHE_LINE_SIZE,new_cache_size*sizeof(struct op_rec));
        for(uint32_t i=0;i<new_cache_size;i++){
            new_cache[i].op=0;
        }
        if (new_cache_size < cache_size) {
            Warning(debug, "op cache reached maximum size");
            new_cache_size = UINT32_MAX;
        }
        Warning(debug, "new op cache has %" PRIu32 " entries", new_cache_size);
    }
    for(uint32_t i=0;i<cache_size;i++){
        uint32_t slot,op,arg1,arg2,res;
        op=op_cache[i].op&0x00ff;
        switch(op){
            case OP_UNUSED: continue;
            case OP_COUNT:
            case OP_CCOUNT1:
            case OP_CCOUNT2:
            case OP_VISIT:
            {
                arg1=op_cache[i].arg1;
                arg2=0;
                if (!(node_table[arg1].next&0x80000000)){
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) break;
                else continue;
            }
            case OP_PROJECT:
            case OP_UNIVERSE:
            {
                arg1=op_cache[i].arg1;
                if (!(node_table[arg1].next&0x80000000)) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                arg2=op_cache[i].res.other.arg2;
                res=op_cache[i].res.other.res;
                if (!(node_table[res].next&0x80000000)) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) break;
                else continue;
            }
            case OP_UNION:
            case OP_MINUS:
            case OP_NEXT:
            case OP_PREV:
            case OP_COPY_MATCH:
            case OP_INTERSECT:
            case OP_SAT:
            case OP_RELPROD:
            {
                arg1=op_cache[i].arg1;
                if (!(node_table[arg1].next&0x80000000)) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                arg2=op_cache[i].res.other.arg2;
                if (!(node_table[arg2].next&0x80000000)) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                res=op_cache[i].res.other.res;
                if (!(node_table[res].next&0x80000000)) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) break;
                else continue;
            }
            default: Abort("missing case");
        }
        slot=hash(op,arg1,arg2)%new_cache_size;
        copy_count++;
        new_cache[slot]=op_cache[i];
    }
    if (!resize){
        for(uint32_t i=0;i<uniq_size;i++){
            unique_table[i]=mdd_sweep_bucket(unique_table[i]);
        }
    } else {
        Warning(debug,"copied %" PRIu32" op cache nodes",copy_count);
        RTfree(op_cache);
        op_cache=new_cache;
        cache_size=new_cache_size;
        uint32_t old_size=mdd_nodes;
        mdd_nodes=fib(nodes_fib);

        if (uniq_size != UINT32_MAX) {
            uniq_size = fib(nodes_fib + 1);
            if (uniq_size < mdd_nodes) {
                Warning(debug, "unique table reached maximum size");
                uniq_size = UINT32_MAX;
            }
            unique_table = RTrealloc(unique_table, uniq_size*sizeof(uint32_t));
        } else if (mdd_nodes < old_size) {
            Warning(debug, "node table reached maximum size");
            mdd_nodes = UINT32_MAX;
        }
        node_table=RTrealloc(node_table,mdd_nodes*sizeof(struct mdd_node));
        for(uint32_t i=0;i<uniq_size;i++){
            unique_table[i]=0;
        }
        free_node=old_size;
        for(uint32_t i=old_size;i<mdd_nodes;i++){
            node_table[i].val=0;
            node_table[i].next=i+1;
        }
        node_table[mdd_nodes-1].next=0;
        for(uint32_t i=2;i<old_size;i++){
            if (node_table[i].next&0x80000000){
                node_table[i].next=node_table[i].next&0x7fffffff;
                uint32_t slot=hash(node_table[i].val,node_table[i].down,node_table[i].right)%uniq_size;
                node_table[i].next=unique_table[slot];
                unique_table[slot]=i;
            } else {
                node_table[i].next=free_node;
                free_node=i;
            }
        }
        Warning(debug,"node/unique tables have %u/%u entries",mdd_nodes,uniq_size);
    }
}

static double mdd_count(uint32_t mdd){
    if (mdd<=1) return mdd;
    uint32_t slot=hash(OP_COUNT,mdd,0)%cache_size;
    if (op_cache[slot].op==OP_COUNT && op_cache[slot].arg1==mdd){
        return op_cache[slot].res.count;
    }
    double res=mdd_count(node_table[mdd].down);
    res+=mdd_count(node_table[mdd].right);
    op_cache[slot].op=OP_COUNT;
    op_cache[slot].arg1=mdd;
    op_cache[slot].res.count=res;
    return res;
}

static uint32_t mdd_create_node(uint32_t val,uint32_t down,uint32_t right){
    if (down==0) return right;
    if (right > 1 && val >= node_table[right].val)
        Abort("bad order %u %u", val, node_table[right].val);
    uint32_t slot_hash=hash(val,down,right);
    uint32_t slot=slot_hash%uniq_size;
    uint32_t res=unique_table[slot];
    //Warning(debug, "mdd_create_node: slot_hash=%u, slot=%u, res=%u", slot_hash, slot, res);
    while(res){
        if (node_table[res].val==val
            && node_table[res].down==down
            && node_table[res].right==right) {
            return res;
        }
        res=node_table[res].next;
    }
    if (free_node==0) {
        mdd_collect(down,right);
        // recompute slot in case of resize.
        slot=slot_hash%uniq_size;
    }
    res=free_node;
    free_node=node_table[free_node].next;
    node_table[res].next=unique_table[slot];
    unique_table[slot]=res;
    node_table[res].val=val;
    node_table[res].down=down;
    node_table[res].right=right;
    return res;
}

static uint32_t mdd_union(uint32_t a,uint32_t b){
    if(a==b) return a;
    if(a==0) return b;
    if(b==0) return a;
    if(a==1 || b==1) Abort("missing case in union");
    if (b<a) { uint32_t tmp=a;a=b;b=tmp; }
    uint32_t slot_hash=hash(OP_UNION,a,b);
    uint32_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_UNION && op_cache[slot].arg1==a && op_cache[slot].res.other.arg2==b) {
        return op_cache[slot].res.other.res;
    }
    uint32_t tmp;
    if (node_table[a].val<node_table[b].val){
        tmp=mdd_union(node_table[a].right,b);
        tmp=mdd_create_node(node_table[a].val,node_table[a].down,tmp);
    } else if (node_table[a].val==node_table[b].val){
        tmp=mdd_union(node_table[a].down,node_table[b].down);
        mdd_push(tmp);
        tmp=mdd_union(node_table[a].right,node_table[b].right);
        tmp=mdd_create_node(node_table[a].val,mdd_pop(),tmp);
    } else { //(node_table[a].val>node_table[b].val)
        tmp=mdd_union(a,node_table[b].right);
        tmp=mdd_create_node(node_table[b].val,node_table[b].down,tmp);
    }
    slot=slot_hash%cache_size;
    op_cache[slot].op=OP_UNION;
    op_cache[slot].arg1=a;
    op_cache[slot].res.other.arg2=b;
    op_cache[slot].res.other.res=tmp;
    return tmp;
}

static uint32_t mdd_minus(uint32_t a,uint32_t b){
    if(a==b) return 0;
    if(a==0) return 0;
    if(b==0) return a;
    if(a==1||b==1) Abort("missing case in minus");
    uint32_t slot_hash=hash(OP_MINUS,a,b);
    uint32_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_MINUS && op_cache[slot].arg1==a && op_cache[slot].res.other.arg2==b) {
        return op_cache[slot].res.other.res;
    }
    uint32_t tmp;
    if (node_table[a].val<node_table[b].val){
        tmp=mdd_minus(node_table[a].right,b);
        tmp=mdd_create_node(node_table[a].val,node_table[a].down,tmp);
    } else if (node_table[a].val==node_table[b].val){
        mdd_push(mdd_minus(node_table[a].down,node_table[b].down));
        tmp=mdd_minus(node_table[a].right,node_table[b].right);
        tmp=mdd_create_node(node_table[a].val,mdd_pop(),tmp);
    } else { //(node_table[a].val>node_table[b].val)
        tmp=mdd_minus(a,node_table[b].right);
    }
    slot=slot_hash%cache_size;
    op_cache[slot].op=OP_MINUS;
    op_cache[slot].arg1=a;
    op_cache[slot].res.other.arg2=b;
    op_cache[slot].res.other.res=tmp;
    return tmp;
}


static uint32_t
mdd_member(uint32_t mdd, const uint32_t *vec, int len)
{
    if (len == 0) {
        if (mdd > 1) Abort("non-uniform length");
        return mdd;
    }

    while(mdd > 1) {
        if (node_table[mdd].val < vec[0]) {
            mdd = node_table[mdd].right;
        } else if (node_table[mdd].val == vec[0]) {
            return mdd_member(node_table[mdd].down, vec+1, len-1);
        } else {
            return 0;
        }
    }
    return 0;
}

static uint32_t mdd_put(uint32_t mdd,const uint32_t *vec,int len,int* is_new){
    if (len==0) {
        if (mdd==0) {
            if(is_new) *is_new=1;
            return 1;
       }
       if (mdd==1) {
            if(is_new) *is_new=0;
            return 1;
       }
       uint32_t tmp=mdd_put(node_table[mdd].right,vec,len,is_new);
       if (tmp!=node_table[mdd].right){
           return mdd_create_node(node_table[mdd].val,node_table[mdd].down,tmp);
       } else {
           return mdd;
       }
    }
    if (mdd>1) {
        if (node_table[mdd].val<vec[0]) {
            uint32_t right=mdd_put(node_table[mdd].right,vec,len,is_new);
            if (right==node_table[mdd].right){
                return mdd;
            } else {
                return mdd_create_node(node_table[mdd].val,node_table[mdd].down,right);
            }
        }
        if (node_table[mdd].val==vec[0]) {
            uint32_t down=mdd_put(node_table[mdd].down,vec+1,len-1,is_new);
            if (down==node_table[mdd].down){
                return mdd;
            } else {
                return mdd_create_node(node_table[mdd].val,down,node_table[mdd].right);
            }
        }
    }
    return mdd_create_node(vec[0],mdd_put(0,vec+1,len-1,is_new),mdd);
}

static void
mdd_enum(uint32_t mdd, uint32_t *vec, int idx, int len,
         vset_element_cb callback, void *context)
{
    if (idx == len) {
        if (len != 0 && mdd != 1) Abort("non-uniform length");
        if (mdd > 1) Abort("non-uniform length");
        if (mdd) callback(context, (int*)vec);
    } else {
        while (mdd > 1) {
            vec[idx] = node_table[mdd].val;
            mdd_enum(node_table[mdd].down, vec, idx+1, len, callback, context);
            mdd = node_table[mdd].right;
        }
        if (mdd != 0) Abort("non-uniform length");
    }
}

static uint32_t
mdd_copy_match(uint32_t p_id, uint32_t mdd, uint32_t pattern, int idx,
               int *proj, int len)
{
    if (mdd == 0) return 0;
    if (mdd == 1) return 1;
    if (len == 0) return mdd;

    uint32_t op        = OP_COPY_MATCH | (p_id << 16);
    uint32_t slot_hash = hash(op, mdd, pattern);
    uint32_t slot      = slot_hash % cache_size;

    if (op_cache[slot].op == op && op_cache[slot].arg1 == mdd
        && op_cache[slot].res.other.arg2 == pattern) {
        return op_cache[slot].res.other.res;
    }

    uint32_t res = 0;
    uint32_t tmp;
    uint32_t mdd_original = mdd;

    if (proj[0] == idx) {
        while (mdd > 1 && node_table[mdd].val < node_table[pattern].val)
            mdd = node_table[mdd].right;

        if (mdd > 1 && node_table[mdd].val == node_table[pattern].val) {
            tmp = mdd_copy_match(node_table[p_id].down, node_table[mdd].down,
                                 node_table[pattern].down,idx+1, proj+1, len-1);
            res = mdd_create_node(node_table[mdd].val, tmp, 0);
        }
    } else {
        while (mdd > 1) {
            mdd_push(res);
            tmp = mdd_copy_match(p_id, node_table[mdd].down, pattern, idx+1,
                                 proj, len);
            tmp = mdd_create_node(node_table[mdd].val, tmp, 0);
            mdd_push(tmp);
            res = mdd_union(res, tmp);
            mdd_pop(); mdd_pop();
            mdd =  node_table[mdd].right;
        }
    }

    slot = slot_hash % cache_size;
    op_cache[slot].op = op;
    op_cache[slot].arg1 = mdd_original;
    op_cache[slot].res.other.arg2 = pattern;
    op_cache[slot].res.other.res  = res;

    return res;
}

static uint32_t
mdd_intersect(uint32_t a, uint32_t b)
{
    if (a == b) return a;
    if (a == 0 || b == 0) return 0;
    if (a == 1 || b == 1) Abort("missing case in intersect");

    uint32_t slot_hash = hash(OP_INTERSECT, a, b);
    uint32_t slot = slot_hash % cache_size;

    if (op_cache[slot].op == OP_INTERSECT && op_cache[slot].arg1 == a
            && op_cache[slot].res.other.arg2 == b) {
        return op_cache[slot].res.other.res;
    }

    uint32_t tmp;

    if (node_table[a].val == node_table[b].val) {
        tmp = mdd_intersect(node_table[a].down, node_table[b].down);
        mdd_push(tmp);
        tmp = mdd_intersect(node_table[a].right, node_table[b].right);
        tmp = mdd_create_node(node_table[a].val, mdd_pop(), tmp);
    } else if (node_table[a].val < node_table[b].val) {
        tmp = mdd_intersect(node_table[a].right, b);
    } else { /* node_table[a].val > node_table[b].val */
        tmp = mdd_intersect(a, node_table[b].right);
    }

    slot = slot_hash % cache_size;
    op_cache[slot].op = OP_INTERSECT;
    op_cache[slot].arg1 = a;
    op_cache[slot].res.other.arg2 = b;
    op_cache[slot].res.other.res = tmp;

    return tmp;
}

/*
static uint32_t mdd_take(uint32_t mdd,int len,uint32_t count){
    if (mdd==0 || count==0) return 0;
    if (len>1) {
        uint32_t down_count=mdd_count(node_table[mdd].down,len-1);
        if (count<down_count){
            return mdd_create_node(node_table[mdd].val,mdd_take(node_table[mdd].down,len-1,count),0);
        } else {
            return mdd_create_node(node_table[mdd].val,node_table[mdd].down,
                mdd_take(node_table[mdd].right,len,count-down_count));
        }
    } else {
        return mdd_create_node(node_table[mdd].val,node_table[mdd].down,
                mdd_take(node_table[mdd].right,len,count-1));
    }
}
*/

static void mdd_clear_and_write_bin(stream_t s, uint32_t mdd, uint32_t* n_count, map_t node_map){
    if (mdd<=1) return;
    if (node_table[mdd].next&0x80000000) {
        node_table[mdd].next=node_table[mdd].next&0x7fffffff;
        mdd_clear_and_write_bin(s,node_table[mdd].down, n_count, node_map);
        mdd_clear_and_write_bin(s,node_table[mdd].right, n_count, node_map);
        simplemap_put(node_map, mdd, (uint32_t)*n_count);
        DSwriteU64(s, (uint64_t)*n_count);
        DSwriteU32(s, node_table[mdd].val);
        DSwriteU64(s, (uint64_t)simplemap_get(node_map, node_table[mdd].down));
        DSwriteU64(s, (uint64_t)simplemap_get(node_map, node_table[mdd].right));
        (*n_count)++;
    }
}

static void
mdd_save_bin(FILE* f, uint32_t mdd)
{
    stream_t s = stream_output(f);
    uint64_t n_count = (uint64_t)mdd_node_count(mdd);
    Print(infoLong,"mdd_save: %"PRIu64" / %u (%.0f%%)", n_count, mdd_nodes, 100*(((float)n_count)/(float)mdd_nodes));
    DSwriteU64(s, n_count);
    mdd_mark(mdd);
    map_t node_map = simplemap_create(n_count * 1.1 + 2);
    simplemap_put(node_map, 0, 0);
    simplemap_put(node_map, 1, 1);
    uint32_t count = 2;
    mdd_clear_and_write_bin(s, mdd, &count, node_map);
    simplemap_destroy(node_map);
    stream_flush(s);
    RTfree(s); //stream_close(&s);
}

static uint32_t
mdd_load_bin(FILE* f)
{
    stream_t s = stream_input(f);
    uint64_t n_count;
    n_count = DSreadU64(s);
    if (n_count < 2)
        n_count = 2;
    if (n_count > UINT32_MAX)
        Abort("Number of nodes is too large for 32-bit encoding.");
    if (mdd_load_node_ids != NULL)
        Abort("Error, mdd_load_node_ids already in use!");
    mdd_load_node_ids = RTmalloc(n_count*sizeof(uint32_t));
    mdd_load_node_ids[0] = 0;
    mdd_load_node_ids[1] = 1;
    mdd_load_node_count = 2;
    uint32_t mdd = 0;
    uint32_t id;
    uint32_t val;
    uint32_t down;
    uint32_t right;
    while (mdd_load_node_count < n_count && !stream_empty(s)) {
        id = (uint32_t)DSreadU64(s);
        val = DSreadU32(s);
        down = (uint32_t)DSreadU64(s);
        right = (uint32_t)DSreadU64(s);
        if (mdd_load_node_count != id)
            Abort("Nodes have to be numbered consecutively from 2 till n-1.");
        //Warning(debug, "id=%llu, val=%u, down=%llu [%llu], right=%llu [%llu]",
        //        id, val, down, mdd_load_node_ids[down], right, mdd_load_node_ids[right]);
        assert(down==0 || mdd_load_node_ids[down]!=0);
        assert(right==0 || mdd_load_node_ids[right]!=0);
        mdd = mdd_create_node(val, mdd_load_node_ids[down], mdd_load_node_ids[right]);
        //Warning(debug, "id=%llu [%llu]", id, mdd);
        mdd_load_node_ids[id] = mdd;
        mdd_load_node_count++;
    }
    RTfree(mdd_load_node_ids);
    RTfree(s); //stream_close(&s);
    mdd_load_node_ids = NULL;
    mdd_load_node_count = 0;
    return mdd;
}

static vset_t
set_create_mdd(vdom_t dom, int k, int *proj)
{
    assert(k <= dom->shared.size);
    int l = (k < 0)?0:k;
    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set) + sizeof(int[l]));
    set->dom  = dom;
    set->mdd  = 0;
    set->next = protected_sets;
    set->prev = NULL;
    if (protected_sets != NULL) protected_sets->prev = set;
    protected_sets = set;
    set->p_len = k;
    set->p_id  = 1;
    for(int i = k - 1; i >= 0; i--) {
        set->proj[i] = proj[i];
        set->p_id    = mdd_create_node(proj[i], set->p_id, 0);
    }
    return set;
}

static void
set_save_mdd(FILE* f, vset_t set)
{
    mdd_save_bin(f, set->mdd);
}

static vset_t
set_load_mdd(FILE* f, vdom_t dom)
{
    vset_t result = set_create_mdd(dom, -1, NULL);
    result->mdd = mdd_load_bin(f);
    return result;
}

static void set_destroy_mdd(vset_t set)
{
    if (protected_sets == set) protected_sets = set->next;
    if (set->prev != NULL) set->prev->next = set->next;
    if (set->next != NULL) set->next->prev = set->prev;

    RTfree(set);
}

static void set_reorder_mdd() { }

static vrel_t
rel_create_mdd(vdom_t dom, int k, int *proj)
{
    assert(0 <= k && k <= dom->shared.size);
    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation)
                                                         + sizeof(int[k]));
    rel->dom  = dom;
    rel->mdd  = 0;
    rel->next = protected_rels;
    rel->prev = NULL;
    if (protected_rels != NULL) protected_rels->prev = rel;
    protected_rels = rel;
    rel->p_len = k;
    rel->p_id  = 1;
    for(int i = k - 1; i >= 0; i--) {
        rel->proj[i] = proj[i];
        rel->p_id    = mdd_create_node(proj[i], rel->p_id, 0);
        // The p_id of a relation is shifted in hash keys; check this is ok
        if ((rel->p_id >> 16) > 0) Abort("rel_create_mdd: projection identifier too large");
    }
    return rel;
}

static void
rel_save_proj_bin(FILE* f, vrel_t rel)
{
    stream_t s = stream_output(f);
    DSwriteS32(s, rel->p_len);
    for(int i=0; i<rel->p_len; i++){
        DSwriteS32(s, rel->proj[i]);
    }
    stream_flush(s);
    RTfree(s); //stream_close(&s);
}

static void
rel_save_mdd(FILE* f, vrel_t rel)
{
    mdd_save_bin(f, rel->mdd);
}

static vrel_t
rel_load_proj_bin(FILE* f, vdom_t dom)
{
    stream_t s = stream_input(f);
    int p_len = DSreadS32(s);
    int proj[p_len];
    for(int i=0; i<p_len; i++){
        proj[i] = DSreadS32(s);
    }
    RTfree(s); //stream_close(&s);
    vrel_t result = rel_create_mdd(dom, p_len, proj);
    return result;
}

static void
rel_load_mdd(FILE* f, vrel_t rel)
{
    rel->mdd = mdd_load_bin(f);
}

static void
set_add_mdd(vset_t set, const int* e)
{
    int len = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    set->mdd = mdd_put(set->mdd, (uint32_t*)e, len, NULL);
}

static int
set_is_empty_mdd(vset_t set)
{
    return (set->mdd == 0);
}

static int
set_equal_mdd(vset_t set1, vset_t set2)
{
    return (set1->mdd == set2->mdd);
}

static void set_clear_mdd(vset_t set){
    set->mdd=0;
}

static void
set_copy_mdd(vset_t dst, vset_t src)
{
    assert(dst->p_id == src->p_id);
    dst->mdd = src->mdd;
}

static void
set_enum_mdd(vset_t set, vset_element_cb cb, void* context)
{
    int len = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    uint32_t vec[len];
    mdd_enum(set->mdd, vec, 0, len, cb, context);
}

static int set_member_mdd(vset_t set, const int* e)
{
    int len = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    return mdd_member(set->mdd, (uint32_t*)e, len);
}

static void
set_count_mdd(vset_t set, long *nodes, double *elements)
{
    if (nodes != NULL) *nodes = mdd_node_count(set->mdd);
    if (elements != NULL) *elements = mdd_count(set->mdd);
}

static void
rel_count_mdd(vrel_t rel, long *nodes, double *elements)
{
    if (nodes != NULL) *nodes = mdd_node_count(rel->mdd);
    if (elements != NULL) *elements = mdd_count(rel->mdd);
}

static void
set_union_mdd(vset_t dst, vset_t src)
{
    assert(dst->p_id == src->p_id);
    dst->mdd = mdd_union(dst->mdd, src->mdd);
}

static void
set_minus_mdd(vset_t dst, vset_t src)
{
    assert(dst->p_id == src->p_id && dst->p_len == src->p_len);
    dst->mdd = mdd_minus(dst->mdd, src->mdd);
}

static void
set_intersect_mdd(vset_t dst, vset_t src)
{
    assert(dst->p_id == src->p_id);
    dst->mdd = mdd_intersect(dst->mdd, src->mdd);
}

static void
set_copy_match_mdd(vset_t dst, vset_t src, int p_len, int *proj, int *match)
{
    assert(src->p_len == -1 && dst->p_len == -1 && p_len >= 0);
    uint32_t singleton = mdd_put(0, (uint32_t*)match, p_len, NULL);
    mdd_push(singleton);

    uint32_t p_id = mdd_put(0, (uint32_t*)proj, p_len, NULL); // FIXME: enable to pass a projection to this function!
    //Warning(info, "set_copy_match_mdd: p_id=%d", p_id);
    // The p_id of is shifted in hash keys; check this is ok
    if ((p_id >> 16) > 0) Abort("set_copy_match_mdd: projection identifier too large");
    mdd_push(p_id);

    dst->mdd = mdd_copy_match(p_id, src->mdd, singleton, 0, proj, p_len);
    mdd_pop(); mdd_pop();
}

static void
set_copy_match_proj_mdd(vset_t dst, vset_t src, int p_len, int *proj, int p_id, int *match)
{
    assert(src->p_len == -1 && dst->p_len == -1 && p_len >= 0);
    uint32_t singleton = mdd_put(0, (uint32_t*)match, p_len, NULL);
    mdd_push(singleton);

    //Warning(info, "set_copy_match_proj_mdd: p_id=%d", p_id);
    // The p_id of is shifted in hash keys; check this is ok
    if ((p_id >> 16) > 0) Abort("set_copy_match_proj_mdd: projection identifier too large");
    mdd_push(p_id);

    dst->mdd = mdd_copy_match(p_id, src->mdd, singleton, 0, proj, p_len);
    mdd_pop(); mdd_pop();
}

static int
proj_create_mdd(int p_len, int *proj)
{
    assert(p_len >= 0);
    uint32_t p_id = mdd_put(0, (uint32_t*)proj, p_len, NULL); // FIXME: enable to pass a projection to this function!
    //Warning(info, "proj_create_mdd: p_id=%d", p_id);
    // The p_id of is shifted in hash keys; check this is ok
    if ((p_id >> 16) > 0) Abort("set_copy_match_mdd: projection identifier too large");
    return p_id;
}

static void
set_enum_match_mdd(vset_t set, int p_len, int *proj, int *match,
                       vset_element_cb cb, void *context)
{
    assert(set->p_len == -1 && p_len >= 0);
    uint32_t singleton = mdd_put(0, (uint32_t*)match, p_len, NULL);
    mdd_push(singleton);

    uint32_t p_id = mdd_put(0, (uint32_t*)proj, p_len, NULL);
    // The p_id of is shifted in hash keys; check this is ok
    if ((p_id >> 16) > 0) Abort("set_enum_match_mdd: projection identifier too large");
    mdd_push(p_id);

    uint32_t tmp = mdd_copy_match(p_id, set->mdd, singleton, 0, proj, p_len);
    mdd_pop(); mdd_pop();

    int len = set->dom->shared.size;
    uint32_t vec[len];
    mdd_enum(tmp, vec, 0, len , cb, context);
}

static void
rel_add_mdd(vrel_t rel, const int *src, const int *dst)
{
    int N = (rel->p_len < 0)?rel->dom->shared.size:rel->p_len;
    uint32_t vec[2*N];
    for(int i = 0; i < N; i++) {
        vec[i+i]   = src[i];
        vec[i+i+1] = dst[i];
    }

    rel->mdd = mdd_put(rel->mdd, vec, 2*N, NULL);
}

static uint32_t
mdd_project(uint32_t p_id, uint32_t mdd, int idx, int *proj, int len)
{
    if(mdd == 0) return 0; //projection of empty is empty.
    if(len == 0) return 1; //projection of non-empty is epsilon.

    uint32_t slot_hash=hash(OP_PROJECT,mdd,p_id);
    uint32_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_PROJECT && op_cache[slot].arg1==mdd
       && op_cache[slot].res.other.arg2==p_id) {
        return op_cache[slot].res.other.res;
    }

    uint32_t res = 0;
    uint32_t mdd_original = mdd;

    if (proj[0]==idx){
        mdd_push(mdd_project(p_id,node_table[mdd].right,idx,proj,len));
        uint32_t tmp=mdd_project(node_table[p_id].down, node_table[mdd].down,
                                 idx+1, proj+1, len-1);
        res=mdd_create_node(node_table[mdd].val,tmp,mdd_pop());
    } else {
        while(mdd>1){
            mdd_push(res);
            uint32_t tmp=mdd_project(p_id,node_table[mdd].down,idx+1,proj,len);
            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            mdd=node_table[mdd].right;
        }
    }

    slot=slot_hash%cache_size;
    op_cache[slot].op=OP_PROJECT;
    op_cache[slot].arg1=mdd_original;
    op_cache[slot].res.other.arg2=p_id;
    op_cache[slot].res.other.res=res;
    return res;
}

static uint32_t
mdd_next(uint32_t p_id, uint32_t set, uint32_t rel, int idx, int *proj, int len)
{
    if (rel==0||set==0) return 0;
    if (len==0) return set;
    if (rel==1||set==1) Abort("missing case in next");
    if (proj[0]==idx){ // current level is affected => find match.
        while(node_table[set].val!=node_table[rel].val){
            if(node_table[set].val < node_table[rel].val) {
                set=node_table[set].right;
                if (set<=1) return 0;
            }
            if(node_table[rel].val < node_table[set].val) {
                rel=node_table[rel].right;
                if (rel<=1) return 0;
            }
        }
    }
    uint32_t op=OP_NEXT|(p_id<<16);
    uint32_t slot_hash=hash(op,set,rel);
    uint32_t old_rel=rel;
    uint32_t slot=slot_hash%cache_size;
    if (op_cache[slot].op==op && op_cache[slot].arg1==set
        && op_cache[slot].res.other.arg2==rel) {
        return op_cache[slot].res.other.res;
    }
    uint32_t res=0;
    if (proj[0]==idx){
        res = mdd_next(p_id, node_table[set].right, node_table[rel].right,
                       idx, proj, len);
        rel=node_table[rel].down;
        while(rel>1){
            mdd_push(res);
            uint32_t tmp = mdd_next(node_table[p_id].down, node_table[set].down,
                                    node_table[rel].down, idx+1, proj+1, len-1);
            tmp=mdd_create_node(node_table[rel].val,tmp,0);
            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            rel=node_table[rel].right;
        }
    } else {
        mdd_push(mdd_next(p_id,node_table[set].right,rel,idx,proj,len));
        res=mdd_next(p_id,node_table[set].down,rel,idx+1,proj,len);
        res=mdd_create_node(node_table[set].val,res,mdd_pop());
    }

    slot=slot_hash%cache_size;
    op_cache[slot].op=op;
    op_cache[slot].arg1=set;
    op_cache[slot].res.other.arg2=old_rel;
    op_cache[slot].res.other.res=res;
    return res;
}

static void
set_project_mdd(vset_t dst, vset_t src)
{
    if (src->p_len != -1 || dst->p_len == -1) {
        dst->mdd = src->mdd;
    } else {
        dst->mdd = 0;
        dst->mdd = mdd_project(dst->p_id, src->mdd, 0, dst->proj, dst->p_len);
    }
}

static void
set_next_mdd(vset_t dst, vset_t src, vrel_t rel)
{
    assert(src->p_len == -1 && dst->p_len == -1);

    dst->mdd = mdd_next(rel->p_id, src->mdd, rel->mdd, 0, rel->proj,rel->p_len);
}


static void
set_example_mdd(vset_t set, int *e)
{
    int len = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    uint32_t mdd = set->mdd;

    if (mdd == 0) Abort("empty set");

    for(int i = 0; i < len; i++){
        if (mdd == 1) Abort("non-uniform length");
        e[i] = node_table[mdd].val;
        mdd  = node_table[mdd].down;
    }

    if (mdd != 1) Abort("non-uniform length");
}


static uint32_t
mdd_prev(uint32_t p_id, uint32_t set, uint32_t rel, int idx, int *proj, int len)
{
    if (rel==0||set==0) return 0;
    if (len==0) return set;
    if (rel==1||set==1) Abort("missing case in prev");
    uint32_t op=OP_PREV|(p_id<<16);
    uint32_t slot_hash=hash(op,set,rel);
    uint32_t slot=slot_hash%cache_size;
    if (op_cache[slot].op==op && op_cache[slot].arg1==set
        && op_cache[slot].res.other.arg2==rel) {
        return op_cache[slot].res.other.res;
    }
    uint32_t res=0;
    if (proj[0]==idx){
        uint32_t right=mdd_prev(p_id,set,node_table[rel].right,idx,proj,len);
        mdd_push(right);
        uint32_t val=node_table[rel].val;
        rel=node_table[rel].down;
        while(rel>1 && set>1) {
            if (node_table[rel].val < node_table[set].val){
                rel=node_table[rel].right;
                continue;
            }
            if (node_table[set].val < node_table[rel].val){
                set=node_table[set].right;
                continue;
            }
            mdd_push(res);
            uint32_t tmp=mdd_prev(node_table[p_id].down, node_table[set].down,
                                  node_table[rel].down, idx+1, proj+1, len-1);
            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            rel=node_table[rel].right;
            set=node_table[set].right;
        }
        res=mdd_create_node(val,res,mdd_pop());
    } else {
        mdd_push(mdd_prev(p_id,node_table[set].right,rel,idx,proj,len));
        res=mdd_prev(p_id,node_table[set].down,rel,idx+1,proj,len);
        res=mdd_create_node(node_table[set].val,res,mdd_pop());
    }

    slot=slot_hash%cache_size;
    op_cache[slot].op=op;
    op_cache[slot].arg1=set;
    op_cache[slot].res.other.arg2=rel;
    op_cache[slot].res.other.res=res;
    return res;
}

static void
set_prev_mdd(vset_t dst, vset_t src, vrel_t rel, vset_t univ)
{
    assert(src->p_len == -1 && dst->p_len == -1);

    dst->mdd = mdd_prev(rel->p_id, src->mdd, rel->mdd, 0, rel->proj,rel->p_len);
    set_intersect_mdd(dst,univ);
}

static uint32_t
mdd_universe(uint32_t p_id, uint32_t dst, uint32_t src, int n) {

    if (src == 0) return 0;

    uint32_t slot_hash=hash(OP_UNIVERSE,src,p_id);
    uint32_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_UNIVERSE && op_cache[slot].arg1==src
       && op_cache[slot].res.other.arg2==p_id) {
        return op_cache[slot].res.other.res;
    }

    mdd_push(mdd_universe(p_id, dst, node_table[src].right, n));

    if (n == 0) {
        dst = mdd_create_node(node_table[src].val, dst, 0);
    } else {
        dst = mdd_universe(p_id, dst, node_table[src].down, n-1);
    }

    uint32_t res = mdd_union(dst, mdd_pop());

    slot=slot_hash%cache_size;
    op_cache[slot].op=OP_UNIVERSE;
    op_cache[slot].arg1=src;
    op_cache[slot].res.other.arg2=p_id;
    op_cache[slot].res.other.res=res;

    return res;

}

static void
set_universe_mdd(vset_t dst, vset_t src) {

    assert(src->p_len == -1 && dst->mdd == 0);

    dst->mdd = 1;

    int l = dst->p_len > 0 ? dst->p_len : src->dom->shared.size;

    uint64_t p_id = dst->p_id;

    for (int n = l-1; n >= 0; n--) {
        dst->mdd = mdd_universe(p_id, dst->mdd, src->mdd, dst->proj[n]);
        p_id = node_table[p_id].down;
    }
}

typedef struct {
    int tg_len;
    int *top_groups;
} top_groups_info;

static vrel_t *rel_set;
static vset_t *proj_set;
static uint32_t rels_tot;
static top_groups_info *top_groups;

static uint32_t saturate(int level, uint32_t mdd);
static uint32_t sat_rel_prod(uint32_t p_id, uint32_t set, uint32_t rel,
                             int idx, int *proj, int len);

static uint32_t
copy_level_sat(uint32_t p_id, uint32_t set, uint32_t rel, int idx,
               int *proj, int len)
{
    uint32_t res = 0;

    while (set > 0) {
        mdd_push(res);
        uint32_t tmp = sat_rel_prod(p_id, node_table[set].down, rel, idx + 1,
                                    proj, len);
        tmp = mdd_create_node(node_table[set].val, tmp, 0);
        mdd_push(tmp);
        res = mdd_union(res, tmp);
        mdd_pop(); mdd_pop();
        set = node_table[set].right;
    }

    return res;
}

static uint32_t
apply_rel_prod(uint32_t p_id, uint32_t set, uint32_t rel, int idx,
               int *proj, int len)
{
    uint32_t res = 0;

    while (set > 1 && rel > 1) {
        if (node_table[set].val < node_table[rel].val)
            set = node_table[set].right;
        else if (node_table[rel].val < node_table[set].val)
            rel = node_table[rel].right;
        else {
            uint32_t rel_down = node_table[rel].down;

            while (rel_down > 1) {
                mdd_push(res);
                uint32_t tmp = sat_rel_prod(node_table[p_id].down,
                                            node_table[set].down,
                                            node_table[rel_down].down,
                                            idx + 1, proj + 1, len - 1);
                tmp = mdd_create_node(node_table[rel_down].val, tmp, 0);
                mdd_push(tmp);
                res = mdd_union(res, tmp);
                mdd_pop(); mdd_pop();
                rel_down = node_table[rel_down].right;
            }

            set = node_table[set].right;
            rel = node_table[rel].right;
        }
    }

    return res;
}

static uint32_t
sat_rel_prod(uint32_t p_id, uint32_t set, uint32_t rel, int idx,
                int *proj, int len)
{
    if (len == 0) return set;
    if (set == 0 || rel == 0) return 0;
    if (set == 1 || rel == 1) Abort("missing case in set_reach_sat");

    uint32_t op = OP_RELPROD | (p_id << 16);
    uint32_t slot_hash = hash(op, set, rel);
    uint32_t slot = slot_hash % cache_size;

    if (op_cache[slot].op == op
          && op_cache[slot].arg1 == set
          && op_cache[slot].res.other.arg2 == rel)
        return op_cache[slot].res.other.res;

    uint32_t res = 0;

    if (proj[0] == idx)
        res = apply_rel_prod(p_id, set, rel, idx, proj, len);
    else
        res = copy_level_sat(p_id, set, rel, idx, proj, len);

    mdd_push(res);
    res = saturate(idx, res);
    mdd_pop();

    slot = slot_hash % cache_size;
    op_cache[slot].op=op;
    op_cache[slot].arg1=set;
    op_cache[slot].res.other.arg2=rel;
    op_cache[slot].res.other.res=res;
    return res;
}

static uint32_t
apply_rel_fixpoint(uint32_t p_id, uint32_t set, uint32_t rel, int idx,
                   int *proj, int len)
{
    uint32_t res = set;

    while (set > 1 && rel > 1) {
        if (node_table[set].val < node_table[rel].val)
            set = node_table[set].right;
        else if (node_table[rel].val < node_table[set].val)
            rel = node_table[rel].right;
        else {
            uint32_t new_res = res;
            uint32_t rel_down = node_table[rel].down;

            while (node_table[rel].val != node_table[new_res].val)
                new_res = node_table[new_res].right;

            while (rel_down > 1) {
                mdd_push(res);
                uint32_t tmp = sat_rel_prod(node_table[p_id].down,
                                            node_table[new_res].down,
                                            node_table[rel_down].down,
                                            idx + 1, proj + 1, len - 1);
                tmp = mdd_create_node(node_table[rel_down].val, tmp, 0);
                mdd_push(tmp);
                res = mdd_union(res, tmp);
                mdd_pop(); mdd_pop();
                rel_down = node_table[rel_down].right;
            }

            set = node_table[set].right;
            rel = node_table[rel].right;
        }
    }

    return res;
}

// Start fixpoint calculations on the MDD at a given level for transition groups
// whose top is at that level. Continue performing fixpoint calculations until
// the MDD does not change anymore.
static uint32_t
sat_fixpoint(int level, uint32_t set)
{
    if (set == 0) return 0;
    if (set == 1) Abort("missing case in sat_fixpoint");

    top_groups_info groups_info = top_groups[level];
    uint32_t new_set = set;

    mdd_push(0);

    while (new_set != mdd_pop()) {
        mdd_push(new_set);
        for (int i = 0; i < groups_info.tg_len; i++) {
            int grp = groups_info.top_groups[i];
            mdd_push(new_set);
            assert(rel_set[grp]->p_len != 0);

            if (rel_set[grp]->expand != NULL) {
                proj_set[grp]->mdd = mdd_project(rel_set[grp]->p_id, new_set,
                                                 level, proj_set[grp]->proj,
                                                 proj_set[grp]->p_len);
                rel_set[grp]->expand(rel_set[grp], proj_set[grp],
                                     rel_set[grp]->expand_ctx);
                proj_set[grp]->mdd = 0;
            }

            new_set = apply_rel_fixpoint(rel_set[grp]->p_id, new_set,
                                         rel_set[grp]->mdd, level,
                                         rel_set[grp]->proj,
                                         rel_set[grp]->p_len);
            mdd_pop();
        }
    }

    return new_set;
}

// Saturation process for the MDD at a given level
static uint32_t
saturate(int idx, uint32_t mdd)
{
    if (mdd == 0 || mdd == 1) return mdd;

    uint32_t slot_hash = hash(OP_SAT, mdd, rels_tot);
    uint32_t slot = slot_hash % cache_size;

    if (op_cache[slot].op == OP_SAT
          && op_cache[slot].arg1 == mdd
          && op_cache[slot].res.other.arg2 == rels_tot)
        return op_cache[slot].res.other.res;

    uint32_t res = 0;
    uint32_t mdd_right = mdd;

    while (mdd_right > 1) {
        mdd_push(res);
        uint32_t tmp = saturate(idx + 1, node_table[mdd_right].down);
        tmp = mdd_create_node(node_table[mdd_right].val, tmp, 0);
        mdd_push(tmp);
        res = mdd_union(res, tmp);
        mdd_pop(); mdd_pop();
        mdd_right = node_table[mdd_right].right;
    }

    mdd_push(res);
    res = sat_fixpoint(idx, res);
    mdd_pop();

    slot = slot_hash % cache_size;
    op_cache[slot].op = OP_SAT;
    op_cache[slot].arg1 = mdd;
    op_cache[slot].res.other.arg2 = rels_tot;
    op_cache[slot].res.other.res = res;
    return res;
}

// Perform fixpoint calculations using the "General Basic Saturation" algorithm
static void
set_least_fixpoint_mdd(vset_t dst, vset_t src, vrel_t rels[], int rel_count)
{
    // Only implemented if not projected
    assert(src->p_len == -1 && dst->p_len == -1);

    // Initialize partitioned transition relations.
    rel_set = rels;

    uint32_t rels_tmp = 0;

    for (int i = 0; i < rel_count; i++)
        rels_tmp = mdd_create_node(rel_count - i, rels[i]->mdd, rels_tmp);

    mdd_push(rels_tmp);
    rels_tot = rels_tmp;

    // Initialize top_groups_info array
    // This stores transition groups per topmost level
    int init_state_len = src->dom->shared.size;
    top_groups = RTmalloc(sizeof(top_groups_info[init_state_len]));
    proj_set = RTmalloc(sizeof(vset_t[rel_count]));

    for (int lvl = 0; lvl < init_state_len; lvl++) {
        top_groups[lvl].top_groups = RTmalloc(sizeof(int[rel_count]));
        top_groups[lvl].tg_len = 0;
    }

    for (int grp = 0; grp < rel_count; grp++) {
        proj_set[grp] = set_create_mdd(rels[grp]->dom, rels[grp]->p_len,
                                       rels[grp]->proj);

        if (rels[grp]->p_len == 0)
            continue;

        int top_lvl = rels[grp]->proj[0];
        top_groups[top_lvl].top_groups[top_groups[top_lvl].tg_len] = grp;
        top_groups[top_lvl].tg_len++;
    }

    // Saturation on initial state set
    dst->mdd = saturate(0, src->mdd);

    // Clean-up
    for (int grp = 0; grp < rel_count; grp++) {
        if (rels[grp]->p_len == 0 && rels[grp]->expand != NULL) {
            proj_set[grp]->mdd = mdd_project(rels[grp]->p_id, dst->mdd,
                                             0, NULL, 0);
            rel_set[grp]->expand(rel_set[grp], proj_set[grp],
                                 rel_set[grp]->expand_ctx);
        }

        vset_destroy(proj_set[grp]);
    }

    for (int lvl = 0; lvl < init_state_len; lvl++)
        RTfree(top_groups[lvl].top_groups);

    rel_set = NULL;
    rels_tot = 0;
    mdd_pop();

    RTfree(proj_set);
    RTfree(top_groups);
}


static void mdd_mark_for_dot(uint32_t mdd){
  // only heads of mdd-nodes are marked
  // note that head of one mdd-node might be in the middle of another one!
  // this means that "sharing within mdd-nodes" is not represented in dot.
    if (mdd<=1) return;
    if (node_table[mdd].next&0x80000000) return;
    node_table[mdd].next = node_table[mdd].next | 0x80000000;
    uint32_t x = mdd;
    while (x) {
      mdd_mark_for_dot(node_table[x].down);
      x=node_table[x].right;
    }
}

static char trueprinted;

static void mdd_clear_and_print(FILE* fp,uint32_t mdd){
  if (mdd==0) {fprintf(fp," n0 [shape=record,label=\"<f0> False\"]\n");}
  else if (mdd==1) {
    if (!trueprinted) {
      fprintf(fp," n1 [shape=record,label=\"<f0> True\"]\n");
      trueprinted=1;
    }
  }
  else if (node_table[mdd].next & 0x80000000) {
    node_table[mdd].next=node_table[mdd].next & 0x7fffffff;

    // print the mdd-node with values
    uint32_t x=mdd;
    int i=0;
    fprintf(fp," n%u [shape=record,label=\"",mdd);
    while (x) {
      if (i>0) fprintf(fp,"|");
      fprintf(fp,"<f%d> %"PRIu32, i, node_table[x].val);
      x=node_table[x].right;
      i++;
    }
    fprintf(fp,"\"];\n");

    // print the edges from this node
    x=mdd;
    i=0;
    while (x) {
      fprintf(fp,"   n%u:f%d -> n%u:f0;\n",mdd,i,node_table[x].down);
      x=node_table[x].right;
      i++;
    }

    // recursively print the sub-mdds
    x=mdd;
    while (x) {
      mdd_clear_and_print(fp,node_table[x].down);
      x=node_table[x].right;
    }
  }
}


static void set_dot_mdd(FILE* fp, vset_t src) {
  uint32_t mdd = src->mdd;
  fprintf(fp,"digraph setbdd {\nnode [shape=record];\n");
  mdd_mark_for_dot(mdd);
  trueprinted=0;
  mdd_clear_and_print(fp,mdd);
  fprintf(fp,"}\n");
}


static void rel_dot_mdd(FILE* fp, vrel_t src) {
  uint32_t mdd = src->mdd;
  fprintf(fp,"digraph relbdd {\nnode [shape=record];\n");
  mdd_mark_for_dot(mdd); // this changes mdd_used; does it matter?
  trueprinted=0;
  mdd_clear_and_print(fp,mdd);
  fprintf(fp,"}\n");
}

static long double mdd_ccount(uint32_t mdd){
    if (mdd<=1) return mdd;
    uint32_t slot1=hash(OP_CCOUNT1,mdd,0)%cache_size;
    uint32_t slot2=hash(OP_CCOUNT2,mdd,0)%cache_size;

    union {
        long double count;
        struct {
            uint64_t p1;
            uint64_t p2;
        } s;
    } res;

    res.count=0.0;
    if (op_cache[slot1].op==OP_CCOUNT1 && op_cache[slot1].arg1==mdd
            && op_cache[slot2].op==OP_CCOUNT2 && op_cache[slot2].arg1==mdd){
        res.s.p1=op_cache[slot1].res.count_part;
        res.s.p2=op_cache[slot2].res.count_part;
        return res.count;
    }
    res.count=mdd_ccount(node_table[mdd].down);
    res.count+=mdd_ccount(node_table[mdd].right);
    op_cache[slot1].op=OP_CCOUNT1;
    op_cache[slot1].arg1=mdd;
    op_cache[slot1].res.count_part=res.s.p1;
    op_cache[slot2].op=OP_CCOUNT2;
    op_cache[slot2].arg1=mdd;
    op_cache[slot2].res.count_part=res.s.p2;
    return res.count;
}

static void
set_ccount_mdd(vset_t set, long *nodes, long double *elements)
{
    if (nodes != NULL) *nodes = mdd_node_count(set->mdd);
    if (elements != NULL) *elements = mdd_ccount(set->mdd);
}

static void
dom_clear_cache(vdom_t dom, const int cache_op)
{
    (void) dom;
    const uint32_t op = (uint32_t) OP_VISIT | (cache_op << 8);

    for (uint32_t i = 0 ; i < cache_size; i++) {
        if ((op_cache[i].op & 0xffff) == op) op_cache[i].op=OP_UNUSED;
    }

    next_cache_op = MAX_OP;
}

static void
mdd_visit_proj(uint32_t set, vset_visit_callbacks_t* cbs, size_t ctx_size, void* context, uint32_t cache_op, uint32_t p_id)
{
    if (set <= 1) {
        if (cbs->vset_visit_pre != NULL) cbs->vset_visit_pre(1, set, 0, NULL, context);
        return;
    }

    uint32_t op = cache_op | (p_id << 16);
    uint32_t slot = hash(op, set, 0) % cache_size;

    void* result;
    if (op_cache[slot].op == op && op_cache[slot].arg1 == set){
        result = op_cache[slot].res.any;
        if (cbs->vset_visit_pre != NULL) cbs->vset_visit_pre(0, node_table[set].val, 1, result, context);
        return;
    }

    if (cbs->vset_visit_pre != NULL) cbs->vset_visit_pre(0, node_table[set].val, 0, NULL, context);

    void* context_down = alloca(ctx_size);
    void* context_right = alloca(ctx_size);
    if (cbs->vset_visit_init_context != NULL) {
        cbs->vset_visit_init_context(context_down, context, 1);
        cbs->vset_visit_init_context(context_right, context, 0);
    }

    mdd_visit_proj(node_table[set].down, cbs, ctx_size, context_down, cache_op, node_table[p_id].down);
    mdd_visit_proj(node_table[set].right, cbs, ctx_size, context_right, cache_op, p_id);

    if (cbs->vset_visit_post != NULL) {
        int cache = 0;
        cbs->vset_visit_post(node_table[set].val, context, &cache, &result);

        if (cache) {
            op_cache[slot].op = op;
            op_cache[slot].arg1 = set;
            op_cache[slot].res.any = result;
            if(cbs->vset_visit_cache_success != NULL) cbs->vset_visit_cache_success(context, result);
        }
    }
}

static void
mdd_visit(uint32_t set, vset_visit_callbacks_t* cbs, size_t ctx_size, void* context, uint32_t cache_op)
{
    if (set <= 1) {
        if (cbs->vset_visit_pre != NULL) cbs->vset_visit_pre(1, set, 0, NULL, context);
        return;
    }

    uint32_t slot = hash(cache_op, set, 0) % cache_size;

    void* result;
    if (op_cache[slot].op == cache_op && op_cache[slot].arg1 == set){
        result = op_cache[slot].res.any;
        if (cbs->vset_visit_pre != NULL) cbs->vset_visit_pre(0, node_table[set].val, 1, result, context);
        return;
    }

    cbs->vset_visit_pre(0, node_table[set].val, 0, NULL, context);

    void* context_down = alloca(ctx_size);
    void* context_right = alloca(ctx_size);
    if (cbs->vset_visit_init_context != NULL) {
        cbs->vset_visit_init_context(context_down, context, 1);
        cbs->vset_visit_init_context(context_right, context, 0);
    }

    mdd_visit(node_table[set].down, cbs, ctx_size, context_down, cache_op);
    mdd_visit(node_table[set].right, cbs, ctx_size, context_right, cache_op);

    if (cbs->vset_visit_post != NULL) {
        int cache = 0;
        cbs->vset_visit_post(node_table[set].val, context, &cache, &result);

        if (cache) {
            op_cache[slot].op = cache_op;
            op_cache[slot].arg1 = set;
            op_cache[slot].res.any = result;
            if(cbs->vset_visit_cache_success != NULL) cbs->vset_visit_cache_success(context, result);
        }
    }
}

static void
set_visit_mdd(vset_t set, vset_visit_callbacks_t* cbs, size_t ctx_size, void* context, int cache_op)
{
    if (cache_op > 0xff) Abort("cache op too large");

    const uint32_t op = (uint32_t) OP_VISIT | (cache_op << 8);

    if (set->p_len != -1) mdd_visit_proj(set->mdd, cbs, ctx_size, context, op, set->p_id);
    else mdd_visit(set->mdd, cbs, ctx_size, context, op);
}

static int
dom_next_cache_op(vdom_t dom)
{
    (void) dom;
    
    next_cache_op++;

    if (next_cache_op > (int) 0xff) {
        Abort("No more free user cache operations");
    }

    return next_cache_op;
}

vdom_t vdom_create_list_native(int n){
    Warning(info,"Creating a native ListDD domain.");
    vdom_t dom=(vdom_t)RTmalloc(sizeof(struct vector_domain));
    vdom_init_shared(dom,n);
    if (unique_table==NULL) {
        mdd_nodes=(nodes_fib <= FIB_MAX)?fib(nodes_fib):UINT32_MAX;
        Warning(info,"initial node table has %" PRIu32 " entries",mdd_nodes);
        uniq_size=(nodes_fib + 1 <= FIB_MAX)?fib(nodes_fib+1):UINT32_MAX;
        Warning(info,"initial uniq table has %" PRIu32 " entries",uniq_size);
        cache_size=(nodes_fib+cache_fib <= FIB_MAX)?fib(nodes_fib+cache_fib):UINT32_MAX;
        Warning(info,"initial op cache has %" PRIu32" entries",cache_size);

        unique_table=RTmalloc(uniq_size*sizeof(uint32_t));
        node_table=RTmalloc(mdd_nodes*sizeof(struct mdd_node));
        op_cache=RTalign(CACHE_LINE_SIZE,cache_size*sizeof(struct op_rec));

        for(uint32_t i=0;i<uniq_size;i++){
            unique_table[i]=0;
        }
        node_table[0].val=0;
        node_table[1].val=0;
        for(uint32_t i=2;i<mdd_nodes;i++){
            node_table[i].val=0;
            node_table[i].next=i+1;
        }
        node_table[mdd_nodes-1].next=0;
        free_node=2;
        for(uint32_t i=0;i<cache_size;i++){
            op_cache[i].op=0;
        }

        mdd_create_stack();
    }
    dom->shared.set_create=set_create_mdd;
    dom->shared.set_save=set_save_mdd;
    dom->shared.set_load=set_load_mdd;
    dom->shared.set_member=set_member_mdd;
    dom->shared.set_add=set_add_mdd;
    dom->shared.set_is_empty=set_is_empty_mdd;
    dom->shared.set_equal=set_equal_mdd;
    dom->shared.set_clear=set_clear_mdd;
    dom->shared.set_copy=set_copy_mdd;
    dom->shared.set_enum=set_enum_mdd;
    dom->shared.set_count=set_count_mdd;
    dom->shared.set_ccount=set_ccount_mdd;
    dom->shared.set_union=set_union_mdd;
    dom->shared.set_minus=set_minus_mdd;
    dom->shared.rel_create=rel_create_mdd;
    dom->shared.rel_save_proj=rel_save_proj_bin;
    dom->shared.rel_save=rel_save_mdd;
    dom->shared.rel_load_proj=rel_load_proj_bin;
    dom->shared.rel_load=rel_load_mdd;
    dom->shared.rel_add=rel_add_mdd;
    dom->shared.rel_count=rel_count_mdd;
    dom->shared.set_project=set_project_mdd;
    dom->shared.set_next=set_next_mdd;
    dom->shared.set_prev=set_prev_mdd;
    dom->shared.set_universe=set_universe_mdd;
    dom->shared.set_example=set_example_mdd;
    dom->shared.set_enum_match=set_enum_match_mdd;
    dom->shared.set_copy_match=set_copy_match_mdd;
    dom->shared.set_copy_match_proj=set_copy_match_proj_mdd;
    dom->shared.proj_create=proj_create_mdd;
    dom->shared.set_intersect=set_intersect_mdd;
    // default implementation for dom->shared.set_zip
    dom->shared.reorder=set_reorder_mdd;
    dom->shared.set_destroy=set_destroy_mdd;
    dom->shared.set_least_fixpoint=set_least_fixpoint_mdd;
    dom->shared.set_dot=set_dot_mdd;
    dom->shared.rel_dot=rel_dot_mdd;
    dom->shared.set_visit_seq=set_visit_mdd;
    dom->shared.dom_clear_cache=dom_clear_cache;
    dom->shared.dom_next_cache_op=dom_next_cache_op;
    return dom;
}
