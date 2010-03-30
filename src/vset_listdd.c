#include <config.h>
#include <stdlib.h>
#include <assert.h>

#include <vdom_object.h>
#include <runtime.h>
#include <stdint.h>

static uint32_t mdd_nodes=1000000;
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
		uint64_t count;
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


struct vector_domain {
	struct vector_domain_shared shared;
	// single global structure for now.
};

struct vector_set {
	vdom_t dom;
	vset_t next; // double linked list of protected mdd's;
	vset_t prev; //
	uint32_t mdd;
	int p_len;
	int proj[];
};

struct vector_relation {
	vdom_t dom;
	vrel_t next; // double linked list of protected mdd's;
	vrel_t prev; //
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
	if (mdd_top==20480) {
		fprintf(stderr,"stack overflow\n");
		exit(1);
	}
	mdd_stack[mdd_top]=mdd;
	mdd_top++;
}

static uint32_t mdd_pop(){
	if (mdd_top==0) {
		fprintf(stderr,"stack underflow\n");
		exit(1);
	}
	mdd_top--;
	return mdd_stack[mdd_top];
}

static uint32_t mdd_head;
static uint32_t mdd_children;
static uint32_t mdd_fanout[3];

static void mdd_mark_and_count(uint32_t mdd){
	if (mdd == 0 ) return;
	if (node_table[mdd].next&0x80000000) return;
	mdd_head++;
	node_table[mdd].next=node_table[mdd].next|0x80000000;
	int fanout=0;
	while(mdd){
		mdd_children++;
		fanout++;
		mdd_mark_and_count(node_table[mdd].down);
		mdd=node_table[mdd].right;
	}
	if (fanout>3) fanout=3;
	fanout--;
	mdd_fanout[fanout]++;
}

static void mdd_clear(uint32_t mdd){
	if (mdd == 0 ) return;
	if (! (node_table[mdd].next&0x80000000)) return;
	node_table[mdd].next=node_table[mdd].next&0x7fffffff;
	while(mdd){
		mdd_clear(node_table[mdd].down);
		mdd=node_table[mdd].right;
	}
}

static uint32_t mdd_node_count(uint32_t mdd);
static void mdd_vcount(uint32_t mdd,const char *name){
	mdd_head=0;
	mdd_children=0;
	mdd_fanout[0]=0;
	mdd_fanout[1]=0;
	mdd_fanout[2]=0;
	mdd_mark_and_count(mdd);
	mdd_clear(mdd);
	fprintf(stderr,"MDD %s has %u head nodes, %u children and %u list nodes\n",
		name,mdd_head,mdd_children,mdd_node_count(mdd));
	fprintf(stderr,"fanout 1,2,*: %u %u %u\n",mdd_fanout[0],mdd_fanout[1],mdd_fanout[2]);
}

static void mdd_mark(uint32_t mdd){
	if (mdd == 0 ) return;
	if (node_table[mdd].next&0x80000000) return;
	mdd_used++;
	node_table[mdd].next=node_table[mdd].next|0x80000000;
	mdd_mark(node_table[mdd].down);
	mdd_mark(node_table[mdd].right);
}

static void mdd_clear_and_count(uint32_t mdd,uint32_t *count){
	if (mdd == 0 ) return;
	if (node_table[mdd].next&0x80000000) {
		node_table[mdd].next=node_table[mdd].next&0x7fffffff;
		(*count)++;
		mdd_clear_and_count(node_table[mdd].down,count);
		mdd_clear_and_count(node_table[mdd].right,count);
	}
}

static uint32_t mdd_node_count(uint32_t mdd){
	uint32_t res=0;
	mdd_mark(mdd);
	mdd_clear_and_count(mdd,&res);
	return res;
}

static uint32_t mdd_sweep(uint32_t mdd){
	if (mdd==0) return 0;
	if (node_table[mdd].next&0x80000000){
		node_table[mdd].next=mdd_sweep(node_table[mdd].next&0x7fffffff);
		return mdd;
	} else {
		uint32_t tmp=node_table[mdd].next;
		node_table[mdd].next=free_node;
		free_node=mdd;
		return mdd_sweep(tmp);
	}
}

static void mdd_collect(uint32_t a,uint32_t b){
	mdd_used=0;
	fprintf(stderr,"marking");
	mdd_mark(a);
	fprintf(stderr,".");
	mdd_mark(b);
	fprintf(stderr,".");
	vset_t set=protected_sets;
	while(set!=NULL){
		mdd_mark(set->mdd);
		fprintf(stderr,".");
		set=set->next;
	}
	vrel_t rel=protected_rels;
	while(rel!=NULL){
		mdd_mark(rel->mdd);
		fprintf(stderr,".");
		rel=rel->next;
	}
	for(int i=0;i<mdd_top;i++){
		fprintf(stderr,"+");
		mdd_mark(mdd_stack[i]);
	}
	fprintf(stderr,"sweeping");
	for(uint32_t i=0;i<mdd_nodes;i++){
		switch(op_cache[i].op){
			case OP_UNUSED: continue;
			case OP_COUNT: {
				uint32_t mdd=op_cache[i].arg1;
				if (!(node_table[mdd].next&0x80000000)) op_cache[i].op=OP_UNUSED;
				continue;
			}
			case OP_UNION:
			case OP_MINUS:
			{
				uint32_t mdd=op_cache[i].arg1;
				if (!(node_table[mdd].next&0x80000000)) {
					op_cache[i].op=OP_UNUSED;
					continue;
				}
				mdd=op_cache[i].res.other.arg2;
				if (!(node_table[mdd].next&0x80000000)) {
					op_cache[i].op=OP_UNUSED;
					continue;
				}
				mdd=op_cache[i].res.other.res;
				if (!(node_table[mdd].next&0x80000000)) {
					op_cache[i].op=OP_UNUSED;
					continue;
				}
				continue;
			}
			default: fprintf(stderr,"missing case\n"); exit(1);
		}
	}
	fprintf(stderr,".");
	for(uint32_t i=0;i<mdd_nodes;i++){
		unique_table[i]=mdd_sweep(unique_table[i]);
	}
	fprintf(stderr,"done\nthere are %u nodes in use\n",mdd_used);
}

static uint64_t mdd_count(uint32_t mdd,int len){
	if (len>1){
		if (mdd==0) return 0;
		uint32_t slot=hash(OP_COUNT,mdd,0)%mdd_nodes;
		if (op_cache[slot].op==OP_COUNT && op_cache[slot].arg1==mdd){
			return op_cache[slot].res.count;
		}
		uint32_t res=mdd_count(node_table[mdd].down,len-1)+mdd_count(node_table[mdd].right,len);
		op_cache[slot].op=OP_COUNT;
		op_cache[slot].arg1=mdd;
		op_cache[slot].res.count=res;
		return res;
	} else {
		uint32_t res=0;
		while (mdd) {
			res++;
			mdd=node_table[mdd].right;
		}
		return res;
	}
}

static uint32_t mdd_create_node(uint32_t val,uint32_t down,uint32_t right){
	uint32_t slot=hash(val,down,right)%mdd_nodes;
	uint32_t res=unique_table[slot];
	while(res){
		if (node_table[res].val==val && node_table[res].down==down && node_table[res].right==right) return res;
		res=node_table[res].next;
	}
	if (free_node==0) {
		mdd_collect(down,right);
		if (free_node==0) {
			fprintf(stderr,"%d mdd nodes are not enough\n",mdd_nodes);
			exit(1);
		}
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
	if (b<a) { uint32_t tmp=a;a=b;b=tmp; }
	uint32_t slot=hash(OP_UNION,a,b)%mdd_nodes;
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
	uint32_t slot=hash(OP_MINUS,a,b)%mdd_nodes;
	if(op_cache[slot].op==OP_MINUS && op_cache[slot].arg1==a && op_cache[slot].res.other.arg2==b) {
		return op_cache[slot].res.other.res;
	}
	uint32_t tmp;
	if (node_table[a].val<node_table[b].val){
		tmp=mdd_minus(node_table[a].right,b);
		tmp=mdd_create_node(node_table[a].val,node_table[a].down,tmp);
	} else if (node_table[a].val==node_table[b].val){
		if (node_table[a].down==0) {
			tmp=mdd_minus(node_table[a].right,node_table[b].right);
		} else {
			tmp=mdd_minus(node_table[a].down,node_table[b].down);
			if (tmp) {
				mdd_push(tmp);
				tmp=mdd_minus(node_table[a].right,node_table[b].right);
				tmp=mdd_create_node(node_table[a].val,mdd_pop(),tmp);
			} else {
				tmp=mdd_minus(node_table[a].right,node_table[b].right);
			}
		}
	} else { //(node_table[a].val>node_table[b].val)
		tmp=mdd_minus(a,node_table[b].right);
	}
	op_cache[slot].op=OP_MINUS;
	op_cache[slot].arg1=a;
	op_cache[slot].res.other.arg2=b;
	op_cache[slot].res.other.res=tmp;
	return tmp;
}


static uint32_t mdd_member(uint32_t mdd,uint32_t *vec,int len){
	if (len==0) return 1;
	while(mdd){
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



static uint32_t mdd_put(uint32_t mdd,uint32_t *vec,int len,int* is_new){
	if (mdd) {
		if (node_table[mdd].val<vec[0]) {
			uint32_t right=mdd_put(node_table[mdd].right,vec,len,is_new);
			if (right==node_table[mdd].right) {
				if(is_new) *is_new=0;
				return mdd;
			}
			if(is_new) *is_new=1;
			return mdd_create_node(node_table[mdd].val,node_table[mdd].down,right);
		}
		if (node_table[mdd].val==vec[0]) {
			if (len>1){
				uint32_t down=mdd_put(node_table[mdd].down,vec+1,len-1,is_new);
				if (down==node_table[mdd].down){
					if(is_new) *is_new=0;
					return mdd;
				}
				if(is_new) *is_new=1;
				return mdd_create_node(node_table[mdd].val,down,node_table[mdd].right);
			} else {
				if(is_new) *is_new=0;
				return mdd;
			}
		}
		if (node_table[mdd].val>vec[0]) {
			if(is_new) *is_new=1;
			return mdd_create_node(vec[0],mdd_put(0,vec+1,len-1,NULL),mdd);
		}
	} else {
		uint32_t down;
		if(is_new) *is_new=1;
		if (len>1) {
			down=mdd_put(0,vec+1,len-1,NULL);
		} else {
			down=0;
		}
		return mdd_create_node(vec[0],down,0);
	}
}

static void mdd_enum(uint32_t mdd,uint32_t *vec,int idx,int len,vset_element_cb callback,void* context){
	if (idx==len) {
		callback(context,vec);
	} else {
		while(mdd){
			vec[idx]=node_table[mdd].val;
			mdd_enum(node_table[mdd].down,vec,idx+1,len,callback,context);
			mdd=node_table[mdd].right;
		}
	}
}

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

struct poptOption listdd_options[]= {
	{ "ldd-nodes",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &mdd_nodes , 0 , "set number of nodes","<nodes>"},
	POPT_TABLEEND
};


static vset_t set_create_mdd(vdom_t dom,int k,int* proj){
	vset_t set=(vset_t)RTmalloc(sizeof(struct vector_set)+k*sizeof(int));
	set->dom=dom;
	set->mdd=0;
	set->next=protected_sets;
	if (protected_sets) protected_sets->prev=set;
	protected_sets=set;
	set->p_len=k;
	for(int i=0;i<k;i++) set->proj[i]=proj[i];
	return set;
}

static vrel_t rel_create_mdd(vdom_t dom,int k,int* proj){
	vrel_t rel=(vrel_t)RTmalloc(sizeof(struct vector_relation)+k*sizeof(int));
	rel->dom=dom;
	rel->mdd=0;
	rel->next=protected_rels;
	if (protected_rels) protected_rels->prev=rel;
	protected_rels=rel;
	rel->p_len=k;
	for(int i=0;i<k;i++) rel->proj[i]=proj[i];
	return rel;
}

static void set_add_mdd(vset_t set,const int* e){
	int len=(set->p_len)?set->p_len:set->dom->shared.size;
	set->mdd=mdd_put(set->mdd,e,len,NULL);
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
	return mdd_member(set->mdd,e,len);
}

static void set_count_mdd(vset_t set,long *nodes,bn_int_t *elements){
	int len=(set->p_len)?set->p_len:set->dom->shared.size;
	uint64_t e_count=mdd_count(set->mdd,len);
	uint32_t n_count=mdd_node_count(set->mdd);
	double ed=e_count;
	bn_double2int(ed,elements);
	*nodes=n_count;
}

vdom_t vdom_create_list_native(int n){
	Warning(info,"Creating a native ListDD domain.");
	vdom_t dom=(vdom_t)RTmalloc(sizeof(struct vector_domain));
	vdom_init_shared(dom,n);
	if (unique_table==NULL) {
		if (mdd_nodes==0) Abort("please set mdd_nodes");	
		unique_table=RTmalloc(mdd_nodes*sizeof(int));
		node_table=RTmalloc(mdd_nodes*sizeof(struct mdd_node));
		op_cache=RTmalloc(mdd_nodes*sizeof(struct op_rec));

		for(int i=0;i<mdd_nodes;i++){
			unique_table[i]=0;
			node_table[i].next=i+1;
			op_cache[i].op=0;
		}
		node_table[mdd_nodes-1].next=0;
		free_node=1;
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
	//dom->shared.rel_create=rel_create_mdd;
/*
	dom->shared.set_enum_match=set_enum_match_fdd;
	dom->shared.set_count=set_count_fdd;
	dom->shared.set_union=set_union_fdd;
	dom->shared.set_minus=set_minus_fdd;
	dom->shared.set_zip=set_zip_fdd;
	dom->shared.set_project=set_project_fdd;
	dom->shared.rel_add=rel_add_fdd;
	dom->shared.rel_count=rel_count_fdd;
	dom->shared.set_next=set_next_appex_fdd;
	dom->shared.set_prev=set_prev_appex_fdd;
	dom->shared.reorder=vset_fdd_reorder;
*/
	return dom;
}

