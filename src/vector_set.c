#include <stdlib.h>

#include "vector_set.h"
#include "runtime.h"
#include "aterm2.h"

static ATerm emptyset=NULL;

ATbool set_member(ATerm set,ATerm *a);

ATerm singleton(ATerm *a,int len);

ATerm set_add(ATerm set,ATerm *a,int len,ATbool *new);

ATerm set_union(ATerm s1,ATerm s2);

ATerm set_minus(ATerm a,ATerm b);

void set_zip(ATerm in1,ATerm in2,ATerm *out1,ATerm *out2);

ATerm set_project(ATerm set,int *proj,int len);

ATerm set_reach(ATerm set,ATerm trans,int *proj,int p_len);

int set_enum(ATerm set,ATerm *a,int len,int (*callback)(ATerm *,int));

void set_reset_ct();

void set_init();

void count_set(ATerm set,long *nodes,long long *elements);

/* currently not used:

long long get_number(ATerm set,ATermIndexedSet idx,long long *count,ATerm *a,int len);

int set_enum_match(ATerm set,ATerm *a,int len,ATerm*pattern,int *proj,int p_len,int (*callback)(ATerm *,int));

int print_array(ATerm *a,int len);

void create_numbering(ATerm set,ATermIndexedSet *idx,long long **count);

ATbool set_put(ATerm *set,ATerm *a,int len);

*/


static AFun cons;
static AFun zip,min,sum,pi,reach;
static ATerm atom;

static ATermTable global_ct=NULL;

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
	return set_member(set->set,vec);
}

int vset_is_empty(vset_t set){
	return ATisEqual(set->set,emptyset);
}

int vset_equal(vset_t set1,vset_t set2){
	return ATisEqual(set1->set,set2->set);
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
	dst->set=set_reach(src->set,rel->rel,rel->proj,rel->p_len);
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

ATbool set_member(ATerm set,ATerm *a){
  if (set==emptyset) return ATfalse;
  else if (set==atom) return ATtrue;
  else { 
    int c = ATcmp(a[0],ATgetArgument(set,0));
    if (c==0)
      return set_member(ATgetArgument(set,1),a+1);
    else if (c>0)
      return set_member(ATgetArgument(set,2),a);
    else return ATfalse;
  }
}

static inline ATerm Cons(ATerm e,ATerm es,ATerm tl) {
  return (ATerm)ATmakeAppl3(cons,e,es,tl);
}

static ATerm MakeCons(ATerm e,ATerm es,ATerm tl){
  if (ATisEqual(es,emptyset)) return tl;
  return Cons(e,es,tl);
}

ATerm singleton(ATerm *a,int len){
  if (len==0)
    return atom;
  else
    return Cons(a[0],singleton(a+1,len-1),emptyset);
}

ATerm set_add(ATerm set,ATerm *a,int len,ATbool *new){
  if (set==emptyset) {if (new) *new=ATtrue; return singleton(a,len);}
  else if (set==atom) {if (new) *new=ATfalse; return atom;}
  else {
    ATerm x=ATgetArgument(set,0);
    int c = ATcmp(a[0],x);
    if (c<0) {
      if (new) *new=ATtrue;
      return Cons(a[0],singleton(a+1,len-1),set);
    }
    else {
      ATerm set1 = ATgetArgument(set,1);
      ATerm set2 = ATgetArgument(set,2);
      if (c==0) 
	return Cons(x,set_add(set1,a+1,len-1,new),set2);
      else
	return Cons(x,set1,set_add(set2,a,len,new));
    }
  }
}

static ATerm set_union_2(ATerm s1, ATerm s2,char lookup) {
  if (s1==atom) return atom;
  else if (s1==emptyset) return s2;
  else if (s2==emptyset) return s1;
  else { ATerm key=NULL,res=NULL;
    if (lookup) {
      key = (ATerm)ATmakeAppl2(sum,s1,s2);
      res = ATtableGet(global_ct,key);
      if (res) return res;
    }
    { // either not looked up, or not found in cache: compute
      ATerm x = ATgetArgument(s1,0);
      ATerm y = ATgetArgument(s2,0);
      int c = ATcmp(x,y);
      if (c==0) res=Cons(x, set_union_2(ATgetArgument(s1,1),ATgetArgument(s2,1),1),
			 set_union_2(ATgetArgument(s1,2),ATgetArgument(s2,2),0));
      else if (c<0) res=Cons(x, ATgetArgument(s1,1),
			     set_union_2(ATgetArgument(s1,2),s2,0));
      else res = Cons(y, ATgetArgument(s2,1),
		      set_union_2(s1,ATgetArgument(s2,2),0));
      if (lookup) ATtablePut(global_ct,key,res);
      return res;
    }
  }
}

ATerm set_union(ATerm s1,ATerm s2){
        s1=set_union_2(s1,s2,0);
	ATtableReset(global_ct);
	return s1;
}

static ATerm set_minus_2(ATerm a,ATerm b, char lookup) {
  if (b==emptyset) return a;
  else if (a==emptyset) return emptyset;
  else if (b==atom) return emptyset;
  else {
    ATerm key=NULL, res=NULL;
    if (lookup) {
      key=(ATerm)ATmakeAppl2(min,a,b);
      res=ATtableGet(global_ct,key);
      if (res) return res;
    }
    { // not looked up, or not found in cache.
      ATerm x=ATgetArgument(a,0);
      ATerm y= ATgetArgument(b,0);
      int c=ATcmp(x,y);
      if (c==0)
	res=MakeCons(x,set_minus_2(ATgetArgument(a,1),ATgetArgument(b,1),1),
		     set_minus_2(ATgetArgument(a,2),ATgetArgument(b,2),0));
      else if (c<0)
	res=Cons(x,ATgetArgument(a,1),
		 set_minus_2(ATgetArgument(a,2),b,0));
      else
	res=(ATerm)set_minus_2(a,ATgetArgument(b,2),0);
      if (lookup) ATtablePut(global_ct,key,res);
      return res;
    }
  }
}

ATerm set_minus(ATerm s1,ATerm s2){
  s1=set_minus_2(s1,s2,0);
  ATtableReset(global_ct);
  return s1;
}

static void set_zip_2(ATerm in1,ATerm in2,ATerm *out1,ATerm *out2, char lookup){
  if (in1==atom) {*out1=atom; *out2=emptyset; return;}
  else if (in1==emptyset) {*out1=*out2=in2; return;}
  else if (in2==emptyset) {*out1=in1; *out2=in2; return;}
  else {
    ATerm key=NULL, res=NULL;
    if (lookup) {
      key = (ATerm)ATmakeAppl2(zip,in1,in2);
      res=ATtableGet(global_ct,key);
      if (res) {
	*out1=ATgetArgument(res,0);
	*out2=ATgetArgument(res,1);
	return;
      }
    }
    { // not looked up, or not found in cache: compute
      int c=ATcmp(ATgetArgument(in1,0),ATgetArgument(in2,0));
      if(c<0) {
	ATerm t1=NULL,t2=NULL;
	set_zip_2(ATgetArgument(in1,2),in2,&t1,&t2,0);
	*out1=Cons(ATgetArgument(in1,0),ATgetArgument(in1,1),t1);
	*out2=t2;
      } 
      else if (c>0) {
	ATerm t1=NULL,t2=NULL;
	set_zip_2(in1,ATgetArgument(in2,2),&t1,&t2,0);
	*out1=Cons(ATgetArgument(in2,0),ATgetArgument(in2,1),t1);
	*out2=Cons(ATgetArgument(in2,0),ATgetArgument(in2,1),t2);
      } 
      else {
	ATerm t1=NULL,t2=NULL,t3=NULL,t4=NULL;
	set_zip_2(ATgetArgument(in1,1),ATgetArgument(in2,1),&t1,&t2,1);
	set_zip_2(ATgetArgument(in1,2),ATgetArgument(in2,2),&t3,&t4,0);
	*out1=Cons(ATgetArgument(in1,0),t1,t3);
	*out2=MakeCons(ATgetArgument(in1,0),t2,t4);
      }
      if (lookup)
	ATtablePut(global_ct,key,(ATerm)ATmakeAppl2(zip,*out1,*out2));
    }
  }
}

void set_zip(ATerm in1,ATerm in2,ATerm *out1,ATerm *out2){
  set_zip_2(in1,in2,out1,out2,0);
  ATtableReset(global_ct);
}

static ATerm set_project_2(ATerm set,int ofs,int *proj,int len,char lookup) {
  // WARNING: cache results may never be reused from different toplevel calls to project!!
  if (set==emptyset) return emptyset;
  else if (len==0) return atom;
  else {
    ATerm key=NULL, res=NULL;
    if (lookup) {
      key=(ATerm)ATmakeAppl1(pi,set);
      res=ATtableGet(global_ct,key);
      if (res) return res;
    }
    { // not looked up, or not found in cache: compute
      if (ofs==proj[0])
	// check: projection of non-empty set is always nonempty...
	res = Cons(ATgetArgument(set,0),
		   set_project_2(ATgetArgument(set,1),ofs+1,proj+1,len-1,1),
		   set_project_2(ATgetArgument(set,2),ofs,proj,len,0));
      else
	for (res=emptyset;set!=emptyset;set=ATgetArgument(set,2))
	  res=set_union_2(res,set_project_2(ATgetArgument(set,1),ofs+1,proj,len,1),1);
      if (lookup) ATtablePut(global_ct,key,res);
      return res;
    }
  }
}

ATerm set_project(ATerm set,int *proj,int len){
  set=set_project_2(set,0,proj,len,0);
  ATtableReset(global_ct);
  return set;
}

static ATerm set_reach_2(ATerm set,ATerm trans,int *proj,int p_len,int ofs, char lookup);

static ATerm copy_level(ATerm set,ATerm trans,int *proj,int p_len,int ofs){
  if (set==emptyset)
    return emptyset;
  else
    return MakeCons(ATgetArgument(set,0),
		    set_reach_2(ATgetArgument(set,1),trans,proj,p_len,ofs+1,1),
		    copy_level(ATgetArgument(set,2),trans,proj,p_len,ofs));
}

static ATerm trans_level(ATerm set,ATerm trans,int *proj,int p_len,int ofs){
  if (trans==emptyset)
    return emptyset;
  else 
    return MakeCons(ATgetArgument(trans,0),
		    set_reach_2(set,ATgetArgument(trans,1),proj+1,p_len-1,ofs+1,1),
		    trans_level(set,ATgetArgument(trans,2),proj,p_len,ofs));
}

static ATerm apply_reach(ATerm set,ATerm trans,int *proj,int p_len,int ofs){
  int c;
  ATerm res=emptyset;
  for(;(ATgetAFun(set)==cons)&&(ATgetAFun(trans)==cons);){
    c=ATcmp(ATgetArgument(set,0),ATgetArgument(trans,0));
    if (c<0)
      set=ATgetArgument(set,2);
    else if (c>0)
      trans=ATgetArgument(trans,2);
    else {
      res=set_union_2(res, trans_level(ATgetArgument(set,1),
				       ATgetArgument(trans,1),proj,p_len,ofs),0);
      set=ATgetArgument(set,2);
      trans=ATgetArgument(trans,2);
    }
  }
  return res;
}

static ATerm set_reach_2(ATerm set,ATerm trans,int *proj,int p_len,int ofs, char lookup){
  // WARNING: cache results may never be reused from different toplevel calls to project!!
  // TO CHECK: why add 'trans' to cache? Just caching set should probably work as well
  if (p_len==0)
    return set;
  else {
    ATerm key=NULL, res=NULL;
    if (lookup) {
      key = (ATerm)ATmakeAppl2(reach,set,trans);
      res=ATtableGet(global_ct,key);
      if (res) return res;
    }
    { if (proj[0]==ofs)
	res = apply_reach(set,trans,proj,p_len,ofs);
      else
	res = copy_level(set,trans,proj,p_len,ofs);
      if (lookup) 
	ATtablePut(global_ct,key,res);
      return res;
    }
  }
}

ATerm set_reach(ATerm set,ATerm trans,int *proj,int p_len){
  set=set_reach_2(set,trans,proj,p_len,0,0);
  ATtableReset(global_ct);
  return set;
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

int set_enum(ATerm set,ATerm *a,int len,int (*callback)(ATerm *,int)){
	return set_enum_2(set,a,len,callback,0);
}

static ATermIndexedSet count_is;
static long node_count;
static long long *elem_count;
static long elem_size;

static long long count_set_2(ATerm set){
  if (set==emptyset) return 0;
  else if (set==atom) return 1;
  else {
    ATbool new;
    long idx=ATindexedSetPut(count_is,(ATerm)set,&new);
    if(new){
      node_count++;
      if (idx>=elem_size){
	elem_size=elem_size+(elem_size>>1);
	elem_count=realloc(elem_count,elem_size*sizeof(long long));
	//ATwarning("resize %d %d %x",idx,elem_size,elem_count);
      }
      long long c=count_set_2(ATgetArgument(set,1))+count_set_2(ATgetArgument(set,2));
      return elem_count[idx]=c;
    }
    else
      return elem_count[idx];
  }
}

void count_set(ATerm set,long *nodes,long long *elements){
	count_is=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
	elem_count=malloc(HASH_INIT*sizeof(long long));
	elem_size=HASH_INIT;
	node_count=2; // atom and emptyset
	*elements=count_set_2(set);
	ATindexedSetDestroy(count_is);
	free(elem_count);
	*nodes=node_count;
}

void set_reset_ct(){
	if (global_ct!=NULL) {
		ATtableDestroy(global_ct);
	}
	global_ct=ATtableCreate(1024,75);
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
	pi=ATmakeAFun("VSET_PI",1,ATfalse);
	ATprotectAFun(pi);
	reach=ATmakeAFun("VSET_REACH",2,ATfalse);
	ATprotectAFun(reach);
	set_reset_ct();
}


/*  CURRENTLY NOT USED 

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

*/