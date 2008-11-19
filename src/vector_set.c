#include <malloc.h>

#include "vector_set.h"
#include "runtime.h"
#include "aterm2.h"


int save_cache=1;
int disable_cache=0;
int simple_project=0;

static ATerm emptyset=NULL;

ATerm singleton(ATerm *a,int len);

void count_set(ATerm set,long *nodes,long long *elements);

void create_numbering(ATerm set,ATermIndexedSet *idx,long long **count);

long long get_number(ATerm set,ATermIndexedSet idx,long long *count,ATerm *a,int len);

ATerm set_add(ATerm set,ATerm *a,int len,ATbool *new);

ATbool set_put(ATerm *set,ATerm *a,int len);

ATerm set_union(ATerm s1,ATerm s2);

ATbool set_member(ATerm set,ATerm *a,int len);

int set_enum(ATerm set,ATerm *a,int len,int (*callback)(ATerm *,int));

int set_enum_match(ATerm set,ATerm *a,int len,ATerm*pattern,int *proj,int p_len,int (*callback)(ATerm *,int));

ATerm set_minus(ATerm a,ATerm b);

ATerm set_project(ATerm set,int *proj,int len);

ATerm set_reach(ATerm set,int set_len,ATerm trans,int *proj,int p_len);

int print_array(ATerm *a,int len);

void set_init();

void set_zip(ATerm in1,ATerm in2,ATerm *out1,ATerm *out2);

void set_reset_ct();




static AFun cons;
static AFun zip,min,sum,pi,reach;
static ATerm atom;

static ATermTable global_ct=NULL;

//static int ATcmp(ATerm t1,ATerm t2){
//	return (((int)t1)>>1)-(((int)t2)>>1);
//}

#define ATcmp ATcompare
//define ATcmp(t1,t2) (((long)t1)-((long)t2))

#define HASH_INIT 4096
#define HASH_LOAD 75

struct vector_domain {
	int size;
};

vdom_t vdom_create(int n){
	vdom_t dom=(vdom_t)RTmalloc(sizeof(struct vector_domain));
	dom->size=n;
	if (!emptyset) set_init();
	return dom;
}

struct vector_set {
	vdom_t dom;
	ATerm set;
	int p_len;
	int proj[];
};

struct vector_relation {
	vdom_t dom;
	ATerm rel;
	int p_len;
	int proj[];
};

vset_t vset_create(vdom_t dom,int k,int* proj){
	vset_t set=(vset_t)RTmalloc(sizeof(struct vector_set)+k*sizeof(int));
	set->dom=dom;
	set->set=emptyset;
	ATprotect(&set->set);
	set->p_len=k;
	for(int i=0;i<k;i++) set->proj[i]=proj[i];
	return set;
}

vrel_t vrel_create(vdom_t dom,int k,int* proj){
	vrel_t rel=(vrel_t)RTmalloc(sizeof(struct vector_relation)+k*sizeof(int));
	rel->dom=dom;
	rel->rel=emptyset;
	ATprotect(&rel->rel);
	rel->p_len=k;
	for(int i=0;i<k;i++) rel->proj[i]=proj[i];
	return rel;
}


void vset_add(vset_t set,int* e){
	int N=set->p_len?set->p_len:set->dom->size;
	ATerm vec[N];
	for(int i=0;i<N;i++) vec[i]=(ATerm)ATmakeInt(e[i]);
	set->set=set_add(set->set,vec,N,NULL);
}


int vset_member(vset_t set,int* e){
	int N=set->p_len?set->p_len:set->dom->size;
	ATerm vec[N];
	for(int i=0;i<N;i++) vec[i]=(ATerm)ATmakeInt(e[i]);
	return set_member(set->set,vec,N);
}

int vset_is_empty(vset_t set){
	return ATisEqual(set->set,emptyset);
}

void vset_clear(vset_t set){
	set->set=emptyset;
}

void vset_copy(vset_t dst,vset_t src){
	//Maybe we should check if the lengths match!
	dst->set=src->set;
}

static vset_element_cb global_cb;
static void* global_context;

static int vset_enum_wrap(ATerm *a,int len){
	int vec[len];
	for(int i=0;i<len;i++) vec[i]=ATgetInt((ATermInt)a[i]);
	global_cb(global_context,vec);
	return 0;
}

void vset_enum(vset_t set,vset_element_cb cb,void* context){
	int N=set->p_len?set->p_len:set->dom->size;
	ATerm vec[N];
	global_cb=cb;
	global_context=context;
	set_enum(set->set,vec,N,vset_enum_wrap);
}

void vset_count(vset_t set,long *nodes,long long *elements){
	count_set(set->set,nodes,elements);
}

void vset_union(vset_t dst,vset_t src){
	dst->set=set_union(dst->set,src->set);
}

void vset_minus(vset_t dst,vset_t src){
	dst->set=set_minus(dst->set,src->set);
}

void vset_next(vset_t dst,vset_t src,vrel_t rel){
	dst->set=set_reach(src->set,src->dom->size,rel->rel,rel->proj,rel->p_len);
}

void vset_project(vset_t dst,vset_t src){
	dst->set=set_project(src->set,dst->proj,dst->p_len);
}

void vset_zip(vset_t dst,vset_t src){
	set_zip(dst->set,src->set,&dst->set,&src->set);
}

void vrel_add(vrel_t rel,int* src,int *dst){
	int N=rel->p_len?rel->p_len:rel->dom->size;
	ATerm vec[2*N];
	for(int i=0;i<N;i++) {
		vec[i+i]=(ATerm)ATmakeInt(src[i]);
		vec[i+i+1]=(ATerm)ATmakeInt(dst[i]);
	}
	rel->rel=set_add(rel->rel,vec,2*N,NULL);
}

/***************************/

static ATermIndexedSet count_is;
static long node_count;
static long long *elem_count;
static long elem_size;

long long get_number(ATerm set,ATermIndexedSet idx,long long *count,ATerm *a,int len){
	if (len==0) {
		while(ATgetAFun(set)==cons) set=ATgetArgument(set,2);
		if(ATisEqual(set,atom)) {
			return 0;
		} else {
			ATerror("atom not a member %t",set);
		}
	} else {
		while(ATgetAFun(set)==cons) {
			if (ATisEqual(a[0],ATgetArgument(set,0))) {
				long node=ATindexedSetGetIndex(idx,ATgetArgument(set,2));
				if (node<0) ATerror("set and index do not match");
				return (get_number(ATgetArgument(set,1),idx,count,a+1,len-1)+count[node]);
			}
			set=ATgetArgument(set,2);
		}
		ATerror("%t not a member prefix",a[0]);
	}
	return 0;
}

static long long count_set_2(ATerm set){
	ATbool new;
	long idx=ATindexedSetPut(count_is,(ATerm)set,&new);
	if(new){
		node_count++;
		if (idx>=elem_size){
			elem_size=elem_size+(elem_size>>1);
			elem_count=realloc(elem_count,elem_size*sizeof(long long));
			//ATwarning("resize %d %d %x",idx,elem_size,elem_count);
		}
		if (ATgetAFun(set)==cons) {
			long long c=count_set_2(ATgetArgument(set,1))+count_set_2(ATgetArgument(set,2));
			return elem_count[idx]=c;
		} else {
			return elem_count[idx]=(ATisEqual(set,atom))?1:0;
		}
	} else {
		return elem_count[idx];
	}
}

void create_numbering(ATerm set,ATermIndexedSet *idx,long long **count){
	count_is=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
	elem_count=malloc(HASH_INIT*sizeof(long long));
	elem_size=HASH_INIT;
	node_count=0;
	count_set_2(set);
	*idx=count_is;
	count_is=NULL;
	*count=elem_count;
	elem_count=NULL;
}

void count_set(ATerm set,long *nodes,long long *elements){
	count_is=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
	elem_count=malloc(HASH_INIT*sizeof(long long));
	elem_size=HASH_INIT;
	node_count=0;
	*elements=count_set_2(set);
	ATindexedSetDestroy(count_is);
	free(elem_count);
	*nodes=node_count;
}

ATerm singleton(ATerm *a,int len){
	if (len==0) {
		return atom;
	} else {
		return (ATerm)ATmakeAppl3(cons,a[0],singleton(a+1,len-1),emptyset);
	}
}

ATbool set_member(ATerm set,ATerm *a,int len){
	if (len==0) {
		while(ATgetAFun(set)==cons) set=ATgetArgument(set,2);
		return ATisEqual(set,atom);
	} else {
		while(ATgetAFun(set)==cons) {
			if (ATisEqual(a[0],ATgetArgument(set,0))) {
				return set_member(ATgetArgument(set,1),a+1,len-1);
			}
			set=ATgetArgument(set,2);
		}
		return ATfalse;
	}
}

ATerm set_add(ATerm set,ATerm *a,int len,ATbool *new){
	if (set_member(set,a,len)) {
		if(new) *new=ATfalse;
		return set;
	} else {
		if(new) *new=ATtrue;
		return set_union(set,singleton(a,len));
	}
}

ATbool set_put(ATerm *set,ATerm *a,int len){
	if(len==0) {
		if (ATgetAFun(*set)==cons){
			ATerm subset=ATgetArgument(*set,2);
			if(set_put(&subset,a,len)){
				*set=(ATerm)ATmakeAppl3(cons,ATgetArgument(*set,0),ATgetArgument(*set,1),subset);
				return ATtrue;
			} else {
				return ATfalse;
			}
		} else {
			if(ATisEqual(*set,atom)){
				return ATfalse;
			} else {
				*set=atom;
				return ATtrue;
			}
		}
	} else {
		if (ATgetAFun(*set)==cons){
			int c=ATcmp(a[0],ATgetArgument(*set,0));
			if (c<0) {
				*set=(ATerm)ATmakeAppl3(cons,a[0],singleton(a+1,len-1),*set);
				return ATtrue;
			} else if (c==0) {
				ATerm subset=ATgetArgument(*set,1);
				if(set_put(&subset,a+1,len-1)){
					*set=(ATerm)ATmakeAppl3(cons,a[0],subset,ATgetArgument(*set,2));
					return ATtrue;
				} else {
					return ATfalse;
				}
			} else {
				ATerm subset=ATgetArgument(*set,2);
				if(set_put(&subset,a,len)){
					*set=(ATerm)ATmakeAppl3(cons,ATgetArgument(*set,0),ATgetArgument(*set,1),subset);
					return ATtrue;
				} else {
					return ATfalse;
				}
			}
		} else {
			*set=(ATerm)ATmakeAppl3(cons,a[0],singleton(a+1,len-1),*set);
			return ATtrue;
		}
	}
}


static inline ATerm set_union_2(ATermTable ct,ATerm s1,ATerm s2);
static ATerm set_union_3(ATermTable ct,ATerm s1,ATerm s2){
	if (ct) {
		ATerm key=(ATerm)ATmakeAppl2(sum,s1,s2);
		ATerm res=ATtableGet(ct,key);
		if(res) return res;
		res=set_union_2(ct,s1,s2);
		ATtablePut(ct,key,res);
		return res;
	} else {
		return set_union_2(ct,s1,s2);
	}
}

static inline ATerm set_union_2(ATermTable ct,ATerm s1,ATerm s2){
	if(ATgetAFun(s1)==cons){
		if (ATgetAFun(s2)==cons){
			int c=ATcmp(ATgetArgument(s1,0),ATgetArgument(s2,0));
			if (c<0) {
			return (ATerm)ATmakeAppl3(
				cons,
				ATgetArgument(s1,0),
				ATgetArgument(s1,1),
				save_cache?set_union_2(ct,ATgetArgument(s1,2),s2):set_union_3(ct,ATgetArgument(s1,2),s2)
				);
			} else if (c>0) {
			return (ATerm)ATmakeAppl3(
				cons,
				ATgetArgument(s2,0),
				ATgetArgument(s2,1),
				save_cache?set_union_2(ct,s1,ATgetArgument(s2,2)):set_union_3(ct,s1,ATgetArgument(s2,2))
				);
			} else {
			return (ATerm)ATmakeAppl3(
				cons,
				ATgetArgument(s1,0),
				set_union_3(ct,ATgetArgument(s1,1),ATgetArgument(s2,1)),
				save_cache?set_union_2(ct,ATgetArgument(s1,2),ATgetArgument(s2,2))
					:set_union_3(ct,ATgetArgument(s1,2),ATgetArgument(s2,2))
				);
			}
		} else {
			return (ATerm)ATmakeAppl3(
				cons,
				ATgetArgument(s1,0),
				ATgetArgument(s1,1),
				set_union_2(ct,ATgetArgument(s1,2),s2)
				);
		}
	} else {
		if (ATgetAFun(s2)==cons){
			return (ATerm)ATmakeAppl3(
				cons,
				ATgetArgument(s2,0),
				ATgetArgument(s2,1),
				set_union_2(ct,s1,ATgetArgument(s2,2))
				);
		} else {
			if (ATisEqual(s1,atom)||ATisEqual(s2,atom)) return atom; else return emptyset;
		}
	}
}


static int set_enum_2(ATerm set,ATerm *a,int len,int (*callback)(ATerm *,int),int ofs){
	int tmp;
	while(ATgetAFun(set)==cons){
		if (ofs<len){
			a[ofs]=ATgetArgument(set,0);
			tmp=set_enum_2(ATgetArgument(set,1),a,len,callback,ofs+1);
			if (tmp) return tmp;
		}
		set=ATgetArgument(set,2);
	}
	if (ATisEqual(set,atom)) {
		return callback(a,ofs);
	}
	return 0;
}

static int set_enum_match_2(ATermIndexedSet dead,ATerm set,ATerm *a,int len,ATerm*pattern,int *proj,int p_len,
	int (*callback)(ATerm *,int),int ofs
){
	int tmp;
	if (ATindexedSetGetIndex(dead,set)>=0) return 0;
	if (ATgetAFun(set)==cons){
		int live=0;
		if (ofs<len){
			if (p_len && proj[0]==ofs){
				ATerm el=ATgetArgument(set,0);
				if (ATisEqual(pattern[0],el)){
					a[ofs]=el;
					tmp=set_enum_match_2(dead,ATgetArgument(set,1),a,len,pattern+1,proj+1,p_len-1,callback,ofs+1);
					if (tmp<0) return tmp;
					if (tmp) live=1;
				}
			} else {
				a[ofs]=ATgetArgument(set,0);
				tmp=set_enum_match_2(dead,ATgetArgument(set,1),a,len,pattern,proj,p_len,callback,ofs+1);
				if (tmp<0) return tmp;
				if(tmp) live=1;
			}
		}
		if(ofs<=len){
			tmp=set_enum_match_2(dead,ATgetArgument(set,2),a,len,pattern,proj,p_len,callback,ofs);
			if(tmp<0) return tmp;
			if(tmp) live=1;
		}
		if (!live){
			ATindexedSetPut(dead,set,NULL);
		}
		return live;
	}
	if (ATisEqual(set,atom)&&ofs==len) {
		return callback(a,ofs)?-1:1;
	} else {
		ATindexedSetPut(dead,set,NULL);
		return 0;
	}
}

int set_enum(ATerm set,ATerm *a,int len,int (*callback)(ATerm *,int)){
	return set_enum_2(set,a,len,callback,0);
}

int set_enum_match(ATerm set,ATerm *a,int len,ATerm*pattern,int *proj,int p_len,int (*callback)(ATerm *,int)){
	ATermIndexedSet dead_branches=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
	int result=set_enum_match_2(dead_branches,set,a,len,pattern,proj,p_len,callback,0);
	ATindexedSetDestroy(dead_branches);
	return (result<0)?1:0;
}

int print_array(ATerm *a,int len){
	ATprintf("[");
	if (len) {
		int i;
		ATprintf("%t",a[0]);
		for(i=1;i<len;i++) ATprintf(",%t",a[i]);
	}
	ATprintf("]\n");
	return 0;
}


static ATerm MakeCons(ATerm e,ATerm es,ATerm tl){
	if (ATisEqual(es,emptyset)) return tl;
	return (ATerm)ATmakeAppl3(cons,e,es,tl);
}


static ATerm set_project_2(ATermTable ct,ATerm set,int ofs,int *proj,int len);
static ATerm set_project_3(ATermTable ct,ATerm set,int ofs,int *proj,int len){
	if(ct){
		ATerm key=(ATerm)ATmakeAppl2(pi,set,(ATerm)ATmakeInt(len));
		ATerm res=ATtableGet(ct,key);
		if (res) return res;
		res=set_project_2(ct,set,ofs,proj,len);
		ATtablePut(ct,key,res);
		return res;
	} else {
		return set_project_2(ct,set,ofs,proj,len);
	}
}

static ATerm set_project_2(ATermTable ct,ATerm set,int ofs,int *proj,int len){
	ATerm res=emptyset;
	if (len==0) {
		if (ATisEqual(set,emptyset)) return emptyset; else return atom;
	}
	if (ofs==proj[0]) {
		if (ATisEqual(set,emptyset)||ATisEqual(set,atom)) return emptyset;
		return MakeCons(ATgetArgument(set,0),
			set_project_3(ct,ATgetArgument(set,1),ofs+1,proj+1,len-1),
			save_cache?set_project_2(ct,ATgetArgument(set,2),ofs,proj,len)
				:set_project_3(ct,ATgetArgument(set,2),ofs,proj,len));
	}
	for(;ATgetAFun(set)==cons;set=ATgetArgument(set,2)){
		res=set_union_3(ct,res,set_project_3(ct,ATgetArgument(set,1),ofs+1,proj,len));
	}
	return res;
}

static ATerm copy_level(ATermTable ct,ATerm set,int set_len,ATerm trans,int *proj,int p_len,int ofs);
static ATerm trans_level(ATermTable ct,ATerm set,int set_len,ATerm trans,int *proj,int p_len,int ofs);
static ATerm set_reach_2(ATermTable ct,ATerm set,int set_len,ATerm trans,int *proj,int p_len,int ofs);

static inline ATerm set_reach_3(ATermTable ct,ATerm set,int set_len,ATerm trans,int *proj,int p_len,int ofs){
	if(ct){
		ATerm key=(ATerm)ATmakeAppl2(reach,set,trans);
		ATerm res=ATtableGet(ct,key);
		if(res)return res;
		res=set_reach_2(ct,set,set_len,trans,proj,p_len,ofs);
		ATtablePut(ct,key,res);
		return res;
	} else {
		return set_reach_2(ct,set,set_len,trans,proj,p_len,ofs);
	}
}


static ATerm apply_reach(ATermTable ct,ATerm set,int set_len,ATerm trans,int *proj,int p_len,int ofs){
	int c;
	ATerm res=emptyset;
	for(;(ATgetAFun(set)==cons)&&(ATgetAFun(trans)==cons);){
		c=ATcmp(ATgetArgument(set,0),ATgetArgument(trans,0));
		if (c<0) {
			set=ATgetArgument(set,2);
		} else if (c>0) {
			trans=ATgetArgument(trans,2);
		} else {
			if(save_cache){
				res=set_union_2(ct,res,
					trans_level(ct,
						ATgetArgument(set,1),set_len,
						ATgetArgument(trans,1),proj,p_len,ofs)
					);
			} else {
				res=set_union_3(ct,res,
					trans_level(ct,
						ATgetArgument(set,1),set_len,
						ATgetArgument(trans,1),proj,p_len,ofs)
					);
			}
			set=ATgetArgument(set,2);
			trans=ATgetArgument(trans,2);
		}
	}
	return res;
}

static ATerm set_reach_2(ATermTable ct,ATerm set,int set_len,ATerm trans,int *proj,int p_len,int ofs){
	if (set_len==ofs){
		while (ATgetAFun(set)==cons) set=ATgetArgument(set,2);
		return set;
	}
	if (p_len==0){
		return set;//project_depth(set,set_len-ofs);
	}
	if (proj[0]==ofs){
		return apply_reach(ct,set,set_len,trans,proj,p_len,ofs);
	}
	return copy_level(ct,set,set_len,trans,proj,p_len,ofs);
}

static ATerm copy_level(ATermTable ct,ATerm set,int set_len,ATerm trans,int *proj,int p_len,int ofs){
	if (ATgetAFun(set)==cons) {
		return MakeCons(ATgetArgument(set,0),
			set_reach_3(ct,ATgetArgument(set,1),set_len,trans,proj,p_len,ofs+1),
			copy_level(ct,ATgetArgument(set,2),set_len,trans,proj,p_len,ofs));
	} else {
		return emptyset;
	}
}

static ATerm trans_level(ATermTable ct,ATerm set,int set_len,ATerm trans,int *proj,int p_len,int ofs){
	if (ATgetAFun(trans)==cons) {
		return MakeCons(ATgetArgument(trans,0),
			set_reach_3(ct,set,set_len,ATgetArgument(trans,1),proj+1,p_len-1,ofs+1),
			trans_level(ct,set,set_len,ATgetArgument(trans,2),proj,p_len,ofs));
	} else {
		return emptyset;
	}
}

static ATerm set_minus_2(ATermTable ct,ATerm a,ATerm b);
static ATerm set_minus_3(ATermTable ct,ATerm a,ATerm b){
	if(ct){
		ATerm key=(ATerm)ATmakeAppl2(min,a,b);
		ATerm res=ATtableGet(ct,key);
		if(res) return res;
		res=set_minus_2(ct,a,b);
		ATtablePut(ct,key,res);
		return res;
	} else {
		return set_minus_2(ct,a,b);
	}
}


static ATerm set_minus_2(ATermTable ct,ATerm a,ATerm b){
	if (ATisEqual(b,emptyset)||ATisEqual(a,emptyset)) return a;
	if (ATgetAFun(a)==cons) {
		if (ATgetAFun(b)==cons) {
			int c=ATcmp(ATgetArgument(a,0),ATgetArgument(b,0));
			if (c<0) {
				return MakeCons(ATgetArgument(a,0),
					ATgetArgument(a,1),
					save_cache?set_minus_2(ct,ATgetArgument(a,2),b):set_minus_3(ct,ATgetArgument(a,2),b)
					);
			}
			if (c>0) {
				return set_minus_2(ct,a,ATgetArgument(b,2));
			}
			return MakeCons(ATgetArgument(a,0),
				set_minus_3(ct,ATgetArgument(a,1),ATgetArgument(b,1)),
				save_cache?set_minus_2(ct,ATgetArgument(a,2),ATgetArgument(b,2))
					:set_minus_3(ct,ATgetArgument(a,2),ATgetArgument(b,2))
				);
		} else {
			return MakeCons(ATgetArgument(a,0),
				ATgetArgument(a,1),
				set_minus_2(ct,ATgetArgument(a,2),b));
		}
	} else {
		while(ATgetAFun(b)==cons) b=ATgetArgument(b,2);
		if (ATisEqual(b,atom)) return emptyset; else return atom;
	}
}

static void set_zip_2(ATermTable ct,ATerm in1,ATerm in2,ATerm *out1,ATerm *out2);

static void set_zip_3(ATermTable ct,ATerm in1,ATerm in2,ATerm *out1,ATerm *out2){
	if(ct){
		ATerm key=(ATerm)ATmakeAppl2(zip,in1,in2);
		ATerm res=ATtableGet(ct,key);
		if(res){
			*out1=ATgetArgument(res,0);
			*out2=ATgetArgument(res,1);
		} else {
			set_zip_2(ct,in1,in2,out1,out2);
			ATtablePut(ct,key,(ATerm)ATmakeAppl2(zip,*out1,*out2));
		}
	} else {
		set_zip_2(ct,in1,in2,out1,out2);
	}
}

static void set_zip_2(ATermTable ct,ATerm in1,ATerm in2,ATerm *out1,ATerm *out2){
	if (ATgetAFun(in1)==cons) {
		if (ATgetAFun(in2)==cons) {
			int c=ATcmp(ATgetArgument(in1,0),ATgetArgument(in2,0));
			if(c<0){
				ATerm t1=NULL,t2=NULL;
				if (save_cache) {
					set_zip_2(ct,ATgetArgument(in1,2),in2,&t1,&t2);
				} else {
					set_zip_3(ct,ATgetArgument(in1,2),in2,&t1,&t2);
				}
				*out1=MakeCons(ATgetArgument(in1,0),ATgetArgument(in1,1),t1);
				*out2=t2;
			} else if (c>0){
				ATerm t1=NULL,t2=NULL;
				if (save_cache) {
					set_zip_2(ct,in1,ATgetArgument(in2,2),&t1,&t2);
				} else {
					set_zip_3(ct,in1,ATgetArgument(in2,2),&t1,&t2);
				}
				*out1=MakeCons(ATgetArgument(in2,0),ATgetArgument(in2,1),t1);
				*out2=MakeCons(ATgetArgument(in2,0),ATgetArgument(in2,1),t2);
			} else {
				ATerm t1=NULL,t2=NULL,t3=NULL,t4=NULL;
				set_zip_3(ct,ATgetArgument(in1,1),ATgetArgument(in2,1),&t1,&t2);
				if (save_cache) {
					set_zip_2(ct,ATgetArgument(in1,2),ATgetArgument(in2,2),&t3,&t4);
				} else {
					set_zip_3(ct,ATgetArgument(in1,2),ATgetArgument(in2,2),&t3,&t4);
				}
				*out1=MakeCons(ATgetArgument(in1,0),t1,t3);
				*out2=MakeCons(ATgetArgument(in1,0),t2,t4);
			}
		} else if (ATisEqual(in2,emptyset)){
			*out1=in1;
			*out2=emptyset;
		} else {
			ATerm t1=NULL,t2=NULL;
			set_zip_2(ct,ATgetArgument(in1,2),in2,&t1,&t2);
			*out1=MakeCons(ATgetArgument(in1,0),ATgetArgument(in1,1),t1);
			*out2=t2;
		}
	} else {
		if(ATisEqual(in1,emptyset)){
			*out1=in2;
			*out2=in2;
		} else if (ATgetAFun(in2)==cons) {
			ATerm t1=NULL,t2=NULL;
			set_zip_2(ct,in1,ATgetArgument(in2,2),&t1,&t2);
			*out1=MakeCons(ATgetArgument(in2,0),ATgetArgument(in2,1),t1);
			*out2=MakeCons(ATgetArgument(in2,0),ATgetArgument(in2,1),t2);
		} else {
			*out1=atom;
			*out2=emptyset;
		}
	}
}

static ATerm set_cut_off(ATermTable ct,ATerm set,int depth){
	if (ATgetAFun(set)==cons) {
		ATerm res=ATtableGet(ct,set);
		if(res) return res;
		if (depth==0) {
			res=MakeCons(ATgetArgument(set,0),atom,set_cut_off(ct,ATgetArgument(set,2),depth));
		} else {
			res=MakeCons(ATgetArgument(set,0),
					set_cut_off(ct,ATgetArgument(set,1),depth-1),
					set_cut_off(ct,ATgetArgument(set,2),depth));
		}
		ATtablePut(ct,set,res);
		return res;
	} else {
		return emptyset;
	}
}

static ATerm set_cut_level(ATermTable ct,ATerm set,int depth){
	if (ATgetAFun(set)==cons) {
		ATerm res=ATtableGet(ct,set);
		if(res) return res;
		if (depth==0) {
			res=set_union(ATgetArgument(set,1),set_cut_level(ct,ATgetArgument(set,2),depth));
		} else {
			res=MakeCons(ATgetArgument(set,0),
					set_cut_level(ct,ATgetArgument(set,1),depth-1),
					set_cut_level(ct,ATgetArgument(set,2),depth));
		}
		ATtablePut(ct,set,res);
		return res;
	} else {
		return emptyset;
	}
}

ATerm set_project(ATerm set,int *proj,int len){
	if (simple_project){
		int i,j;
		ATermTable ct=ATtableCreate(HASH_INIT,HASH_LOAD);
		set=set_cut_off(ct,set,proj[len-1]);
		for(i=len-1;i>0;i--){
			for(j=proj[i]-1;j>proj[i-1];j--){
				ATtableReset(ct);
				set=set_cut_level(ct,set,j);
			}
		}
		for(j=proj[0]-1;j>=0;j--){
			ATtableReset(ct);
			set=set_cut_level(ct,set,j);
		}
		ATtableDestroy(ct);
		return set;
	} else {
		ATermTable ct=NULL;
		//ct=ATtableCreate(HASH_INIT,HASH_LOAD);
		ct=global_ct;
		set=set_project_2(ct,set,0,proj,len);
		//ATtableDestroy(ct);
		if (ct) ATtableReset(ct);
		return set;
	}
}

ATerm set_reach(ATerm set,int set_len,ATerm trans,int *proj,int p_len){
	//ATerm stripped;
	ATermTable ct=NULL;
	//ct=ATtableCreate(HASH_INIT,HASH_LOAD);
	ct=global_ct;
	//for(stripped=emptyset;!ATisEqual(trans,emptyset);trans=ATgetArgument(trans,2)){
	//	stripped=set_union_2(ct,stripped,ATgetArgument(trans,1));
	//}
	set=set_reach_2(ct,set,set_len,trans,proj,p_len,0);
	//ATtableDestroy(ct);
	if (ct) ATtableReset(ct);
	return set;
}

ATerm set_minus(ATerm s1,ATerm s2){
	ATermTable ct=NULL;
	//ct=ATtableCreate(HASH_INIT,HASH_LOAD);
	ct=global_ct;
	s1=set_minus_2(ct,s1,s2);
	//ATtableDestroy(ct);
	if (ct) ATtableReset(ct);
	return s1;
}

void set_zip(ATerm in1,ATerm in2,ATerm *out1,ATerm *out2){
	ATermTable ct=NULL;
	//ct=ATtableCreate(HASH_INIT,HASH_LOAD);
	ct=global_ct;
	set_zip_2(ct,in1,in2,out1,out2);
	//ATtableDestroy(ct);
	if (ct) ATtableReset(ct);
}
/*
ATerm set_project(ATerm set,int *proj,int len){
	ATermTable ct=NULL;
	//ct=ATtableCreate(HASH_INIT,HASH_LOAD);
	ct=global_ct;
	set=set_project_2(ct,set,0,proj,len);
	//ATtableDestroy(ct);
	if (ct) ATtableReset(ct);
	return set;
}
*/
ATerm set_union(ATerm s1,ATerm s2){
	ATermTable ct=NULL;
	//ct=ATtableCreate(HASH_INIT,HASH_LOAD);
	ct=global_ct;
	s1=set_union_2(ct,s1,s2);
	//ATtableDestroy(ct);
	if (ct) ATtableReset(ct);
	return s1;
}

void set_init(){
	ATprotect(&emptyset);
	ATprotect(&atom);
	emptyset=ATmake("VSET_E");
	atom=ATmake("VSET_A");
	cons=ATmakeAFun("VSET_C",3,ATfalse);
	ATprotectAFun(cons);
	zip=ATmakeAFun("VSET_ZIP",2,ATfalse);
	ATprotectAFun(zip);
	min=ATmakeAFun("VSET_MINUS",2,ATfalse);
	ATprotectAFun(min);
	sum=ATmakeAFun("VSET_UNION",2,ATfalse);
	ATprotectAFun(sum);
	pi=ATmakeAFun("VSET_PI",2,ATfalse);
	ATprotectAFun(pi);
	reach=ATmakeAFun("VSET_REACH",2,ATfalse);
	ATprotectAFun(reach);
	set_reset_ct();
}


void set_reset_ct(){
	if (global_ct!=NULL) {
		ATtableDestroy(global_ct);
	}
	if (disable_cache) {
		global_ct=NULL;
	} else {
		global_ct=ATtableCreate(1024,75);
	}
}

