#include <config.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include <hre/user.h>
#include <hre-io/user.h>
#include <vset-lib/vdom_object.h>
#include <util-lib/simplemap.h>
#include <util-lib/fast_hash.h>
#include <util-lib/util.h>

static uint64_t mdd_nodes;
static uint64_t uniq_size;
static uint64_t cache_size;

/** fibonacci number of the size of the node table. */
static int nodes_fib=30;
/** Maximum fibonacci number before overflow. */
static const int FIB_MAX = 93;
/** difference between the fibonacci numbers of the sizes of the node table and the cache. */
static int cache_fib=1;

static uint64_t fib(int n){
    uint64_t tmp1=0;
    uint64_t tmp2=1;
    while(n>0){
        uint64_t tmp=tmp1;
        tmp1=tmp2;
        tmp2+=tmp;
        n--;
    }
    return tmp1;
}

struct poptOption listdd64_options[]= {
    { "ldd-step", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &nodes_fib, 0, "set intial step in node size", "<step>"},
    { "ldd-cache", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cache_fib , 0 , "set Fibonacci difference between operation op cache and nodes (may be negative)","<diff>"},
    POPT_TABLEEND
};

static uint64_t free_node=1;
static uint64_t* unique_table=NULL;
struct mdd_node {
    uint64_t next;
    uint16_t reachable;
    uint16_t copy;
    uint32_t val;
    uint64_t down;
    uint64_t right;
};

#define COPY_WRITE 0
#define COPY_COPY 1
#define COPY_DONT_CARE 2

static uint32_t *set_copy;

static struct mdd_node *node_table=NULL;
struct op_rec {
    uint32_t dummy;
    uint32_t op;
    uint64_t arg1;
    union {
        double count;
        struct {
            uint64_t arg2;
            uint64_t arg3;
            uint64_t arg4;
            uint64_t res;
            uint64_t dummy1;
            uint64_t dummy2;
        } other;
    } res;
};

static struct op_rec *op_cache=NULL;

// op cache operations
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
#define OP_JOIN 12
#define OP_CCOUNT1 13
#define OP_CCOUNT2 14

static void cache_put(uint64_t slot_hash,
                      uint32_t op,
                      uint64_t arg1,
                      uint64_t arg2,
                      uint64_t arg3,
                      uint64_t arg4,
                      uint64_t res) {
    uint64_t slot=slot_hash%cache_size;
    op_cache[slot].op=op;
    op_cache[slot].arg1=arg1;
    op_cache[slot].res.other.arg2=arg2;
    op_cache[slot].res.other.arg3=arg3;
    op_cache[slot].res.other.arg4=arg4;
    op_cache[slot].res.other.res=res;
}

struct vector_domain {
    struct vector_domain_shared shared;
    // single global structure for now.
};

struct vector_set {
    vdom_t dom;
    vset_t next; // double linked list of protected mdd's;
    vset_t prev; //
    uint64_t mdd;
    uint64_t p_id;
    int p_len;
    int proj[];
};

struct vector_relation {
    vdom_t dom;
    expand_cb expand;
    void *expand_ctx;
    vrel_t next; // double linked list of protected mdd's;
    vrel_t prev; //
    uint64_t mdd;
    uint64_t p_id;
    int r_p_len;
    int w_p_len;
    int* r_proj;
    int* w_proj;
};

static uint64_t hash2(uint32_t a,uint64_t b){
    uint64_t x[1];
    x[0] = b;
    return MurmurHash64(x, sizeof(uint64_t), a);
}

static uint64_t hash3(uint32_t a,uint64_t b,uint64_t c){
    uint64_t x[2];
    x[0] = b;
    x[1] = c;
    return MurmurHash64(x, 2*sizeof(uint64_t), a);
}

static uint64_t hash4(uint32_t a,uint64_t b,uint64_t c,uint64_t d){
    uint64_t x[3];
    x[0] = b;
    x[1] = c;
    x[2] = d;
    return MurmurHash64(x, 3*sizeof(uint64_t), a);
}

static uint64_t hash5(uint32_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e){
    uint64_t x[4];
    x[0] = b;
    x[1] = c;
    x[2] = d;
    x[3] = e;
    return MurmurHash64(x, 4*sizeof(uint64_t), a);
}

static vset_t protected_sets=NULL;
static vrel_t protected_rels=NULL;

static uint64_t* mdd_load_node_ids=NULL;
static uint64_t mdd_load_node_count=0;

static uint64_t mdd_used;

/** fibonacci number of the size of the stack. */
static uint64_t stack_fib=22;

static uint64_t stack_size;
static uint64_t *mdd_stack = NULL;
static uint64_t mdd_top=0;

static void
mdd_create_stack()
{
    stack_size = fib(stack_fib);
    mdd_stack = RTmalloc(sizeof(uint64_t[stack_size]));
    Warning(info, "initial stack size %"PRIu64, stack_size);
}

static void
mdd_push(uint64_t mdd)
{
    if (mdd_top==stack_size) {
        uint64_t stack_size_new = fib(++stack_fib);

        if (stack_size_new < stack_size)
            Abort("stack overflow");

        mdd_stack = RTrealloc(mdd_stack, sizeof(uint64_t[stack_size_new]));
        stack_size = stack_size_new;
        Warning(debug, "new stack size %"PRIu64"", stack_size);
    }
    mdd_stack[mdd_top]=mdd;
    mdd_top++;
}

static uint64_t
mdd_pop()
{
    if (mdd_top==0) Abort("stack underflow");
    mdd_top--;
    return mdd_stack[mdd_top];
}

static void mdd_mark(uint64_t mdd){
    if (mdd<=1) return;
    if (node_table[mdd].reachable) return;
    mdd_used++;
    node_table[mdd].reachable=1;
    mdd_mark(node_table[mdd].down);
    mdd_mark(node_table[mdd].right);
}

static void mdd_clear_and_count(uint64_t mdd,uint64_t *count){
    if (mdd<=1) return;
    if (node_table[mdd].reachable) {
        node_table[mdd].reachable=0;
        (*count)++;
        mdd_clear_and_count(node_table[mdd].down,count);
        mdd_clear_and_count(node_table[mdd].right,count);
    }
}

static uint64_t
mdd_node_count(uint64_t mdd)
{
    if (mdd <= 1) return 1; // emptyset or singleton

    uint64_t res = 0;
    mdd_mark(mdd);
    mdd_clear_and_count(mdd, &res);
    return res+2; // real nodes plus emptyset(0) and singleton epsilon(1).
}

static uint64_t mdd_sweep_bucket(uint64_t mdd){
    if (mdd==0) return 0;
    if (mdd==1) Abort("data corruption");
    if (node_table[mdd].reachable){
        node_table[mdd].reachable=0;
        node_table[mdd].next=mdd_sweep_bucket(node_table[mdd].next);
        return mdd;
    } else {
        uint64_t tmp=node_table[mdd].next;
        node_table[mdd].next=free_node;
        free_node=mdd;
        return mdd_sweep_bucket(tmp);
    }
}

static void mdd_collect(uint64_t a,uint64_t b){
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
    for(uint64_t i=0;i<mdd_top;i++){
        mdd_mark(mdd_stack[i]);
    }
    for(uint64_t i=0;i<mdd_load_node_count;i++){
        mdd_mark(mdd_load_node_ids[i]);
    }
    Warning(debug, "ListDD64 garbage collection: %"PRIu64" of %"PRIu64" nodes used",
            mdd_used, mdd_nodes);
    if (mdd_used == UINT64_MAX)
        Abort("Node table full at maximum size");

    int resize=0;
    // The two assignments below are not needed, but silence compiler warnings
    uint64_t new_cache_size = cache_size;
    struct op_rec *new_cache = op_cache;
    uint64_t copy_count=0;

    if (mdd_used > fib(nodes_fib-1) && mdd_nodes != UINT64_MAX) {
        Warning(debug,"insufficient free nodes, resizing");
        resize=1;
        nodes_fib++;
        new_cache_size=fib(nodes_fib+cache_fib);
        new_cache=RTalign(CACHE_LINE_SIZE,new_cache_size*sizeof(struct op_rec));
        for(uint64_t i=0;i<new_cache_size;i++){
            new_cache[i].op=OP_UNUSED;
        }
        if (new_cache_size < cache_size) {
            Warning(debug, "op cache reached maximum size");
            new_cache_size = UINT64_MAX;
        }
        Warning(debug, "new op cache has %"PRIu64" entries", new_cache_size);
    }
    for(uint64_t i=0;i<cache_size;i++){
        uint64_t slot;
        switch(op_cache[i].op){
            case OP_UNUSED: continue;
            case OP_COUNT:
            case OP_CCOUNT1:
            case OP_CCOUNT2:
            {
                if (!node_table[op_cache[i].arg1].reachable){
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) {
                    slot=hash2(op_cache[i].op,op_cache[i].arg1)%new_cache_size;
                    break;
                }
                else continue;
            }
            case OP_PROJECT:
            case OP_UNIVERSE:
            {
                if (!node_table[op_cache[i].arg1].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.res].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) {
                    slot=hash3(op_cache[i].op,op_cache[i].arg1,op_cache[i].res.other.arg2)%new_cache_size;
                    break;
                }
                else continue;
            }
            case OP_UNION:
            case OP_MINUS:
            case OP_INTERSECT:
            case OP_SAT:
            {
                if (!node_table[op_cache[i].arg1].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.arg2].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.res].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) {
                    slot=hash3(op_cache[i].op,op_cache[i].arg1,op_cache[i].res.other.arg2)%new_cache_size;
                    break;
                }
                else continue;
            }
            case OP_COPY_MATCH:
            case OP_NEXT:
            case OP_RELPROD:
            {
                if (!node_table[op_cache[i].arg1].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.arg2].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.res].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) {
                    slot=hash4(op_cache[i].op,op_cache[i].arg1,op_cache[i].res.other.arg2, op_cache[i].res.other.arg3)%new_cache_size;
                    break;
                }
                else continue;
            }
            case OP_PREV:
            {
                if (!node_table[op_cache[i].arg1].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.arg2].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.arg3].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.res].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) {
                    slot=hash5(op_cache[i].op,op_cache[i].arg1,op_cache[i].res.other.arg2,op_cache[i].res.other.arg3,op_cache[i].res.other.arg4)%new_cache_size;
                    break;
                }
                else continue;
            }
            case OP_JOIN:
            {
                if (!node_table[op_cache[i].arg1].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.arg2].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (!node_table[op_cache[i].res.other.res].reachable) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                if (resize) {
                    slot=hash5(op_cache[i].op,op_cache[i].arg1,op_cache[i].res.other.arg2,op_cache[i].res.other.arg3,op_cache[i].res.other.arg4)%new_cache_size;
                    break;
                }
                else continue;
            }
            default: Abort("missing case in collect");
        }
        copy_count++;
        new_cache[slot]=op_cache[i];
    }
    if (!resize){
        for(uint64_t i=0;i<uniq_size;i++){
            unique_table[i]=mdd_sweep_bucket(unique_table[i]);
        }
    } else {
        Warning(debug,"copied %"PRIu64" op cache nodes",copy_count);
        RTalignedFree(op_cache);
        op_cache=new_cache;
        cache_size=new_cache_size;
        uint64_t old_size=mdd_nodes;
        mdd_nodes=fib(nodes_fib);

        if (uniq_size != UINT64_MAX) {
            uniq_size = fib(nodes_fib + 1);
            if (uniq_size < mdd_nodes) {
                Warning(debug, "unique table reached maximum size");
                uniq_size = UINT64_MAX;
            }
            unique_table = RTrealloc(unique_table, uniq_size*sizeof(uint64_t));
        } else if (mdd_nodes < old_size) {
            Warning(debug, "node table reached maximum size");
            mdd_nodes = UINT64_MAX;
        }

        node_table=RTrealloc(node_table,mdd_nodes*sizeof(struct mdd_node));
        for(uint64_t i=0;i<uniq_size;i++){
            unique_table[i]=0;
        }
        free_node=old_size;
        for(uint64_t i=old_size;i<mdd_nodes;i++){
            node_table[i].val=0;
            node_table[i].copy=COPY_COPY;
            node_table[i].next=i+1;
        }
        node_table[mdd_nodes-1].next=0;
        for(uint64_t i=2;i<old_size;i++){
            if (node_table[i].reachable){
                node_table[i].reachable=0;
                uint64_t slot=hash4(node_table[i].val,node_table[i].down,node_table[i].right,node_table[i].copy)%uniq_size;
                node_table[i].next=unique_table[slot];
                unique_table[slot]=i;
            } else {
                node_table[i].next=free_node;
                free_node=i;
            }
        }
        Warning(debug,"node/unique tables have %"PRIu64"/%"PRIu64" entries",mdd_nodes,uniq_size);
    }
}

static double mdd_count(uint64_t mdd){
    if (mdd<=1) return mdd;
    uint64_t slot_hash=hash2(OP_COUNT,mdd);
    uint64_t slot = slot_hash%cache_size;
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

static uint64_t mdd_create_node(uint32_t val,uint64_t down,uint64_t right,uint32_t copy){
    if (down==0) return right;
    if (right > 1 &&
            ((val >= node_table[right].val && copy == node_table[right].copy) ||
            (val == node_table[right].val && copy >= node_table[right].copy)))
        Abort("bad order %u (%u) %u (%u)", val, copy, node_table[right].val, node_table[right].copy);

    uint64_t slot_hash=hash4(val,down,right,copy);
    uint64_t slot=slot_hash%uniq_size;
    uint64_t res=unique_table[slot];
    //Warning(debug, "mdd_create_node: slot_hash=%u, slot=%u, res=%u", slot_hash, slot, res);
    while(res){
        if (node_table[res].val==val
            && node_table[res].down==down
            && node_table[res].right==right
            && node_table[res].copy==copy) {
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
    node_table[res].copy=copy;
    return res;
}

static uint64_t mdd_union(uint64_t a,uint64_t b){
    if(a==b) return a;
    if(a==0) return b;
    if(b==0) return a;
    if(a==1 || b==1) Abort("missing case in union: a=%" PRIu64 ", b=%" PRIu64 "", a, b);
    if (b<a) { uint64_t tmp=a;a=b;b=tmp; }
    uint64_t slot_hash=hash3(OP_UNION,a,b);
    uint64_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_UNION && op_cache[slot].arg1==a && op_cache[slot].res.other.arg2==b) {
        return op_cache[slot].res.other.res;
    }
    uint64_t tmp;
    if (node_table[a].val<node_table[b].val){
        tmp=mdd_union(node_table[a].right,b);
        tmp=mdd_create_node(node_table[a].val,node_table[a].down,tmp,COPY_DONT_CARE);
    } else if (node_table[a].val==node_table[b].val){
        tmp=mdd_union(node_table[a].down,node_table[b].down);
        mdd_push(tmp);
        tmp=mdd_union(node_table[a].right,node_table[b].right);
        tmp=mdd_create_node(node_table[a].val,mdd_pop(),tmp,COPY_DONT_CARE);
    } else { //(node_table[a].val>node_table[b].val)
        tmp=mdd_union(a,node_table[b].right);
        tmp=mdd_create_node(node_table[b].val,node_table[b].down,tmp,COPY_DONT_CARE);
    }
    cache_put(slot_hash, OP_UNION, a, b, 0, 0, tmp);
    return tmp;
}

static uint64_t mdd_minus(uint64_t a,uint64_t b){
    if(a==b) return 0;
    if(a==0) return 0;
    if(b==0) return a;
    if(a==1||b==1) Abort("missing case in minus");
    uint64_t slot_hash=hash3(OP_MINUS,a,b);
    uint64_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_MINUS && op_cache[slot].arg1==a && op_cache[slot].res.other.arg2==b) {
        return op_cache[slot].res.other.res;
    }
    uint64_t tmp;
    if (node_table[a].val<node_table[b].val){
        tmp=mdd_minus(node_table[a].right,b);
        tmp=mdd_create_node(node_table[a].val,node_table[a].down,tmp,COPY_DONT_CARE);
    } else if (node_table[a].val==node_table[b].val){
        mdd_push(mdd_minus(node_table[a].down,node_table[b].down));
        tmp=mdd_minus(node_table[a].right,node_table[b].right);
        tmp=mdd_create_node(node_table[a].val,mdd_pop(),tmp,COPY_DONT_CARE);
    } else { //(node_table[a].val>node_table[b].val)
        tmp=mdd_minus(a,node_table[b].right);
    }
    cache_put(slot_hash,OP_MINUS, a, b, 0, 0, tmp);
    return tmp;
}


static uint64_t
mdd_member(uint64_t mdd, const uint32_t *vec, int len)
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

static uint64_t mdd_put(uint64_t mdd,const uint32_t *vec,int len,int* is_new, const uint32_t *cpy_vec){
    if (len==0) {
        if (mdd==0) {
            if(is_new) *is_new=1;
            return 1;
       }
       if (mdd==1) {
            if(is_new) *is_new=0;
            return 1;
       }
       uint64_t tmp=mdd_put(node_table[mdd].right,vec,len,is_new, cpy_vec);
       if (tmp!=node_table[mdd].right){
           return mdd_create_node(node_table[mdd].val,node_table[mdd].down,tmp,node_table[mdd].copy);
       } else {
           return mdd;
       }
    }
    if (mdd>1) {
        if (node_table[mdd].val<vec[0] || node_table[mdd].copy < cpy_vec[0]) {
            uint64_t right=mdd_put(node_table[mdd].right,vec,len,is_new,cpy_vec);
            if (right==node_table[mdd].right){
                return mdd;
            } else {
                return mdd_create_node(node_table[mdd].val,node_table[mdd].down,right,node_table[mdd].copy);
            }
        }
        if (node_table[mdd].val==vec[0] && node_table[mdd].copy==cpy_vec[0]) {
            uint64_t down=mdd_put(node_table[mdd].down,vec+1,len-1,is_new,cpy_vec+1);
            if (down==node_table[mdd].down){
                return mdd;
            } else {
                return mdd_create_node(node_table[mdd].val,down,node_table[mdd].right,node_table[mdd].copy);
            }
        }
    }
    return mdd_create_node(vec[0],mdd_put(0,vec+1,len-1,is_new,cpy_vec+1),mdd,cpy_vec[0]);
}

static void
mdd_enum(uint64_t mdd, uint32_t *vec, int idx, int len,
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

static uint64_t
mdd_copy_match(uint64_t p_id, uint64_t mdd, uint64_t pattern, int idx,
               int *proj, int len)
{
    if (mdd == 0) return 0;
    if (mdd == 1) return 1;
    if (len == 0) return mdd;

    uint64_t slot_hash = hash4(OP_COPY_MATCH, mdd, pattern, p_id);
    uint64_t slot      = slot_hash % cache_size;

    if (op_cache[slot].op == OP_COPY_MATCH && op_cache[slot].arg1 == mdd
        && op_cache[slot].res.other.arg2 == pattern
        && op_cache[slot].res.other.arg3 == p_id) {
        return op_cache[slot].res.other.res;
    }

    uint64_t res = 0;
    uint64_t tmp;
    uint64_t mdd_original = mdd;

    if (proj[0] == idx) {
        while (mdd > 1 && node_table[mdd].val < node_table[pattern].val)
            mdd = node_table[mdd].right;

        if (mdd > 1 && node_table[mdd].val == node_table[pattern].val) {
            tmp = mdd_copy_match(node_table[p_id].down, node_table[mdd].down,
                                 node_table[pattern].down,idx+1, proj+1, len-1);
            res = mdd_create_node(node_table[mdd].val, tmp, 0, COPY_DONT_CARE);
        }
    } else {
        while (mdd > 1) {
            mdd_push(res);
            tmp = mdd_copy_match(p_id, node_table[mdd].down, pattern, idx+1,
                                 proj, len);
            tmp = mdd_create_node(node_table[mdd].val, tmp, 0, COPY_DONT_CARE);
            mdd_push(tmp);
            res = mdd_union(res, tmp);
            mdd_pop(); mdd_pop();
            mdd =  node_table[mdd].right;
        }
    }

    cache_put(slot_hash,OP_COPY_MATCH, mdd_original, pattern, p_id, 0, res);
    return res;
}

static uint64_t
mdd_intersect(uint64_t a, uint64_t b)
{
    if (a == b) return a;
    if (a == 0 || b == 0) return 0;
    if (a == 1 || b == 1) Abort("missing case in intersect");

    uint64_t slot_hash = hash3(OP_INTERSECT, a, b);
    uint64_t slot = slot_hash % cache_size;

    if (op_cache[slot].op == OP_INTERSECT && op_cache[slot].arg1 == a
            && op_cache[slot].res.other.arg2 == b) {
        return op_cache[slot].res.other.res;
    }

    uint64_t tmp;

    if (node_table[a].val == node_table[b].val) {
        tmp = mdd_intersect(node_table[a].down, node_table[b].down);
        mdd_push(tmp);
        tmp = mdd_intersect(node_table[a].right, node_table[b].right);
        tmp = mdd_create_node(node_table[a].val, mdd_pop(), tmp, COPY_DONT_CARE);
    } else if (node_table[a].val < node_table[b].val) {
        tmp = mdd_intersect(node_table[a].right, b);
    } else { /* node_table[a].val > node_table[b].val */
        tmp = mdd_intersect(a, node_table[b].right);
    }

    cache_put(slot_hash,OP_INTERSECT, a, b, 0, 0, tmp);
    return tmp;
}

static void mdd_clear_and_write_bin(stream_t s, uint64_t mdd, uint64_t* n_count, map64_t node_map){
    if (mdd<=1) return;
    if (node_table[mdd].reachable) {
        node_table[mdd].reachable=0;
        mdd_clear_and_write_bin(s,node_table[mdd].down, n_count, node_map);
        mdd_clear_and_write_bin(s,node_table[mdd].right, n_count, node_map);
        simplemap64_put(node_map, mdd, (uint64_t)*n_count);
        DSwriteU64(s, (uint64_t)*n_count);
        DSwriteU32(s, node_table[mdd].val);
        DSwriteU64(s, simplemap64_get(node_map, node_table[mdd].down));
        DSwriteU64(s, simplemap64_get(node_map, node_table[mdd].right));
        DSwriteU32(s, (uint32_t)node_table[mdd].copy);
        (*n_count)++;
    }
}

static void
mdd_save_bin(FILE* f, uint64_t mdd)
{
    stream_t s = stream_output(f);
    uint64_t n_count = mdd_node_count(mdd);
    Print(infoLong,"mdd_save: %" PRIu64 " / %" PRIu64 " (%.0f%%)", n_count, mdd_nodes, 100*(((float)n_count)/(float)mdd_nodes));
    DSwriteU64(s, n_count);
    mdd_mark(mdd);
    map64_t node_map = simplemap64_create(n_count * 1.1 + 2);
    simplemap64_put(node_map, 0, 0);
    simplemap64_put(node_map, 1, 1);
    uint64_t count = 2;
    mdd_clear_and_write_bin(s, mdd, &count, node_map);
    simplemap64_destroy(node_map);
    stream_flush(s);
    RTfree(s); //stream_close(&s);
}

static uint64_t
mdd_load_bin(FILE* f)
{
    stream_t s = stream_input(f);
    uint64_t n_count;
    n_count = DSreadU64(s);
    if (n_count < 2)
        n_count = 2;
    if (mdd_load_node_ids != NULL)
        Abort("Error, mdd_load_node_ids already in use!");
    mdd_load_node_ids = RTmalloc(n_count*sizeof(uint64_t));
    mdd_load_node_ids[0] = 0;
    mdd_load_node_ids[1] = 1;
    mdd_load_node_count = 2;
    uint64_t mdd = 0;
    uint64_t id;
    uint32_t val;
    uint64_t down;
    uint64_t right;
    uint32_t copy;
    while (mdd_load_node_count < n_count && !stream_empty(s)) {
        id = DSreadU64(s);
        val = DSreadU32(s);
        down = DSreadU64(s);
        right = DSreadU64(s);
        copy = DSreadU32(s);
        if (mdd_load_node_count != id)
            Abort("Nodes have to be numbered consecutively from 2 till n-1.");
        //Warning(debug, "id=%llu, val=%u, down=%llu [%llu], right=%llu [%llu]",
        //        id, val, down, mdd_load_node_ids[down], right, mdd_load_node_ids[right]);
        assert(down==0 || mdd_load_node_ids[down]!=0);
        assert(right==0 || mdd_load_node_ids[right]!=0);
        mdd = mdd_create_node(val, mdd_load_node_ids[down], mdd_load_node_ids[right], copy);
        //Warning(debug, "id=%llu [%llu]", id, mdd);
        mdd_load_node_ids[id] = mdd;
        mdd_load_node_count++;
    }
    RTfree(mdd_load_node_ids);
    RTfree (s); //stream_close(&s);
    mdd_load_node_ids = NULL;
    mdd_load_node_count = 0;
    return mdd;
}

static vset_t
set_create_mdd(vdom_t dom, int k, int *proj)
{
    assert(k <= dom->shared.size);
    int l = (k < 0)?dom->shared.size:k;
    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set) + sizeof(int[l]));
    set->dom  = dom;
    set->mdd  = 0;
    set->next = protected_sets;
    set->prev = NULL;
    if (protected_sets != NULL) protected_sets->prev = set;
    protected_sets = set;
    set->p_len = l;
    set->p_id  = 1;
    for(int i = l - 1; i >= 0; i--) {
        set->proj[i] = k < 0 ? i : proj[i];
        set->p_id    = mdd_create_node(k < 0 ? i : proj[i], set->p_id, 0, COPY_DONT_CARE);
        // The p_id of a relation is shifted in hash keys; check this is ok
        if ((set->p_id >> 32) > 0) Abort("set_create_mdd: projection identifier too large");
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
rel_create_mdd_rw(vdom_t dom, int r_k, int *r_proj, int w_k, int *w_proj)
{

    assert(0 <= r_k && r_k <= dom->shared.size && 0 <= w_k && w_k <= dom->shared.size);

    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation) + sizeof(int[r_k]) + sizeof(int[w_k]));
    rel->dom  = dom;
    rel->mdd  = 0;
    rel->next = protected_rels;
    rel->prev = NULL;
    if (protected_rels != NULL) protected_rels->prev = rel;
    protected_rels = rel;
    rel->p_id  = 1;
    rel->r_p_len = r_k;
    rel->w_p_len = w_k;
    rel->r_proj = (int*) ((char*) rel + sizeof(struct vector_relation));
    rel->w_proj = (int*) ((char*) rel + sizeof(struct vector_relation) + sizeof(int[r_k]));

    int r = r_k-1;
    int w = w_k-1;
    while (r >= 0 || w >= 0) {

        if(w >= 0 && (r == -1 ||  w_proj[w] >= r_proj[r])) {
            rel->w_proj[w] = w_proj[w];
            rel->p_id = mdd_create_node(w_proj[w], rel->p_id, 1, COPY_DONT_CARE);
            // The p_id of a relation is shifted in hash keys; check this is ok
            if ((rel->p_id >> 32) > 0) Abort("rel_create_mdd: projection identifier too large");
            w--;
        }

        if(r >= 0 && (w == -1 || r_proj[r] >= w_proj[w])) {
           rel->r_proj[r] = r_proj[r];
           rel->p_id = mdd_create_node(r_proj[r], rel->p_id, 0, COPY_DONT_CARE);
           // The p_id of a relation is shifted in hash keys; check this is ok
           if ((rel->p_id >> 32) > 0) Abort("rel_create_mdd: projection identifier too large");
           r--;
        }
    }

    return rel;
}

static vrel_t
rel_create_mdd(vdom_t dom, int k, int *proj)
{
    return rel_create_mdd_rw(dom,k,proj,k,proj);
}

static void
rel_save_proj_bin(FILE* f, vrel_t rel)
{
    stream_t s = stream_output(f);

    DSwriteS32(s, rel->r_p_len);
    for(int i=0; i<rel->r_p_len; i++){
        DSwriteS32(s, rel->r_proj[i]);
    }
    DSwriteS32(s, rel->w_p_len);
    for(int i=0; i<rel->w_p_len; i++){
        DSwriteS32(s, rel->w_proj[i]);
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
    int r_p_len = DSreadS32(s);
    int r_proj[r_p_len];
    for(int i=0; i<r_p_len; i++){
        r_proj[i] = DSreadS32(s);
    }
    int w_p_len = DSreadS32(s);
    int w_proj[w_p_len];
    for(int i=0; i<w_p_len; i++){
        w_proj[i] = DSreadS32(s);
    }
    RTfree(s); //stream_close(&s);
    vrel_t result = rel_create_mdd_rw(dom, r_p_len, r_proj, w_p_len, w_proj);
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
    set->mdd = mdd_put(set->mdd, (uint32_t*)e, len, NULL, set_copy);
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
    assert(src->p_len == src->dom->shared.size && dst->p_len == dst->dom->shared.size && p_len >= 0);
    uint64_t singleton = mdd_put(0, (uint32_t*)match, p_len, NULL, set_copy);
    mdd_push(singleton);

    uint64_t p_id = mdd_put(0, (uint32_t*)proj, p_len, NULL, set_copy); // FIXME: enable to pass a projection to this function!
    //Warning(info, "set_copy_match_mdd: p_id=%d", p_id);
    // The p_id of is shifted in hash keys; check this is ok
    if ((p_id >> 32) > 0) Abort("set_copy_match_mdd: projection identifier too large");
    mdd_push(p_id);

    dst->mdd = mdd_copy_match(p_id, src->mdd, singleton, 0, proj, p_len);
    mdd_pop(); mdd_pop();
}

static void
set_copy_match_proj_mdd(vset_t dst, vset_t src, int p_len, int *proj, int p_id, int *match)
{
    assert(src->p_len == src->dom->shared.size && dst->p_len == dst->dom->shared.size && p_len >= 0);
    uint64_t singleton = mdd_put(0, (uint32_t*)match, p_len, NULL, set_copy);
    mdd_push(singleton);
    mdd_push(p_id);

    dst->mdd = mdd_copy_match(p_id, src->mdd, singleton, 0, proj, p_len);
    mdd_pop(); mdd_pop();
}

static int
proj_create_mdd(int p_len, int *proj)
{
    assert(p_len >= 0);
    uint64_t p_id = mdd_put(0, (uint32_t*)proj, p_len, NULL, set_copy); // FIXME: enable to pass a projection to this function!
    //Warning(info, "proj_create_mdd: p_id=%d", p_id);
    // The p_id of is shifted in hash keys; check this is ok
    if ((p_id >> 32) > 0) Abort("set_copy_match_mdd: projection identifier too large");
    return p_id;
}

static void
set_enum_match_mdd(vset_t set, int p_len, int *proj, int *match,
                       vset_element_cb cb, void *context)
{
    assert(set->p_len == set->dom->shared.size && p_len >= 0);
    uint64_t singleton = mdd_put(0, (uint32_t*)match, p_len, NULL, set_copy);
    mdd_push(singleton);

    uint64_t p_id = mdd_put(0, (uint32_t*)proj, p_len, NULL, set_copy);
    // The p_id of is shifted in hash keys; check this is ok
    if ((p_id >> 32) > 0) Abort("set_enum_match_mdd: projection identifier too large");
    mdd_push(p_id);

    uint64_t tmp = mdd_copy_match(p_id, set->mdd, singleton, 0, proj, p_len);
    mdd_pop(); mdd_pop();

    int len = set->dom->shared.size;
    uint32_t vec[len];
    mdd_enum(tmp, vec, 0, len , cb, context);
}

static void
rel_add_mdd_cpy(vrel_t rel, const int *src, const int *dst, const int *cpy) {

    uint32_t vec[rel->r_p_len + rel->w_p_len];
    uint32_t cpy_vec[rel->r_p_len + rel->w_p_len];

    int j=0;
    int r=0;
    int w=0;
    while (r < rel->r_p_len || w < rel->w_p_len) {

        int is_read = 0;

        if  (r < rel->r_p_len && (w == rel->w_p_len || rel->r_proj[r] <= rel->w_proj[w])) {
            vec[j] = src[r];
            cpy_vec[j] = COPY_DONT_CARE;
            j++;
            is_read = 1;
        }

        if (w < rel->w_p_len && (r == rel->r_p_len ||  rel->w_proj[w] <= rel->r_proj[r])) {
            if (is_read) {
                vec[j] = dst[w];
                cpy_vec[j] = COPY_DONT_CARE;
            } else {
                if (cpy != NULL && cpy[w]) {
                    // note here we use 0 as node value. It could be the case that any other value is used
                    // in mdd_put and mdd_create_node to achieve more sharing. The value does not matter when
                    // copying values.
                    vec[j] = 0;
                    cpy_vec[j] = COPY_COPY;
                } else {
                    vec[j] = dst[w];
                    cpy_vec[j] = COPY_WRITE;
                }
            }
            j++; w++;
        }

        if (is_read) {
            r++;
        }

    }

    rel->mdd = mdd_put(rel->mdd, vec, rel->r_p_len + rel->w_p_len, NULL, cpy_vec);
}

static void
rel_add_mdd(vrel_t rel, const int *src, const int *dst) {
    rel_add_mdd_cpy(rel,src,dst,NULL);
}

static uint64_t
mdd_project(uint64_t p_id, uint64_t mdd, int idx, int *proj, int len)
{
    if(mdd == 0) return 0; //projection of empty is empty.
    if(len == 0) return 1; //projection of non-empty is epsilon.

    uint64_t slot_hash=hash3(OP_PROJECT,mdd,p_id);
    uint64_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_PROJECT && op_cache[slot].arg1==mdd
       && op_cache[slot].res.other.arg2==p_id) {
        return op_cache[slot].res.other.res;
    }

    uint64_t res = 0;
    uint64_t mdd_original = mdd;

    if (proj[0]==idx){
        mdd_push(mdd_project(p_id,node_table[mdd].right,idx,proj,len));
        uint64_t tmp=mdd_project(node_table[p_id].down, node_table[mdd].down,
                                 idx+1, proj+1, len-1);
        res=mdd_create_node(node_table[mdd].val,tmp,mdd_pop(),COPY_DONT_CARE);
    } else {
        while(mdd>1){
            mdd_push(res);
            uint64_t tmp=mdd_project(p_id,node_table[mdd].down,idx+1,proj,len);
            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            mdd=node_table[mdd].right;
        }
    }
    cache_put(slot_hash, OP_PROJECT, mdd_original, p_id, 0, 0, res);
    return res;
}

static uint64_t
mdd_next(uint64_t p_id, uint64_t set, uint64_t rel, int idx, int *r_proj, int r_len, int* w_proj, int w_len)
{
    if (r_len < 0 || w_len < 0) Abort("Rel out of bounds");
    if (rel==0||set==0) return 0;
    if (w_len ==0 && r_len == 0) return set;
    if (rel==1||set==1)Abort("missing case in next; set: %" PRIu64 ", rel: %" PRIu64 " idx=%d, r_len=%d, w_len=%d", set, rel, idx, r_len, w_len);

    uint64_t slot_hash;
    uint64_t old_rel;
    uint64_t old_set;
    uint64_t slot;

    uint64_t res=0;

    if ((r_len > 0 && r_proj[0]==idx) && (w_len > 0 && w_proj[0] == idx)) { // +

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

        slot_hash=hash4(OP_NEXT,set,rel,p_id);
        slot=slot_hash%cache_size;
        if (op_cache[slot].op==OP_NEXT && op_cache[slot].arg1==set
            && op_cache[slot].res.other.arg2==rel
            && op_cache[slot].res.other.arg3==p_id) {
            return op_cache[slot].res.other.res;
        }

        old_rel=rel;
        old_set=set;

        res = mdd_next(p_id, node_table[set].right, node_table[rel].right, idx, r_proj, r_len, w_proj, w_len);

        rel=node_table[rel].down;
        p_id=node_table[p_id].down;

        while(rel>1){
            mdd_push(res);
            uint64_t tmp = mdd_next(node_table[p_id].down, node_table[set].down,
                                    node_table[rel].down, idx+1, r_proj+1, r_len-1, w_proj+1, w_len - 1);
            tmp=mdd_create_node(node_table[rel].val,tmp,0,COPY_DONT_CARE);
            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            rel=node_table[rel].right;
        }

    } else if (r_len > 0 && r_proj[0]==idx) { // r

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

        slot_hash=hash4(OP_NEXT,set,rel,p_id);
        slot=slot_hash%cache_size;
        if (op_cache[slot].op==OP_NEXT && op_cache[slot].arg1==set
            && op_cache[slot].res.other.arg2==rel
            && op_cache[slot].res.other.arg3==p_id) {
            return op_cache[slot].res.other.res;
        }
        old_rel=rel;
        old_set=set;

        res = mdd_next(p_id, node_table[set].right, node_table[rel].right, idx, r_proj, r_len, w_proj, w_len);

        mdd_push(res);
        uint64_t tmp = mdd_next(node_table[p_id].down, node_table[set].down,
                                node_table[rel].down, idx+1, r_proj+1, r_len-1, w_proj, w_len);
        res=mdd_create_node(node_table[set].val,tmp,mdd_pop(),COPY_DONT_CARE);

    } else if (w_len > 0 && w_proj[0] == idx) { // w

        slot_hash=hash4(OP_NEXT,set,rel,p_id);
        slot=slot_hash%cache_size;
        if (op_cache[slot].op==OP_NEXT && op_cache[slot].arg1==set
            && op_cache[slot].res.other.arg2==rel
            && op_cache[slot].res.other.arg3==p_id) {
            return op_cache[slot].res.other.res;
        }
        old_rel=rel;
        old_set=set;

        res = mdd_next(p_id, node_table[set].right, rel, idx, r_proj, r_len, w_proj, w_len);

        while(rel>1){
            mdd_push(res);
            uint64_t tmp = mdd_next(node_table[p_id].down, node_table[set].down,
                                    node_table[rel].down, idx+1, r_proj, r_len, w_proj+1, w_len - 1);
            tmp=mdd_create_node(node_table[rel].copy == COPY_COPY ? node_table[set].val : node_table[rel].val,tmp,0,COPY_DONT_CARE);

            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            rel=node_table[rel].right;

        }

    } else { // -

        slot_hash=hash4(OP_NEXT,set,rel,p_id);
        slot=slot_hash%cache_size;
        if (op_cache[slot].op==OP_NEXT && op_cache[slot].arg1==set
            && op_cache[slot].res.other.arg2==rel
            && op_cache[slot].res.other.arg3==p_id) {
            return op_cache[slot].res.other.res;
        }
        old_rel=rel;
        old_set=set;

        mdd_push(mdd_next(p_id,node_table[set].right,rel,idx,r_proj,r_len, w_proj, w_len));
        res=mdd_next(p_id,node_table[set].down,rel,idx+1,r_proj,r_len, w_proj, w_len);
        res=mdd_create_node(node_table[set].val,res,mdd_pop(),COPY_DONT_CARE);
    }

    cache_put(slot_hash, OP_NEXT, old_set, old_rel, p_id, 0, res);
    return res;
}

static void
set_project_mdd(vset_t dst, vset_t src)
{
    if (src->p_len != src->dom->shared.size || dst->p_len == dst->dom->shared.size) {
        dst->mdd = src->mdd;
    } else {
        dst->mdd = 0;
        dst->mdd = mdd_project(dst->p_id, src->mdd, 0, dst->proj, dst->p_len);
    }
}

static void
set_next_mdd(vset_t dst, vset_t src, vrel_t rel)
{
    assert(src->p_len == src->dom->shared.size && dst->p_len == dst->dom->shared.size);
    dst->mdd = mdd_next(rel->p_id, src->mdd, rel->mdd, 0, rel->r_proj, rel->r_p_len, rel->w_proj, rel->w_p_len);
}

static void
set_example_mdd(vset_t set, int *e)
{
    int len = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    uint64_t mdd = set->mdd;

    if (mdd == 0) Abort("empty set");

    for(int i = 0; i < len; i++){
        if (mdd == 1) Abort("non-uniform length");
        e[i] = node_table[mdd].val;
        mdd  = node_table[mdd].down;
    }

    if (mdd != 1) Abort("non-uniform length");
}

static void
set_random_mdd(vset_t set, int *e)
{
    int len = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    //printf("set_random_mdd: len=%d\n", len);
    uint64_t mdd = set->mdd;

    if (mdd == 0) Abort("empty set");

    for(int i = 0; i < len; i++){
        if (mdd == 1) Abort("non-uniform length");
        int j = 0;
        for(uint64_t right = node_table[mdd].right; right != 0; right = node_table[right].right) { j++; }
        int r = rand() % (j+1);
        //printf("%d: %d/%d\n", i, r, j+1);
        for(j=0; j < r; j++) { /*printf("%d: %d\n", i, node_table[mdd].val);*/ mdd = node_table[mdd].right; }
        e[i] = node_table[mdd].val;
        //printf("%d: %d\n", i, e[i]);
        mdd  = node_table[mdd].down;
    }

    if (mdd != 1) Abort("non-uniform length");
}

static uint64_t
mdd_prev_write(uint64_t p_id, uint64_t set, uint64_t rel, uint64_t univ, uint64_t val, int idx, int *r_proj, int r_len, int *w_proj, int w_len);

static uint64_t
mdd_prev_copy(uint64_t p_id, uint64_t set, uint64_t rel, uint64_t univ, int idx, int *r_proj, int r_len, int *w_proj, int w_len);

static uint64_t
mdd_prev(uint64_t p_id, uint64_t set, uint64_t rel, uint64_t univ, int idx, int *r_proj, int r_len, int *w_proj, int w_len)
{
    if (r_len < 0 || w_len < 0) Abort("rel out of bounds");
    if (set == 0 || rel == 0 || univ == 0) return 0;
    if (r_len==0 && w_len==0) return mdd_intersect(univ, set);
    if (rel==1 || set==1 || univ==1) Abort("missing case in prev; set: %" PRIu64 ", rel: %" PRIu64 ", univ: %" PRIu64 "", set, rel, univ);

    uint64_t slot_hash, slot, res;

    if ((r_len > 0 && r_proj[0] == idx) && (w_len > 0 && w_proj[0] == idx)) { // +

        while(node_table[rel].val!=node_table[univ].val){
            if(node_table[rel].val < node_table[univ].val) {
                rel=node_table[rel].right;
                if (rel<=1) return 0;
            }
            if(node_table[univ].val < node_table[rel].val) {
                univ=node_table[univ].right;
                if (univ<=1) return 0;
            }
        }

        slot_hash=hash5(OP_PREV,set,rel,univ,p_id);
        slot=slot_hash%cache_size;
        if (op_cache[slot].op==OP_PREV && op_cache[slot].arg1==set
            && op_cache[slot].res.other.arg2==rel
            && op_cache[slot].res.other.arg3==univ
            && op_cache[slot].res.other.arg4==p_id) {
            return op_cache[slot].res.other.res;
        }

        mdd_push(mdd_prev(p_id, set, node_table[rel].right, node_table[univ].right, idx, r_proj, r_len, w_proj, w_len));
        res = mdd_prev_write(node_table[p_id].down, set, node_table[rel].down, univ, node_table[rel].val, idx, r_proj+1, r_len-1, w_proj+1, w_len-1);
        res = mdd_union(res, mdd_pop());

        cache_put(slot_hash, OP_PREV, set, rel, univ, p_id, res);

    } else if (r_len > 0 && r_proj[0] == idx) { // r

        while(node_table[rel].val!=node_table[set].val){
            if(node_table[rel].val < node_table[set].val) {
                rel=node_table[rel].right;
                if (rel<=1) return 0;
            }
            if(node_table[set].val < node_table[rel].val) {
                set=node_table[set].right;
                if (set<=1) return 0;
            }
        }

        while(node_table[rel].val!=node_table[univ].val){
            if(node_table[rel].val < node_table[univ].val) {
                rel=node_table[rel].right;
                if (rel<=1) return 0;
            }
            if(node_table[univ].val < node_table[rel].val) {
                univ=node_table[univ].right;
                if (univ<=1) return 0;
            }
        }

        slot_hash=hash5(OP_PREV,set,rel,univ,p_id);
        slot=slot_hash%cache_size;
        if (op_cache[slot].op==OP_PREV && op_cache[slot].arg1==set
            && op_cache[slot].res.other.arg2==rel
            && op_cache[slot].res.other.arg3==univ
            && op_cache[slot].res.other.arg4==p_id) {
            return op_cache[slot].res.other.res;
        }

        mdd_push(mdd_prev(p_id, node_table[set].right, node_table[rel].right, node_table[univ].right, idx, r_proj, r_len, w_proj, w_len));
        res = mdd_create_node(
                node_table[set].val,
                mdd_prev(node_table[p_id].down, node_table[set].down, node_table[rel].down, node_table[univ].down, idx+1, r_proj+1, r_len-1, w_proj, w_len),
                mdd_pop(),
                COPY_DONT_CARE);

        cache_put(slot_hash, OP_PREV, set, rel, univ, p_id, res);

    } else if(w_len > 0 && w_proj[0] == idx) { // w

        slot_hash=hash5(OP_PREV,set,rel,univ,p_id);
        slot=slot_hash%cache_size;
        if (op_cache[slot].op==OP_PREV && op_cache[slot].arg1==set
            && op_cache[slot].res.other.arg2==rel
            && op_cache[slot].res.other.arg3==univ
            && op_cache[slot].res.other.arg4==p_id) {
            return op_cache[slot].res.other.res;
        }

        if (node_table[rel].copy == COPY_COPY) {
            mdd_push(mdd_prev(p_id, set, node_table[rel].right, univ, idx, r_proj, r_len, w_proj, w_len));
            res = mdd_prev_copy(node_table[p_id].down, set, node_table[rel].down, univ, idx, r_proj, r_len, w_proj+1, w_len-1);
            res = mdd_union(res, mdd_pop());

        } else {
            mdd_push(mdd_prev(p_id, set, rel, node_table[univ].right, idx, r_proj, r_len, w_proj, w_len));
            res = mdd_prev_write(p_id, set, rel, univ, node_table[univ].val, idx, r_proj, r_len, w_proj+1, w_len-1);
            res = mdd_union(res, mdd_pop());
        }

        cache_put(slot_hash, OP_PREV, set, rel, univ, p_id, res);

    } else { // -
        res = mdd_prev_copy(p_id, set, rel, univ, idx, r_proj, r_len, w_proj, w_len);
    }

    return res;
}

static uint64_t
mdd_prev_write(uint64_t p_id, uint64_t set, uint64_t rel, uint64_t univ, uint64_t val, int idx, int *r_proj, int r_len, int *w_proj, int w_len)
{
    if (r_len < 0 || w_len < 0) Abort("rel out of bounds");
    if (set == 0 || rel == 0) return 0;
    if (rel==1 || set==1) Abort("missing case in prev_write; set: %" PRIu64 ", rel: %" PRIu64 ", univ: %" PRIu64 "", set, rel, univ);

    uint64_t res = 0;

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
        uint64_t tmp=mdd_prev(node_table[p_id].down, node_table[set].down, node_table[rel].down, node_table[univ].down, idx+1, r_proj, r_len, w_proj, w_len);
        res=mdd_union(tmp, mdd_pop());

        rel=node_table[rel].right;
        set=node_table[set].right;
    }
    return mdd_create_node(val,res,0,COPY_DONT_CARE);
}

static uint64_t
mdd_prev_copy(uint64_t p_id, uint64_t set, uint64_t rel, uint64_t univ, int idx, int *r_proj, int r_len, int *w_proj, int w_len) {

    if (set == 0 || univ == 0) return 0;

    uint64_t res;

    while(node_table[set].val!=node_table[univ].val){
        if(node_table[set].val < node_table[univ].val) {
            set=node_table[set].right;
            if (set<=1) return 0;
        }
        if(node_table[univ].val < node_table[set].val) {
            univ=node_table[univ].right;
            if (univ<=1) return 0;
        }
    }

    uint64_t slot_hash=hash5(OP_PREV,set,rel,univ,p_id);
    uint64_t slot=slot_hash%cache_size;
    if (op_cache[slot].op==OP_PREV && op_cache[slot].arg1==set
        && op_cache[slot].res.other.arg2==rel
        && op_cache[slot].res.other.arg3==univ
        && op_cache[slot].res.other.arg4==p_id) {
        return op_cache[slot].res.other.res;
    }

    mdd_push(mdd_prev_copy(p_id, node_table[set].right, rel, node_table[univ].right, idx, r_proj, r_len, w_proj, w_len));
    res = mdd_prev(p_id, node_table[set].down, rel, node_table[univ].down, idx+1, r_proj, r_len, w_proj, w_len);
    res = mdd_create_node(node_table[set].val, res, mdd_pop(), COPY_DONT_CARE);

    cache_put(slot_hash, OP_PREV, set, rel, univ, p_id, res);

    return res;
}

static void
set_prev_mdd(vset_t dst, vset_t src, vrel_t rel, vset_t univ)
{
    assert(src->p_len == src->dom->shared.size && dst->p_len == dst->dom->shared.size && src->p_id == univ->p_id && src->p_len == univ->p_len);

    dst->mdd = mdd_prev(rel->p_id,src->mdd, rel->mdd, univ->mdd, 0, rel->r_proj,rel->r_p_len, rel->w_proj, rel->w_p_len);
}

static uint64_t
mdd_universe(uint64_t p_id, uint64_t dst, uint64_t src, int n) {

    if (src == 0) return 0;

    uint64_t slot_hash=hash3(OP_UNIVERSE,src,p_id);
    uint64_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_UNIVERSE && op_cache[slot].arg1==src
       && op_cache[slot].res.other.arg2==p_id) {
        return op_cache[slot].res.other.res;
    }

    mdd_push(mdd_universe(p_id, dst, node_table[src].right, n));

    if (n == 0) {
        dst = mdd_create_node(node_table[src].val, dst, 0, COPY_DONT_CARE);
    } else {
        dst = mdd_universe(p_id, dst, node_table[src].down, n-1);
    }

    uint64_t res = mdd_union(dst, mdd_pop());

    cache_put(slot_hash, OP_UNIVERSE, src, p_id, 0, 0, res);

    return res;

}

static void
set_universe_mdd(vset_t dst, vset_t src) {

    assert(src->p_len == src->dom->shared.size && dst->mdd == 0);

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
static uint64_t rels_tot;
static top_groups_info *top_groups;

static uint64_t saturate(int level, uint64_t mdd);
static uint64_t sat_rel_prod(uint64_t p_id, uint64_t set, uint64_t rel,
                             int idx, int *r_proj, int r_len, int *w_proj, int w_len);

static uint64_t
copy_level_sat(uint64_t p_id, uint64_t set, uint64_t rel, int idx,
               int *r_proj, int r_len, int *w_proj, int w_len)
{
    uint64_t res = 0;

    while (set > 0) {
        mdd_push(res);
        uint64_t tmp = sat_rel_prod(p_id, node_table[set].down, rel, idx + 1,
                                    r_proj, r_len, w_proj, w_len);
        tmp = mdd_create_node(node_table[set].val, tmp, 0, COPY_DONT_CARE);
        mdd_push(tmp);
        res = mdd_union(res, tmp);
        mdd_pop(); mdd_pop();
        set = node_table[set].right;
    }

    return res;
}

static uint64_t
apply_rel_prod(uint64_t p_id, uint64_t set, uint64_t rel, int idx,
               int *r_proj, int r_len, int *w_proj, int w_len)
{
    uint64_t res = 0;

    if (r_len > 0 && r_proj[0]==idx && w_len > 0 && w_proj[0] == idx) { // +

        while (set > 1 && rel > 1) {
            if (node_table[set].val < node_table[rel].val)
                set = node_table[set].right;
            else if (node_table[rel].val < node_table[set].val)
                rel = node_table[rel].right;
            else {
                uint64_t rel_down = node_table[rel].down;

                while (rel_down > 1) {
                    mdd_push(res);
                    uint64_t tmp = sat_rel_prod(node_table[node_table[p_id].down].down,
                                                node_table[set].down,
                                                node_table[rel_down].down,
                                                idx + 1, r_proj + 1, r_len - 1, w_proj+1, w_len-1);
                    tmp = mdd_create_node(node_table[rel_down].val, tmp, 0, COPY_DONT_CARE);
                    mdd_push(tmp);
                    res = mdd_union(res, tmp);
                    mdd_pop(); mdd_pop();
                    rel_down = node_table[rel_down].right;
                }

                set = node_table[set].right;
                rel = node_table[rel].right;
            }
        }

    } else if (w_len > 0 && w_proj[0] == idx) { // w

        uint64_t old_rel = rel;

        while (set > 1) {
            while (rel > 1) {
                mdd_push(res);
                uint64_t tmp = sat_rel_prod(node_table[p_id].down,
                                            node_table[set].down,
                                            node_table[rel].down,
                                            idx + 1, r_proj, r_len, w_proj+1, w_len-1);
                tmp = mdd_create_node(
                        node_table[rel].copy == COPY_COPY ? node_table[set].val : node_table[rel].val,
                        tmp, 0, COPY_DONT_CARE);
                mdd_push(tmp);
                res = mdd_union(res, tmp);
                mdd_pop(); mdd_pop();
                rel = node_table[rel].right;
            }

            set = node_table[set].right;
            rel = old_rel;

        }

    } else { // r

        while (set > 1 && rel > 1) {
            if (node_table[set].val < node_table[rel].val)
                set = node_table[set].right;
            else if (node_table[rel].val < node_table[set].val)
                rel = node_table[rel].right;
            else {

                mdd_push(res);
                uint64_t tmp = sat_rel_prod(node_table[p_id].down,
                                            node_table[set].down,
                                            node_table[rel].down,
                                            idx + 1, r_proj + 1, r_len - 1, w_proj, w_len);
                tmp = mdd_create_node(node_table[rel].val, tmp, 0, COPY_DONT_CARE);
                mdd_push(tmp);
                res = mdd_union(res, tmp);
                mdd_pop(); mdd_pop();

                set = node_table[set].right;
                rel = node_table[rel].right;

            }
        }

    }

    return res;
}

static uint64_t
sat_rel_prod(uint64_t p_id, uint64_t set, uint64_t rel, int idx,
                int *r_proj, int r_len, int *w_proj, int w_len)
{
    if (r_len == 0 && w_len == 0) return set;
    if (set == 0 || rel == 0) return 0;
    if (set == 1 || rel == 1) Abort("missing case in set_reach_sat %d, %d, %d, %d", r_proj[0], r_len, w_proj[0], w_len);

    uint64_t slot_hash = hash4(OP_RELPROD, set, rel, p_id);
    uint64_t slot = slot_hash % cache_size;

    if (op_cache[slot].op == OP_RELPROD
          && op_cache[slot].arg1 == set
          && op_cache[slot].res.other.arg2 == rel
          && op_cache[slot].res.other.arg3==p_id)
        return op_cache[slot].res.other.res;

    uint64_t res = 0;

    if ((r_len > 0 && r_proj[0]==idx) || (w_len > 0 && w_proj[0] == idx))
        res = apply_rel_prod(p_id, set, rel, idx, r_proj, r_len, w_proj, w_len);
    else
        res = copy_level_sat(p_id, set, rel, idx, r_proj, r_len, w_proj, w_len);

    mdd_push(res);
    res = saturate(idx, res);
    mdd_pop();

    cache_put(slot_hash, OP_RELPROD, set, rel, p_id, 0, res);
    return res;
}

static uint64_t
apply_rel_fixpoint(uint64_t p_id, uint64_t set, uint64_t rel, int idx,
                   int *r_proj, int r_len, int *w_proj, int w_len)
{
    uint64_t res = set;

    if (r_len > 0 && r_proj[0]==idx && w_len > 0 && w_proj[0] == idx) { // +

        while (set > 1 && rel > 1) {
            if (node_table[set].val < node_table[rel].val)
                set = node_table[set].right;
            else if (node_table[rel].val < node_table[set].val)
                rel = node_table[rel].right;
            else {
                uint64_t new_res = res;
                uint64_t rel_down = node_table[rel].down;

                while (node_table[rel].val != node_table[new_res].val)
                    new_res = node_table[new_res].right;

                while (rel_down > 1) {
                    mdd_push(res);
                    uint64_t tmp = sat_rel_prod(node_table[node_table[p_id].down].down,
                                                node_table[new_res].down,
                                                node_table[rel_down].down,
                                                idx + 1, r_proj + 1, r_len - 1, w_proj+1, w_len-1);
                    tmp = mdd_create_node(node_table[rel_down].val, tmp, 0, COPY_DONT_CARE);
                    mdd_push(tmp);
                    res = mdd_union(res, tmp);
                    mdd_pop(); mdd_pop();
                    rel_down = node_table[rel_down].right;
                }

                set = node_table[set].right;
                rel = node_table[rel].right;
            }
        }

    } else if (w_len > 0 && w_proj[0] == idx) { // w

        uint64_t old_rel = rel;

        while (set > 1) {

            while (rel > 1) {
                mdd_push(res);
                uint64_t tmp = sat_rel_prod(node_table[p_id].down,
                                            node_table[set].down,
                                            node_table[rel].down,
                                            idx + 1, r_proj, r_len, w_proj+1, w_len-1);
                tmp = mdd_create_node(
                        node_table[rel].copy == COPY_COPY ? node_table[set].val : node_table[rel].val,
                        tmp, 0, COPY_DONT_CARE);
                mdd_push(tmp);
                res = mdd_union(res, tmp);
                mdd_pop(); mdd_pop();
                rel = node_table[rel].right;
            }

            set = node_table[set].right;
            rel = old_rel;

        }

    } else { // r

        while (set > 1 && rel > 1) {
            if (node_table[set].val < node_table[rel].val)
                set = node_table[set].right;
            else if (node_table[rel].val < node_table[set].val)
                rel = node_table[rel].right;
            else {
                uint64_t new_res = res;

                while (node_table[rel].val != node_table[new_res].val)
                    new_res = node_table[new_res].right;

                mdd_push(res);
                uint64_t tmp = sat_rel_prod(node_table[p_id].down,
                                            node_table[new_res].down,
                                            node_table[rel].down,
                                            idx + 1, r_proj + 1, r_len - 1, w_proj, w_len);
                tmp = mdd_create_node(node_table[rel].val, tmp, 0, COPY_DONT_CARE);
                mdd_push(tmp);
                res = mdd_union(res, tmp);
                mdd_pop(); mdd_pop();

                set = node_table[set].right;
                rel = node_table[rel].right;

            }

        }

    }

    return res;
}

// Start fixpoint calculations on the MDD at a given level for transition groups
// whose top is at that level. Continue performing fixpoint calculations until
// the MDD does not change anymore.
static uint64_t
sat_fixpoint(int level, uint64_t set)
{
    if (set == 0) return 0;
    if (set == 1) Abort("missing case in sat_fixpoint");

    top_groups_info groups_info = top_groups[level];
    uint64_t new_set = set;

    mdd_push(0);

    while (new_set != mdd_pop()) {
        mdd_push(new_set);
        for (int i = 0; i < groups_info.tg_len; i++) {
            int grp = groups_info.top_groups[i];
            mdd_push(new_set);
            assert(rel_set[grp]->r_p_len != 0 || rel_set[grp]->w_p_len != 0);

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
                                         rel_set[grp]->r_proj,
                                         rel_set[grp]->r_p_len,
                                         rel_set[grp]->w_proj,
                                         rel_set[grp]->w_p_len);
            mdd_pop();
        }
    }

    return new_set;
}

// Saturation process for the MDD at a given level
static uint64_t
saturate(int idx, uint64_t mdd)
{
    if (mdd == 0 || mdd == 1) return mdd;

    uint64_t slot_hash = hash3(OP_SAT, mdd, rels_tot);
    uint64_t slot = slot_hash % cache_size;

    if (op_cache[slot].op == OP_SAT
          && op_cache[slot].arg1 == mdd
          && op_cache[slot].res.other.arg2 == rels_tot)
        return op_cache[slot].res.other.res;

    uint64_t res = 0;
    uint64_t mdd_right = mdd;

    while (mdd_right > 1) {
        mdd_push(res);
        uint64_t tmp = saturate(idx + 1, node_table[mdd_right].down);
        tmp = mdd_create_node(node_table[mdd_right].val, tmp, 0, COPY_DONT_CARE);
        mdd_push(tmp);
        res = mdd_union(res, tmp);
        mdd_pop(); mdd_pop();
        mdd_right = node_table[mdd_right].right;
    }

    mdd_push(res);
    res = sat_fixpoint(idx, res);
    mdd_pop();


    cache_put(slot_hash, OP_SAT, mdd, rels_tot, 0, 0, res);
    return res;
}

// Perform fixpoint calculations using the "General Basic Saturation" algorithm
static void
set_least_fixpoint_mdd(vset_t dst, vset_t src, vrel_t rels[], int rel_count)
{
    // Only implemented if not projected
    assert(src->p_len == src->dom->shared.size && dst->p_len == dst->dom->shared.size);

    // Initialize partitioned transition relations.
    rel_set = rels;

    uint64_t rels_tmp = 0;

    for (int i = 0; i < rel_count; i++)
        rels_tmp = mdd_create_node(rel_count - i, rels[i]->mdd, rels_tmp, COPY_DONT_CARE);

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
        proj_set[grp] = set_create_mdd(rels[grp]->dom, rels[grp]->r_p_len,
                                       rels[grp]->r_proj);

        if (rels[grp]->r_p_len == 0 && rels[grp]->w_p_len == 0) continue;

        int top_lvl; // = minimum of rels[grp]->r_proj[0] and rels[grp]->w_proj[0]
        if (rels[grp]->r_p_len == 0) top_lvl = rels[grp]->w_proj[0];
        else if (rels[grp]->w_p_len == 0) top_lvl = rels[grp]->r_proj[0];
        else top_lvl = min(rels[grp]->r_proj[0], rels[grp]->w_proj[0]);

        top_groups[top_lvl].top_groups[top_groups[top_lvl].tg_len] = grp;
        top_groups[top_lvl].tg_len++;
    }

    // Saturation on initial state set
    dst->mdd = saturate(0, src->mdd);

    // Clean-up
    for (int grp = 0; grp < rel_count; grp++) {
        if (rels[grp]->r_p_len == 0 && rels[grp]->expand != NULL) {
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

static void mdd_mark_for_dot(uint64_t mdd){
  // only heads of mdd-nodes are marked
  // note that head of one mdd-node might be in the middle of another one!
  // this means that "sharing within mdd-nodes" is not represented in dot.
    if (mdd<=1) return;
    if (node_table[mdd].reachable) return;
    node_table[mdd].reachable=1;
    uint64_t x = mdd;
    while (x) {
      mdd_mark_for_dot(node_table[x].down);
      x=node_table[x].right;
    }
}

static char trueprinted;

static void mdd_clear_and_print(FILE* fp,uint64_t mdd){
  if (mdd==0) {fprintf(fp," n0 [shape=record,label=\"<f0> False\"]\n");}
  else if (mdd==1) {
    if (!trueprinted) {
      fprintf(fp," n1 [shape=record,label=\"<f0> True\"]\n");
      trueprinted=1;
    }
  }
  else if (node_table[mdd].reachable) {
    node_table[mdd].reachable=0;

    // print the mdd-node with values
    uint64_t x=mdd;
    int i=0;
    fprintf(fp," n%" PRIu64 " [shape=record,label=\"",mdd);
    while (x) {
      if (i>0) fprintf(fp,"|");

      char *c = "";
      if (node_table[x].copy == COPY_WRITE) c = ", w";
      else if(node_table[x].copy == COPY_COPY) c = ", c";
      else if(node_table[x].copy == COPY_DONT_CARE) c = "";

      fprintf(fp,"<f%d> %"PRIu32" (%" PRIu64 "%s)", i, node_table[x].val, x, c);
      x=node_table[x].right;
      i++;
    }
    fprintf(fp,"\"];\n");

    // print the edges from this node
    x=mdd;
    i=0;
    while (x) {
      fprintf(fp,"   n%" PRIu64 ":f%d -> n%" PRIu64 ":f0;\n",mdd,i,node_table[x].down);
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

static void set_dot_name (FILE* fp, vset_t set)
{
    fprintf(fp, "node [shape=oval];\n");

    if (set->p_len < 0) {

        for (int i = 0; i < set->dom->shared.size; i++) {

            fprintf(fp, "l%d [label=\"%s\"];\n", i, vdom_get_name(set->dom, i));

            if (i > 0 && i < set->dom->shared.size) {
                fprintf(fp, "l%d -> l%d[style=invis];\n", i-1, i);
            }
        }

    } else {

        for (int i = 0; i < set->p_len; i++) {

            fprintf(fp, "l%d [label=\"%s\"];\n", i, vdom_get_name(set->dom, set->proj[i]));

            if (i > 0 && i < set->p_len) {
                fprintf(fp, "l%d -> l%d[style=invis];\n", i-1, i);
            }
        }
    }
}

static void rel_dot_name (FILE *fp, vrel_t rel)
{
    fprintf(fp, "node [shape=oval];\n");

    int to_read = 0;
    int from_read = 0;

    int i=0;
    int r=0;
    int w=0;
    while (r < rel->r_p_len || w < rel->w_p_len) {

        int read = w >= rel->w_p_len || (r < rel->r_p_len && w < rel->w_p_len && rel->r_proj[r] <= rel->w_proj[w]);
        int write = r >= rel->r_p_len || (r < rel->r_p_len && w < rel->w_p_len && rel->w_proj[w] <= rel->r_proj[r]);

        int proj = -1;
        char *bg_color = NULL;
        if (read && write) {
            bg_color = "grey75";
            proj = rel->r_proj[r];
        } else if(read) {
            bg_color = "white";
            proj = rel->r_proj[r];
        } else if(write) {
            bg_color = "grey45";
            proj = rel->w_proj[w];
        }

        char *var_name = vdom_get_name(rel->dom, proj);
        if (read) fprintf(fp, "l%d [style=filled, label=\"%s\", fillcolor=%s];\n", i, var_name, bg_color);
        if (write) fprintf(fp, "ln%d [style=filled, label=\"%s'\", fillcolor=%s];\n", i, var_name, bg_color);
        if (read && write) fprintf(fp, "l%d -> ln%d[style=invis];\n", i, i);

        if (write) to_read = 0;
        if (read) to_read = 1;

        if (i > 0)
            fprintf(fp, "l%s%d -> l%s%d[style=invis];\n", from_read ? "" : "n", i-1, to_read ? "" : "n", i);

        if (read) {
            from_read = 1;
            r++;
        }
        if (write) {
            from_read = 0;
            w++;
        }
        i++;
    }
}

static void set_dot_mdd(FILE* fp, vset_t src) {
  uint64_t mdd = src->mdd;
  fprintf(fp,"digraph setbdd {\n");
  set_dot_name(fp, src);
  fprintf(fp, "node [shape=record];\n");
  mdd_mark_for_dot(mdd);
  trueprinted=0;
  mdd_clear_and_print(fp,mdd);
  fprintf(fp,"}\n");
}

static void rel_dot_mdd(FILE* fp, vrel_t src) {
  uint64_t mdd = src->mdd;
  fprintf(fp,"digraph relbdd {\n");
  rel_dot_name(fp, src);
  fprintf(fp, "node [shape=record];\n");
  mdd_mark_for_dot(mdd); // this changes mdd_used; does it matter?
  trueprinted=0;
  mdd_clear_and_print(fp,mdd);
  fprintf(fp,"}\n");
}

static uint64_t
mdd_join(uint64_t a_pid, uint64_t b_pid, uint64_t a, uint64_t b, int a_p_len, int* a_proj, int b_p_len, int* b_proj)
{
    if (a_p_len < 0 || b_p_len < 0) Abort("missing projection information");
    if (a == 0 || b == 0)  return 0;
    if (a_p_len == 0) return b;
    if (b_p_len == 0) return a;

    uint64_t res;
    uint64_t slot_hash;
    uint64_t slot;

    if (a_proj[0] == b_proj[0]) {

        if (a_pid == b_pid) return mdd_intersect(a, b);

        uint64_t old_a=a;
        uint64_t old_b=b;

        while(node_table[a].val!=node_table[b].val){
            if(node_table[a].val < node_table[b].val) {
              a=node_table[a].right;
              if (a<=1) return 0;
            }
            if(node_table[b].val < node_table[a].val) {
              b=node_table[b].right;
              if (b<=1) return 0;
            }
        }

        slot_hash = hash5(OP_JOIN, a, b, a_pid, b_pid);
        slot = slot_hash % cache_size;

        if (op_cache[slot].op == OP_JOIN && op_cache[slot].arg1 == a
                && op_cache[slot].res.other.arg2 == b
                && op_cache[slot].res.other.arg3 == a_pid
                && op_cache[slot].res.other.arg4 == b_pid) {
            return op_cache[slot].res.other.res;
        }

        res = mdd_join(a_pid, b_pid, node_table[a].right, node_table[b].right, a_p_len, a_proj, b_p_len, b_proj);
        mdd_push(res);
        res = mdd_join(node_table[a_pid].down, node_table[b_pid].down, node_table[a].down, node_table[b].down, a_p_len-1, a_proj+1, b_p_len-1, b_proj+1);
        res = mdd_create_node(node_table[b].val, res, mdd_pop(), COPY_DONT_CARE);

        cache_put(slot_hash, OP_JOIN, old_a, old_b, a_pid, b_pid, res);

    } else {

        slot_hash = hash5(OP_JOIN, a, b, a_pid, b_pid);
        slot = slot_hash % cache_size;

        if (op_cache[slot].op == OP_JOIN && op_cache[slot].arg1 == a
                && op_cache[slot].res.other.arg2 == b
                && op_cache[slot].res.other.arg3 == a_pid
                && op_cache[slot].res.other.arg4 == b_pid) {
            return op_cache[slot].res.other.res;
        }

        if(a_proj[0] > b_proj[0]) {
            res = mdd_join(a_pid, b_pid, a, node_table[b].right, a_p_len, a_proj, b_p_len, b_proj);
            mdd_push(res);
            res = mdd_join(a_pid, node_table[b_pid].down, a, node_table[b].down, a_p_len, a_proj, b_p_len-1, b_proj+1);
            res = mdd_create_node(node_table[b].val, res, mdd_pop(), COPY_DONT_CARE);
        } else { //b_proj[0] > a_proj[0]
            res = mdd_join(a_pid, b_pid, node_table[a].right, b, a_p_len, a_proj, b_p_len, b_proj);
            mdd_push(res);
            res = mdd_join(node_table[a_pid].down, b_pid, node_table[a].down, b, a_p_len-1, a_proj+1, b_p_len, b_proj);
            res = mdd_create_node(node_table[a].val, res, mdd_pop(), COPY_DONT_CARE);
        }

        cache_put(slot_hash, OP_JOIN, a, b, a_pid, b_pid, res);
    }

    return res;

}


static void
set_join_mdd(vset_t dst, vset_t left, vset_t right)
{
    dst->mdd = mdd_join(
            left->p_id, right->p_id,
            left->mdd,right->mdd,
            left->p_len, left->proj,
            right->p_len, right->proj);
}

static int separates_rw() { return 1; }

static long double mdd_ccount(uint32_t mdd){
    if (mdd<=1) return mdd;
    uint32_t slot1=hash3(OP_CCOUNT1,mdd,0)%cache_size;
    uint32_t slot2=hash3(OP_CCOUNT2,mdd,0)%cache_size;

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
        res.s.p1=op_cache[slot1].res.other.arg2;
        res.s.p2=op_cache[slot2].res.other.arg3;
        return res.count;
    }
    res.count=mdd_ccount(node_table[mdd].down);
    res.count+=mdd_ccount(node_table[mdd].right);
    op_cache[slot1].op=OP_CCOUNT1;
    op_cache[slot1].arg1=mdd;
    op_cache[slot1].res.other.arg2=res.s.p1;
    op_cache[slot2].op=OP_CCOUNT2;
    op_cache[slot2].arg1=mdd;
    op_cache[slot2].res.other.arg3=res.s.p2;
    return res.count;
}

static void
set_ccount_mdd(vset_t set, long *nodes, long double *elements)
{
    if (nodes != NULL) *nodes = mdd_node_count(set->mdd);
    if (elements != NULL) *elements = mdd_ccount(set->mdd);
}

vdom_t vdom_create_list64_native(int n){
    Warning(info,"Creating a native ListDD 64-bit domain.");
    vdom_t dom=(vdom_t)RTmalloc(sizeof(struct vector_domain));
    vdom_init_shared(dom,n);
    if (unique_table==NULL) {
        mdd_nodes=(nodes_fib <= FIB_MAX)?fib(nodes_fib):UINT64_MAX;
        Warning(info,"initial node table has %"PRIu64" entries",mdd_nodes);
        uniq_size=(nodes_fib + 1 <= FIB_MAX)?fib(nodes_fib+1):UINT64_MAX;
        Warning(info,"initial uniq table has %"PRIu64" entries",uniq_size);
        cache_size=(nodes_fib+cache_fib <= FIB_MAX)?fib(nodes_fib+cache_fib):UINT64_MAX;
        Warning(info,"initial op cache has %"PRIu64" entries",cache_size);

        unique_table=RTmalloc(uniq_size*sizeof(uint64_t));
        node_table=RTmalloc(mdd_nodes*sizeof(struct mdd_node));
        op_cache=RTalign(CACHE_LINE_SIZE,cache_size*sizeof(struct op_rec));

        for(uint64_t i=0;i<uniq_size;i++){
            unique_table[i]=0;
        }
        node_table[0].val=0;
        node_table[0].copy=COPY_COPY;
        node_table[1].val=0;
        node_table[1].copy=COPY_COPY;
        for(uint64_t i=2;i<mdd_nodes;i++){
            node_table[i].val=0;
            node_table[i].next=i+1;
            node_table[i].copy=COPY_COPY;
        }
        node_table[mdd_nodes-1].next=0;
        free_node=2;
        for(uint64_t i=0;i<cache_size;i++){
            op_cache[i].op=OP_UNUSED;
        }

        mdd_create_stack();

        set_copy = RTmalloc(sizeof(uint32_t)*n);
        for (int i=0; i < n; i++) set_copy[i] = COPY_DONT_CARE;
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
    dom->shared.rel_create_rw=rel_create_mdd_rw;
    dom->shared.rel_save_proj=rel_save_proj_bin;
    dom->shared.rel_save=rel_save_mdd;
    dom->shared.rel_load_proj=rel_load_proj_bin;
    dom->shared.rel_load=rel_load_mdd;
    dom->shared.rel_add=rel_add_mdd;
    dom->shared.rel_add_cpy=rel_add_mdd_cpy;
    dom->shared.rel_count=rel_count_mdd;
    dom->shared.set_project=set_project_mdd;
    dom->shared.set_next=set_next_mdd;
    dom->shared.set_prev=set_prev_mdd;
    dom->shared.set_universe=set_universe_mdd;
    dom->shared.set_example=set_example_mdd;
    dom->shared.set_random=set_random_mdd;
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
    dom->shared.set_join=set_join_mdd;
    dom->shared.separates_rw=separates_rw;
    return dom;
}
