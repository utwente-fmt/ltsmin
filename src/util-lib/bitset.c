#include <hre/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <hre/user.h>
#include <util-lib/balloc.h>
#include <util-lib/bitset.h>

/**
A leaf bitset is an array of words, one word contains 64 bits.
 */
typedef uint64_t word_t;
/**
  The number of bits in the integers represented by one word.
  The get the word in a leaf to wich an elements belongs,
  on must shift to the right by this number of bits.
 */
#define WORD_CLASS 6
/**
  The mask needed to get the bit in a word of a given leaf.
 */
#define WORD_MASK ((1<<WORD_CLASS)-1)

struct bitset {
    /**
        The first part of a bitset is a tree that stores
        the initial part of the set, which is neither empty nor full.
     */
    void *set;
    /** The depth of the tree. */
    int depth;
    /** The number of elements that can be represented by the tree. */
	element_t set_size;
    /** The second part is either the full set or the empty set. */
	void *default_value;
	
	/** The internal nodes of the tree are managed with this allocater. */
	allocater_t node_alloc;
	/** The number of bits of a node index. */
	int node_class;
	/** The number of chiuldren of a node. */
	unsigned int node_size;
	
	/** The leaf nodes of the tree are managed with this allocater. */
	allocater_t base_alloc;	
	/** The number of bits for indexing a bit in a leaf node. */
	int base_class;
	/** The number of words in a leaf node. */
	unsigned int base_words;
};

/** The emtpy set. */
#define ALL_ZERO ((void*)1)

/** The full set. */
#define ALL_ONES ((void*)2)


/** The empty word. */
#define WORD_EMPTY ((word_t)0)
/** The singleton 1. */
#define WORD_ONE ((word_t)1)
/** The full word. */
#define WORD_FULL ((word_t)(-1))

/** The element 1. */
#define ELEM_ONE ((element_t)1)

/** return the least significant bits of the element */
#define ELEM_MASK(e,bits) (e&((ELEM_ONE<<bits)-1))

static int ilog2(int n){
    if (n<1) Abort("argument is not a power of 2");
    int i=0;
    while(n!=1){
        if (n&1) Abort("argument is not a power of 2");
        i++;
        n=n>>1;
    }
    return i;
}

bitset_t bitset_create(int node_size,int leaf_size){
    bitset_t set=RT_NEW(struct bitset);
    
    /* compute internal node auxiliary values. */
    int void_p_size=sizeof(void*);
    if (node_size%void_p_size) Abort("node size is not a multiple of the pointer size");
    set->node_size=node_size/void_p_size;
    set->node_class=ilog2(set->node_size);
    if (set->node_class==0) Abort("node too small for two pointers");
    
    /* compute leaf node auxiliary values. */
    if (leaf_size%sizeof(word_t)) Abort("leaf size is not a multiple of the word size");
    set->base_class=ilog2(leaf_size)+3; // one byte is 8 bits
    set->base_words=leaf_size/sizeof(word_t);
    
    /* initially the set is just one empty leaf. */
	set->set_size=ELEM_ONE << (set->base_class);
	set->set=ALL_ZERO;
	set->default_value=ALL_ZERO;
	set->depth=0;

    /* create the allocaters. */
	set->node_alloc=BAcreate(node_size,1024*1024);
	if (node_size == leaf_size){
	    set->base_alloc=set->node_alloc;
	    BAaddref(set->node_alloc);
	} else {
    	set->base_alloc=BAcreate(leaf_size,1024*1024);
	}

    /* done */
	return set;
}

bitset_t bitset_create_shared(bitset_t set){
    bitset_t newset=RT_NEW(struct bitset);
    
    /* copy internal node settings */
	newset->node_class=set->node_class;
	newset->node_size=set->node_size;
	
	/* copy leaf node settings */
	newset->base_class=set->base_class;
	newset->base_words=set->base_words;
   
    /* create empty set */
	newset->set_size=ELEM_ONE << (set->base_class);
	newset->set=ALL_ZERO;
	newset->default_value=ALL_ZERO;
	newset->depth=0;
	
	/* copy allocaters */
	newset->node_alloc=set->node_alloc;
	BAaddref(newset->node_alloc);
	newset->base_alloc=set->base_alloc;
	BAaddref(newset->base_alloc);
	return newset;
}

/** Recursively free the given set */
static void free_set(bitset_t main,int depth,void *set){
	if (set==ALL_ZERO) return;
	if (set==ALL_ONES) return;
	if (depth){ // internal node
	    void **node=(void**)set;
		for(unsigned int i=0;i<main->node_size;i++){
			free_set(main,depth-1,node[i]);
		}
		BAfree(main->node_alloc,set);
	} else { // leaf node
		BAfree(main->base_alloc,set);
	}
}

void bitset_destroy(bitset_t set){
    free_set(set,set->depth,set->set);
    BAderef(set->node_alloc);
    BAderef(set->base_alloc);
    RTfree(set);
}

void bitset_clear_all(bitset_t set){
	free_set(set,set->depth,set->set);
	set->set_size=ELEM_ONE << (set->base_class);
	set->set=ALL_ZERO;
	set->default_value=ALL_ZERO;
	set->depth=0;
}

void bitset_set_all(bitset_t set){
	free_set(set,set->depth,set->set);
	set->set_size=ELEM_ONE << (set->base_class);
	set->set=ALL_ONES;
	set->default_value=ALL_ONES;
	set->depth=0;
}

/** expand the tree by one level */
static void expand(bitset_t set){
	void **newnode=(void**)BAget(set->node_alloc);
	newnode[0]=set->set;
	for(unsigned int i=1;i<set->node_size;i++){
		newnode[i]=set->default_value;
	}
	set->set=(void*)newnode;
	set->set_size<<=set->node_class;
	set->depth++;
}

/** collapse a full or empty internal node */
static void* simplify_node(bitset_t main,void**node){
    if(node[0]!=ALL_ZERO && node[0]!=ALL_ONES) return node;
    for(unsigned int i=1;i<main->node_size;i++){
        if (node[i]!=node[0]) return node;
    }
    void* result=node[0];
    BAfree(main->node_alloc,node);
    return result;
}

/** collapse a full or empty leaf node */
static void* simplify_leaf(bitset_t main,word_t*node){
    switch(node[0]){
    case WORD_EMPTY:
        for(unsigned int i=1;i<main->base_words;i++){
            if (node[i]!=WORD_EMPTY) return node;
        }
        BAfree(main->base_alloc,node);
        return ALL_ZERO;
    case WORD_FULL:
        for(unsigned int i=1;i<main->base_words;i++){
            if (node[i]!=WORD_FULL) return node;
        }
        BAfree(main->base_alloc,node);
        return ALL_ONES;
    default:
        return node;
    }
}

static void** expand_node(bitset_t main,void*node){
    if(node!=ALL_ZERO && node!=ALL_ONES) return node;
    void**res=(void**)BAget(main->node_alloc);
    for(unsigned int i=0;i<main->node_size;i++){
        res[i]=node;
    }
    return res;
}

static word_t* expand_leaf(bitset_t main,void*node){
    word_t w;
    if(node==ALL_ONES){
        w=WORD_FULL;
    } else if (node==ALL_ZERO){
        w=WORD_EMPTY;
    } else {
        return node;
    }
    word_t* res=(word_t*)BAget(main->base_alloc);
    for(unsigned int i=0;i<main->base_words;i++){
        res[i]=w;
    }
    return res;
}

/** Compute the number of bits in the child index at the given depth. */
static int child_bits(bitset_t main,int depth){
    return (depth-1)*main->node_class + main->base_class;
}

/** Set the given bit. */
static void* set_bit(bitset_t main,void* set,int depth,element_t e,int*new){
    if (set==ALL_ONES) return set;
    if (depth) {
        void**node=expand_node(main,set);
        int bits=child_bits(main,depth);
        int branch=e>>bits;
        e=ELEM_MASK(e,bits);
        node[branch]=set_bit(main,node[branch],depth-1,e,new);
        if(*new){
            return simplify_node(main,node);
        } else {
            return node;
        }
    } else {
        word_t *node=expand_leaf(main,set);
        int word=e>>WORD_CLASS;
        e=ELEM_MASK(e,WORD_CLASS);
        word_t bit=(WORD_ONE<<e);
        if (!(node[word]&bit)) {
            *new=1;
            node[word]=node[word]|bit;
            return simplify_leaf(main,node);
        } else {
            return node;
        }
    }
}

int bitset_set(bitset_t set,element_t e){
    while(!(e<set->set_size)){
        expand(set);
    }
    int new=0;
    set->set=set_bit(set,set->set,set->depth,e,&new);
    return new;
}

/** Set the given bit. */
static void* clear_bit(bitset_t main,void* set,int depth,element_t e){
    if (set==ALL_ZERO) return set;
    if (depth) {
        void**node=expand_node(main,set);
        int bits=child_bits(main,depth);
        int branch=e>>bits;
        e=ELEM_MASK(e,bits);
        node[branch]=clear_bit(main,node[branch],depth-1,e);
        return simplify_node(main,node);
    } else {
        word_t *node=expand_leaf(main,set);
        int word=e>>WORD_CLASS;
        e=ELEM_MASK(e,WORD_CLASS);
        node[word]=node[word]&(~(WORD_ONE<<e));
        return simplify_leaf(main,node);
    }
}

void bitset_clear(bitset_t set,element_t e){
    while(!(e<set->set_size)){
        expand(set);
    }
    set->set=clear_bit(set,set->set,set->depth,e);
}

static int testbit(bitset_t main,void *set,int depth,element_t e){
	if (set==ALL_ONES) return 1;
	if (set==ALL_ZERO) return 0;
	if (depth) {
        void**node=(void**)set;
        int bits=child_bits(main,depth);
        int branch=e>>bits;
        e=ELEM_MASK(e,bits);
        return testbit(main,node[branch],depth-1,e);
    } else {
        word_t *node=(word_t*)set;
        int word=e>>WORD_CLASS;
        e=ELEM_MASK(e,WORD_CLASS);
        return (node[word]&(WORD_ONE<<e))?1:0;
    }
}

int bitset_test(bitset_t set,element_t e){
    if (e<set->set_size) {
        return testbit(set,set->set,set->depth,e);
    } else {
        return (set->default_value==ALL_ONES);
    }
}

/** Find the next bit set. */
static element_t find_next_set(bitset_t main,void *set,int depth,element_t e){
    if (set==ALL_ONES) return e;
	if (depth) {
        int bits=child_bits(main,depth);
	    element_t branch;
	    if (set==ALL_ZERO) {
	        branch=main->node_size;
	        e=0;
	    } else {
            void**node=(void**)set;
            branch=e>>bits;
            e=ELEM_MASK(e,bits);
            element_t size=ELEM_ONE<<bits;
            while(branch<main->node_size){
                e=find_next_set(main,node[branch],depth-1,e);
                if (e<size) {
                    break;
                }
                e=0;
                branch++;
            }
        }
        return (branch<<bits) + e;
    } else {
	    element_t word;
	    if (set==ALL_ZERO) {
	        word=main->base_words;
	        e=0;
	    } else {
	        word_t *node=(word_t*)set;
            word=e>>WORD_CLASS;
            e=ELEM_MASK(e,WORD_CLASS);
            while(word<main->base_words){
                if (node[word]&(WORD_ONE<<e)) {
                    break;
                }
                e++;
                if (e>>WORD_CLASS){
                    e=0;
                    word++;
                }
            }
        }
        return (word<<WORD_CLASS) + e;
    }
}

int bitset_next_set(bitset_t set,element_t *e){
    if ((*e)<set->set_size){
        element_t next=find_next_set(set,set->set,set->depth,*e);
        if (next<set->set_size) {
            *e=next;
            return 1;
        }
    }
    if (set->default_value==ALL_ONES) {
        *e=set->set_size;
        return 1;
    } else {
        return 0;
    }
}

static void print(FILE*f,bitset_t set,void* ptr,int depth){
	int i,seg;
	for(i=set->depth;i>depth;i--) fprintf(f,"|");
	if (ptr==ALL_ONES) {
		fprintf(f,"+ALL_ONES\n");
	} else if (ptr==ALL_ZERO) {
		fprintf(f,"+ALL_ZERO\n");
	} else if (depth) {
		fprintf(f,"++\n");
		depth--;
		for(seg=0;seg<(1<<set->node_class);seg++) print(f,set,((void**)ptr)[seg],depth);
	} else {
		fprintf(f,"+");
		for(seg=0;seg<(1<<(set->base_class-WORD_CLASS));seg++){
			for(i=0;i<1<<WORD_CLASS;i++){
				fprintf(f,"%s",(((word_t*)ptr)[seg]&(((word_t)1)<<i))?"1":"0");
			}
		}
		fprintf(f,"\n");
	}
}

void bitset_fprint(FILE*f,bitset_t set) {
	print(f,set,set->set,set->depth);
	fprintf(f,"+%s\n",(set->default_value==ALL_ONES)?"ALL_ONES":"ALL_ZERO");
}

static void* invert(bitset_t main,void* set,int depth){
    if (set==ALL_ONES) return ALL_ZERO;
    if (set==ALL_ZERO) return ALL_ONES;
    if (depth) {
        void**node=expand_node(main,set);
        for(unsigned int i=0;i<main->node_size;i++){
            node[i]=invert(main,node[i],depth-1);
        }
        return node;
    } else {
        word_t *node=expand_leaf(main,set);
        for(unsigned int i=0;i<main->base_words;i++){
            node[i]=~node[i];
        }
        return node;
    }
}

void bitset_invert(bitset_t set){
    set->default_value=invert(set,set->default_value,-1);
    set->set=invert(set,set->set,set->depth);
}

/* this function is not efficient!, rewrite it! */
void bitset_set_range(bitset_t set,element_t low,element_t high){
    for(element_t i=low;i<=high;i++){
        bitset_set(set,i);
    }
}

