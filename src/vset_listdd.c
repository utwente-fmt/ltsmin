#include <config.h>
#include <stdlib.h>
#include <assert.h>

#include <vdom_object.h>
#include <runtime.h>
#include <stdint.h>

static uint32_t mdd_nodes;
static uint32_t uniq_size;
static uint32_t cache_size;

/** fibonacci number of the size of the node table. */
static uint32_t nodes_fib=30;
/** difference between the fibonacci numbers of the sizes of the node table and the cache. */
static int cache_fib=0;

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
	{ "ldd-nodes",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &nodes_fib , 0 , "set intial step in node size","<step>"},
/* The following code is present as a hook for tuning the cache implementation,
   which is future work.
	{ "ldd-cache",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cache_fib , 0 , "set difference between cache and nodes","<diff>"},
 */
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

struct vector_domain {
	struct vector_domain_shared shared;
	// single global structure for now.
};

static uint32_t next_setid=0;

struct vector_set {
	vdom_t dom;
	vset_t next; // double linked list of protected mdd's;
	vset_t prev; //
	uint32_t setid;
	uint32_t mdd;
	int p_len;
	int proj[];
};

static uint32_t next_relid=0;

struct vector_relation {
	vdom_t dom;
	vrel_t next; // double linked list of protected mdd's;
	vrel_t prev; //
	uint32_t relid;
	uint32_t mdd;
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

static uint32_t mdd_used;

static uint32_t mdd_stack[20480];
static int mdd_top=0;

static void mdd_push(uint32_t mdd){
	if (mdd_top==20480) Abort("stack overflow");
	mdd_stack[mdd_top]=mdd;
	mdd_top++;
}

static uint32_t mdd_pop(){
	if (mdd_top==0) Abort("stack underflow");
	mdd_top--;
	return mdd_stack[mdd_top];
}

static void mdd_mark(uint32_t mdd){
	if (mdd<=1) return;
	if (node_table[mdd].val&0x80000000) return;
	mdd_used++;
	node_table[mdd].val=node_table[mdd].val|0x80000000;
	mdd_mark(node_table[mdd].down);
	mdd_mark(node_table[mdd].right);
}

static void mdd_clear_and_count(uint32_t mdd,uint32_t *count){
	if (mdd<=1) return;
	if (node_table[mdd].val&0x80000000) {
		node_table[mdd].val=node_table[mdd].val&0x7fffffff;
		(*count)++;
		mdd_clear_and_count(node_table[mdd].down,count);
		mdd_clear_and_count(node_table[mdd].right,count);
	}
}

static uint32_t mdd_node_count(uint32_t mdd){
    if (mdd==0) return 1; // just emptyset
	uint32_t res=0;
	mdd_mark(mdd);
	mdd_clear_and_count(mdd,&res);
	return res+2; // real nodes plus emptyset(0) and singleton epsilon(1).
}

static uint32_t mdd_sweep_bucket(uint32_t mdd){
	if (mdd==0) return 0;
	if (mdd==1) Abort("data corruption");
	if (node_table[mdd].val&0x80000000){
        node_table[mdd].val=node_table[mdd].val&0x7fffffff;
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
		set=set->next;
	}
	vrel_t rel=protected_rels;
	while(rel!=NULL){
		mdd_mark(rel->mdd);
		rel=rel->next;
	}
	for(int i=0;i<mdd_top;i++){
		mdd_mark(mdd_stack[i]);
	}
	/* The following code marks results of projection and
	   next, to allow them to remain in the cache. On the
	   few tests done, there did not seem to be a speedup
	   but the memory use went up considerably.
	   Still it may be useful for saturation.
	for(uint32_t i=0;i<cache_size;i++){
	    uint32_t slot,op,arg1,arg2,res;
	    op=op_cache[i].op&0xffff;
		switch(op){
			case OP_PROJECT:
            {
                arg1=op_cache[i].arg1;
                if (!(node_table[arg1].val&0x80000000)) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                res=op_cache[i].res.other.res;
                mdd_mark(res);
                continue;
            }
            case OP_SAT:
            case OP_RELPROD:
            case OP_UNION:
			{
				arg1=op_cache[i].arg1;
				if (!(node_table[arg1].val&0x80000000)) {
					op_cache[i].op=OP_UNUSED;
					continue;
				}
				arg2=op_cache[i].res.other.arg2;
				if (!(node_table[arg2].val&0x80000000)) {
					op_cache[i].op=OP_UNUSED;
					continue;
				}
				res=op_cache[i].res.other.res;
                mdd_mark(res);
				continue;
			}
			default: continue;
		}
	}
	*/
	Warning(info,"ListDD garbage collection: %d of %d nodes used",mdd_used,mdd_nodes);
	int resize=0;
	uint32_t new_cache_size;
	struct op_rec *new_cache;
	uint32_t copy_count=0;
	if (mdd_used > fib(nodes_fib-1)){
	    Warning(info,"insufficient free nodes, resizing");
	    resize=1;
        new_cache_size=fib(nodes_fib+cache_fib);
        new_cache=RTmalloc(new_cache_size*sizeof(struct op_rec));
		for(uint32_t i=0;i<new_cache_size;i++){
			new_cache[i].op=0;
		}
		if (new_cache_size < cache_size) Abort("cache size overflow");
		Warning(info,"new cache has %u entries",new_cache_size);
	}
	for(uint32_t i=0;i<cache_size;i++){
	    uint32_t slot,op,arg1,arg2,res;
	    op=op_cache[i].op&0xffff;
		switch(op){
			case OP_UNUSED: continue;
			case OP_COUNT: {
				arg1=op_cache[i].arg1;
				arg2=0;
				if (!(node_table[arg1].val&0x80000000)){
				    op_cache[i].op=OP_UNUSED;
				    continue;
			    }
				if (resize) break;
                else continue;
			}
			case OP_PROJECT:
            {
                arg1=op_cache[i].arg1;
                if (!(node_table[arg1].val&0x80000000)) {
                    op_cache[i].op=OP_UNUSED;
                    continue;
                }
                arg2=op_cache[i].res.other.arg2;
                res=op_cache[i].res.other.res;
                if (!(node_table[res].val&0x80000000)) {
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
				if (!(node_table[arg1].val&0x80000000)) {
					op_cache[i].op=OP_UNUSED;
					continue;
				}
				arg2=op_cache[i].res.other.arg2;
				if (!(node_table[arg2].val&0x80000000)) {
					op_cache[i].op=OP_UNUSED;
					continue;
				}
				res=op_cache[i].res.other.res;
				if (!(node_table[res].val&0x80000000)) {
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
	    Warning(info,"copied %u cache nodes",copy_count);
	    RTfree(op_cache);
	    op_cache=new_cache;
	    cache_size=new_cache_size;
	    nodes_fib++;
		uint32_t old_size=mdd_nodes;
		mdd_nodes=fib(nodes_fib);
	    uniq_size=fib(nodes_fib+1);
	    if (uniq_size<mdd_nodes) Abort("overflow in node table resize");
	    unique_table=RTrealloc(unique_table,uniq_size*sizeof(int));
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
	        if (node_table[i].val&0x80000000){
                node_table[i].val=node_table[i].val&0x7fffffff;
	            uint32_t slot=hash(node_table[i].val,node_table[i].down,node_table[i].right)%uniq_size;
		        node_table[i].next=unique_table[slot];
		        unique_table[slot]=i;
	        } else {
	            node_table[i].next=free_node;
	            free_node=i;
	        }
		}
	    Warning(info,"node/unique tables have %u/%u entries",mdd_nodes,uniq_size);
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
	if (right>1 && val>=node_table[right].val) Abort("bad order %d %d",*((int*)1),node_table[right].val);
	uint32_t slot_hash=hash(val,down,right);
	uint32_t slot=slot_hash%uniq_size;
	uint32_t res=unique_table[slot];
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
		// recompute slot in case of resize...
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


static uint32_t mdd_member(uint32_t mdd,const uint32_t *vec,int len){
    if (len==0) {
        while(mdd>1) mdd=node_table[mdd].right;
        return mdd;
    }
    while(mdd>1){
        if (node_table[mdd].val<vec[0]) {
            mdd=node_table[mdd].right;
        } else if (node_table[mdd].val==vec[0]) {
            return mdd_member(node_table[mdd].down,vec+1,len-1);
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

static void mdd_enum(uint32_t mdd,uint32_t *vec,int idx,int len,vset_element_cb callback,void* context){
    if (idx==len) {
        if (mdd!=1) Abort("non-uniform length");
        while(mdd>1) mdd=node_table[mdd].right;
        if (mdd) callback(context,(int*)vec);
    } else {
        while(mdd>1){
            vec[idx]=node_table[mdd].val;
            mdd_enum(node_table[mdd].down,vec,idx+1,len,callback,context);
            mdd=node_table[mdd].right;
        }
        if (mdd!=0) Abort("non-uniform length");
    }
}

static uint32_t
mdd_copy_match(uint32_t mdd, int len, uint32_t pattern, int *proj,
                   int p_len, int ofs)
{
    uint32_t slot_hash=hash(OP_COPY_MATCH, mdd, pattern);
    uint32_t slot=slot_hash%cache_size;

    if (op_cache[slot].op == OP_COPY_MATCH && op_cache[slot].arg1 == mdd
            && op_cache[slot].res.other.arg2 == pattern) {
        return op_cache[slot].res.other.res;
    }

    uint32_t tmp;

    if (mdd != 0 && mdd != 1) {
        tmp = 0;

        if (ofs < len) {
            // if still matching and this offset matches,
            // compare the value and return it if it matches
            if (p_len > 0 && proj[0] == ofs) {
                // does it match?
                if (node_table[mdd].val == node_table[pattern].val) {
                    // try to match the next element, return the result
                    tmp = mdd_copy_match(node_table[mdd].down, len,
                                             node_table[pattern].down,
                                             proj + 1, p_len - 1, ofs + 1);
                }
            // not matching anymore or the offset doesn't match
            } else {
                tmp = mdd_copy_match(node_table[mdd].down, len, pattern, proj,
                                         p_len, ofs + 1);
            }
        }
        mdd_push(tmp);
        tmp = 0;
        // test matches in the second argument
        if (ofs <= len) {
            tmp = mdd_copy_match(node_table[mdd].right, len, pattern, proj,
                                     p_len, ofs);
        }
        // combine results
        tmp = mdd_create_node(node_table[mdd].val, mdd_pop(), tmp);
    } else {
        if (mdd == 1 && ofs == len) {
            tmp = mdd;
        } else {
            tmp = 0;
        }
    }

    slot = slot_hash % cache_size;
    op_cache[slot].op = OP_COPY_MATCH;
    op_cache[slot].arg1 = mdd;
    op_cache[slot].res.other.arg2 = pattern;
    op_cache[slot].res.other.res  = tmp;

    return tmp;
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

static vset_t set_create_mdd(vdom_t dom,int k,int* proj){
    vset_t set=(vset_t)RTmalloc(sizeof(struct vector_set)+k*sizeof(int));
    set->dom=dom;
    set->mdd=0;
    set->setid=next_setid++;
    set->next=protected_sets;
    set->prev=NULL;
    if (protected_sets != NULL) protected_sets->prev=set;
    protected_sets=set;
    set->p_len=k;
    for(int i=0;i<k;i++) {
        set->proj[i]=proj[i];
    }
    return set;
}

static void set_destroy_mdd(vset_t set)
{
    if (protected_sets == set) protected_sets = set->next;
    if (set->prev != NULL) set->prev->next = set->next;
    if (set->next != NULL) set->next->prev = set->prev;

    RTfree(set);
}

static void set_reorder_mdd() { }

static vrel_t rel_create_mdd(vdom_t dom,int k,int* proj){
    vrel_t rel=(vrel_t)RTmalloc(sizeof(struct vector_relation)+k*sizeof(int));
    rel->dom=dom;
    rel->mdd=0;
    rel->relid=next_relid++;
    rel->next=protected_rels;
    rel->prev=NULL;
    if (protected_rels != NULL) protected_rels->prev=rel;
    protected_rels=rel;
    rel->p_len=k;
    for(int i=0;i<k;i++) {
        rel->proj[i]=proj[i];
    }
    return rel;
}

static void set_add_mdd(vset_t set,const int* e){
	int len=(set->p_len)?set->p_len:set->dom->shared.size;
	set->mdd=mdd_put(set->mdd,(uint32_t*)e,len,NULL);
}

static int set_is_empty_mdd(vset_t set){
	return (set->mdd==0);
}

static int set_equal_mdd(vset_t set1,vset_t set2){
	return (set1->mdd==set2->mdd);
}

static void set_clear_mdd(vset_t set){
	set->mdd=0;
}

static void set_copy_mdd(vset_t dst,vset_t src){
	dst->mdd=src->mdd;
}

static void set_enum_mdd(vset_t set,vset_element_cb cb,void* context){
	int len=(set->p_len)?set->p_len:set->dom->shared.size;
	uint32_t vec[len];
	mdd_enum(set->mdd,vec,0,len,cb,context);
}

static int set_member_mdd(vset_t set,const int* e){
	int len=(set->p_len)?set->p_len:set->dom->shared.size;
	return mdd_member(set->mdd,(uint32_t*)e,len);
}

static void set_count_mdd(vset_t set,long *nodes,bn_int_t *elements){
    double e_count=mdd_count(set->mdd);
    uint32_t n_count=mdd_node_count(set->mdd);
    bn_double2int(e_count,elements);
    *nodes=n_count;
}

static void rel_count_mdd(vrel_t rel,long *nodes,bn_int_t *elements){
    double e_count=mdd_count(rel->mdd);
    uint32_t n_count=mdd_node_count(rel->mdd);
    bn_double2int(e_count,elements);
    *nodes=n_count;
}

static void set_union_mdd(vset_t dst,vset_t src){
    dst->mdd=mdd_union(dst->mdd,src->mdd);
}

static void set_minus_mdd(vset_t dst,vset_t src){
    dst->mdd=mdd_minus(dst->mdd,src->mdd);
}

static void
set_intersect_mdd(vset_t dst, vset_t src)
{
    dst->mdd = mdd_intersect(dst->mdd, src->mdd);
}

static void
set_copy_match_mdd(vset_t src, vset_t dst, int p_len, int *proj, int *match)
{
    int len = (src->p_len)?src->p_len:src->dom->shared.size;
    uint32_t singleton = mdd_put(0, (uint32_t*)match, p_len, NULL);

    mdd_push(singleton);
    dst->mdd = mdd_copy_match(src->mdd, len, singleton, proj, p_len, 0);
    mdd_pop();
}

static void
set_enum_match_mdd(vset_t set, int p_len, int *proj, int *match,
                       vset_element_cb cb, void *context)
{
    int len = (set->p_len)?set->p_len:set->dom->shared.size;
    uint32_t singleton, tmp;

    singleton = mdd_put(0, (uint32_t*)match, p_len, NULL);
    mdd_push(singleton);
    tmp = mdd_copy_match(set->mdd, len, singleton, proj, p_len, 0);
    mdd_pop();

    uint32_t vec[len];

    mdd_enum(tmp, vec, 0, len, cb, context);
}

static void rel_add_mdd(vrel_t rel,const int* src, const int* dst){
    int N=rel->p_len?rel->p_len:rel->dom->shared.size;
    uint32_t vec[2*N];
    for(int i=0;i<N;i++) {
        vec[i+i]=src[i];
        vec[i+i+1]=dst[i];
    }
    rel->mdd=mdd_put(rel->mdd,vec,2*N,NULL);
}

static uint32_t mdd_project(uint32_t setid,uint32_t mdd,int idx,int*proj,int len){
    if(mdd==0) return 0; //projection of empty is empty.
    if(len==0) return 1; //projection of non-empty is epsilon.

    uint32_t slot_hash=hash(OP_PROJECT,mdd,setid);
    uint32_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==OP_PROJECT && op_cache[slot].arg1==mdd
       && op_cache[slot].res.other.arg2==setid) {
        return op_cache[slot].res.other.res;
    }

    uint32_t res = 0;
    uint32_t mdd_original = mdd;

    if (proj[0]==idx){
        mdd_push(mdd_project(setid,node_table[mdd].right,idx,proj,len));
        uint32_t tmp=mdd_project(setid,node_table[mdd].down,idx+1,proj+1,len-1);
        res=mdd_create_node(node_table[mdd].val,tmp,mdd_pop());
    } else {
        while(mdd>1){
            mdd_push(res);
            uint32_t tmp=mdd_project(setid,node_table[mdd].down,idx+1,proj,len);
            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            mdd=node_table[mdd].right;
        }
    }

    slot=slot_hash%cache_size;
    op_cache[slot].op=OP_PROJECT;
    op_cache[slot].arg1=mdd_original;
    op_cache[slot].res.other.arg2=setid;
    op_cache[slot].res.other.res=res;
    return res;
}

static uint32_t mdd_next(int relid,uint32_t set,uint32_t rel,int idx,int*proj,int len){
    if (len==0) return set;
    if (rel==0||set==0) return 0;
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
    uint32_t op=OP_NEXT|(relid<<16);
    uint32_t slot_hash=hash(op,set,rel);
    uint32_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==op && op_cache[slot].arg1==set && op_cache[slot].res.other.arg2==rel) {
        return op_cache[slot].res.other.res;
    }
    uint32_t res=0;
    if (proj[0]==idx){
        res=mdd_next(relid,node_table[set].right,node_table[rel].right,idx,proj,len);
        rel=node_table[rel].down;
        while(rel>1){
            mdd_push(res);
            uint32_t tmp=mdd_next(relid,node_table[set].down,node_table[rel].down,idx+1,proj+1,len-1);
            tmp=mdd_create_node(node_table[rel].val,tmp,0);
            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            rel=node_table[rel].right;
        }
    } else {
        mdd_push(mdd_next(relid,node_table[set].right,rel,idx,proj,len));
        res=mdd_next(relid,node_table[set].down,rel,idx+1,proj,len);
        res=mdd_create_node(node_table[set].val,res,mdd_pop());
    }

    slot=slot_hash%cache_size;
    op_cache[slot].op=op;
    op_cache[slot].arg1=set;
    op_cache[slot].res.other.arg2=rel;
    op_cache[slot].res.other.res=res;
    return res;
}

static void set_project_mdd(vset_t dst,vset_t src){
    dst->mdd=0;
    dst->mdd=mdd_project(dst->setid,src->mdd,0,dst->proj,dst->p_len);
}

static void set_next_mdd(vset_t dst,vset_t src,vrel_t rel){
    if (rel->p_len==0) Abort("next requires strict subvector");
    dst->mdd=mdd_next(rel->relid,src->mdd,rel->mdd,0,rel->proj,rel->p_len);
}


static void set_example_mdd(vset_t set,int *e){
    int len=(set->p_len)?set->p_len:set->dom->shared.size;
    uint32_t mdd=set->mdd;
    for(int i=0;i<len;i++){
        if (mdd<=1) Abort("empty set??");
        e[i]=node_table[mdd].val;
        mdd=node_table[mdd].down;
    }
    if (mdd!=1) Abort("non-uniform length");
}


static uint32_t mdd_prev(int relid,uint32_t set,uint32_t rel,int idx,int*proj,int len){
    if (len==0) return set;
    if (rel==0||set==0) return 0;
    if (rel==1||set==1) Abort("missing case in prev");
    uint32_t op=OP_PREV|(relid<<16);
    uint32_t slot_hash=hash(op,set,rel);
    uint32_t slot=slot_hash%cache_size;
    if(op_cache[slot].op==op && op_cache[slot].arg1==set && op_cache[slot].res.other.arg2==rel) {
        return op_cache[slot].res.other.res;
    }
    uint32_t res=0;
    if (proj[0]==idx){
        uint32_t right=mdd_prev(relid,set,node_table[rel].right,idx,proj,len);
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
            uint32_t tmp=mdd_prev(relid,node_table[set].down,node_table[rel].down,idx+1,proj+1,len-1);
            mdd_push(tmp);
            res=mdd_union(res,tmp);
            mdd_pop();mdd_pop();
            rel=node_table[rel].right;
            set=node_table[set].right;
        }
        res=mdd_create_node(val,res,mdd_pop());
    } else {
        mdd_push(mdd_prev(relid,node_table[set].right,rel,idx,proj,len));
        res=mdd_prev(relid,node_table[set].down,rel,idx+1,proj,len);
        res=mdd_create_node(node_table[set].val,res,mdd_pop());
    }

    slot=slot_hash%cache_size;
    op_cache[slot].op=op;
    op_cache[slot].arg1=set;
    op_cache[slot].res.other.arg2=rel;
    op_cache[slot].res.other.res=res;
    return res;
}

static void set_prev_mdd(vset_t dst,vset_t src,vrel_t rel){
    if (rel->p_len==0) Abort("prev requires strict subvector");
    dst->mdd=mdd_prev(rel->relid,src->mdd,rel->mdd,0,rel->proj,rel->p_len);
}

typedef struct {
    uint32_t tg_len;
    uint32_t *top_groups;
} top_groups_info;

static vrel_t *rel_set;
static uint32_t rels_tot;
static top_groups_info *top_groups;

static uint32_t saturate(int level, uint32_t mdd);
static uint32_t set_reach_sat(uint32_t relid, uint32_t set, uint32_t rel,
                                int idx, int *proj, int len);

static uint32_t
copy_level_sat(uint32_t relid, uint32_t set, uint32_t rel, int idx,
               int *proj, int len)
{
    if (set == 0) return 0;
    if (set == 1) Abort("missing case in copy_level");

    uint32_t tmp = set_reach_sat(relid, node_table[set].down, rel, idx + 1,
                                   proj, len);
    mdd_push(tmp);
    tmp = copy_level_sat(relid, node_table[set].right, rel, idx, proj, len);
    return mdd_create_node(node_table[set].val, mdd_pop(), tmp);
}

static uint32_t
apply_reach_sat(uint32_t relid, uint32_t set, uint32_t rel, int idx,
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
                uint32_t tmp = set_reach_sat(relid, node_table[set].down,
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
set_reach_sat(uint32_t relid, uint32_t set, uint32_t rel, int idx,
                int *proj, int len)
{
    if (len == 0) return set;
    if (set == 0 || rel == 0) return 0;
    if (set == 1 || rel == 1) Abort("missing case in set_reach_sat");

    uint32_t op = OP_RELPROD | (relid << 16);
    uint32_t slot_hash = hash(op, set, rel);
    uint32_t slot = slot_hash % cache_size;

    if (op_cache[slot].op == op
          && op_cache[slot].arg1 == set
          && op_cache[slot].res.other.arg2 == rel)
        return op_cache[slot].res.other.res;

    uint32_t res = 0;

    if (proj[0] == idx)
        res = apply_reach_sat(relid, set, rel, idx, proj, len);
    else
        res = copy_level_sat(relid, set, rel, idx, proj, len);

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
apply_reach_fixpoint(uint32_t relid, uint32_t set, uint32_t rel, int idx,
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
                uint32_t tmp = set_reach_sat(relid, node_table[new_res].down,
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
        for (uint32_t i = 0; i < groups_info.tg_len; i++) {
            uint32_t grp = groups_info.top_groups[i];
            mdd_push(new_set);
            new_set = apply_reach_fixpoint(rel_set[grp]->relid, new_set,
                                           rel_set[grp]->mdd, level,
                                           rel_set[grp]->proj,
                                           rel_set[grp]->p_len);
            mdd_pop();
        }
    }

    return new_set;
}

// Traverse the local state values of an MDD node recursively:
// - Base case: end of MDD node is reached OR MDD node is atom node.
// - Induction: construct a new MDD node with the link to next entry of the
//   MDD node handled recursively
static uint32_t
saturate_locals(int level, uint32_t node_set)
{
    if (node_set == 0) return node_set;
    if (node_set == 1) Abort("missing case in saturate_locals");

    mdd_push(saturate(level + 1, node_table[node_set].down));
    uint32_t new_node_set = saturate_locals(level, node_table[node_set].right);
    return mdd_create_node(node_table[node_set].val, mdd_pop(), new_node_set);
}

// Saturation process for the MDD at a given level
static uint32_t
saturate(int level, uint32_t mdd)
{
    if (mdd == 0 || mdd == 1) return mdd;

    uint32_t slot_hash = hash(OP_SAT, mdd, rels_tot);
    uint32_t slot = slot_hash % cache_size;

    if (op_cache[slot].op == OP_SAT
          && op_cache[slot].arg1 == mdd
          && op_cache[slot].res.other.arg2 == rels_tot)
        return op_cache[slot].res.other.res;

    uint32_t new_set = saturate_locals(level, mdd);
    mdd_push(new_set);
    new_set = sat_fixpoint(level, new_set);
    mdd_pop();

    slot = slot_hash % cache_size;
    op_cache[slot].op = OP_SAT;
    op_cache[slot].arg1 = mdd;
    op_cache[slot].res.other.arg2 = rels_tot;
    op_cache[slot].res.other.res = new_set;
    return new_set;
}

// Perform fixpoint calculations using the "General Basic Saturation" algorithm
static void
set_least_fixpoint_mdd(vset_t dst, vset_t src, vrel_t rels[], int rel_count)
{
    uint32_t count = (uint32_t)rel_count;

    // Only implemented if not projected
    assert (src->p_len == 0 && dst->p_len == 0);

    // Initialize partitioned transition relations.
    rel_set = rels;

    uint32_t rels_tmp = 0;

    for (uint32_t i = 0; i < count; i++)
        rels_tmp = mdd_create_node(count - i, rels[i]->mdd, rels_tmp);

    mdd_push(rels_tmp);
    rels_tot = rels_tmp;

    // Retrieve initial state vector and its length.
    uint32_t init_state_len = src->dom->shared.size;
    uint32_t init_state_set = src->mdd;

    // Initialize top_groups_info array
    // This stores transitions groups per topmost level

    top_groups = RTmalloc(sizeof(top_groups_info[init_state_len]));

    for (uint32_t lvl = 0; lvl < init_state_len; lvl++) {
        top_groups[lvl].top_groups = RTmalloc(sizeof(uint32_t[count]));
        top_groups[lvl].tg_len = 0;
    }

    for (uint32_t grp = 0; grp < count; grp++) {
        uint32_t top_lvl = rels[grp]->proj[0];
        top_groups[top_lvl].top_groups[top_groups[top_lvl].tg_len] = grp;
        top_groups[top_lvl].tg_len++;
    }

    // Saturation on initial state set
    dst->mdd = saturate(0, init_state_set);

    // Clean-up
    rel_set = NULL;
    rels_tot = 0;
    mdd_pop();

    for (uint32_t lvl = 0; lvl < init_state_len; lvl++)
        RTfree(top_groups[lvl].top_groups);

    RTfree(top_groups);
}


vdom_t vdom_create_list_native(int n){
	Warning(info,"Creating a native ListDD domain.");
	vdom_t dom=(vdom_t)RTmalloc(sizeof(struct vector_domain));
	vdom_init_shared(dom,n);
	if (unique_table==NULL) {
	    mdd_nodes=fib(nodes_fib);
	    Warning(info,"initial node table has %u entries",mdd_nodes);
	    uniq_size=fib(nodes_fib+1);
	    Warning(info,"initial uniq table has %u entries",uniq_size);
        cache_size=fib(nodes_fib+cache_fib);
        Warning(info,"initial operation cache has %u entries",cache_size);

		unique_table=RTmalloc(uniq_size*sizeof(int));
		node_table=RTmalloc(mdd_nodes*sizeof(struct mdd_node));
		op_cache=RTmalloc(cache_size*sizeof(struct op_rec));

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
	}
	dom->shared.set_create=set_create_mdd;
	dom->shared.set_member=set_member_mdd;
	dom->shared.set_add=set_add_mdd;
	dom->shared.set_is_empty=set_is_empty_mdd;
	dom->shared.set_equal=set_equal_mdd;
	dom->shared.set_clear=set_clear_mdd;
	dom->shared.set_copy=set_copy_mdd;
	dom->shared.set_enum=set_enum_mdd;
	dom->shared.set_count=set_count_mdd;
    dom->shared.set_union=set_union_mdd;
    dom->shared.set_minus=set_minus_mdd;
    dom->shared.rel_create=rel_create_mdd;
    dom->shared.rel_add=rel_add_mdd;
    dom->shared.rel_count=rel_count_mdd;
    dom->shared.set_project=set_project_mdd;
    dom->shared.set_next=set_next_mdd;
    dom->shared.set_prev=set_prev_mdd;
    dom->shared.set_example=set_example_mdd;
    dom->shared.set_enum_match=set_enum_match_mdd;
    dom->shared.set_copy_match=set_copy_match_mdd;
    dom->shared.set_intersect=set_intersect_mdd;
    // default implementation for dom->shared.set_zip
    dom->shared.reorder=set_reorder_mdd;
    dom->shared.set_destroy=set_destroy_mdd;
    dom->shared.set_least_fixpoint=set_least_fixpoint_mdd;
    return dom;
}

