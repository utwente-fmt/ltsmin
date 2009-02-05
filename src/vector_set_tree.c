#include <stdlib.h>

#include "vector_set.h"
#include "runtime.h"
#include "aterm2.h"

ATbool set_member(ATerm set,const int *a);
ATerm singleton(const int *a,int len);
ATerm set_add(ATerm set, const int *a,int len,ATbool *new);
int set_enum(ATerm set,int *a,int len,int (*callback)(int *,int));
void set_init();
int print_array(const int *a,int len);

static AFun cons;
static ATerm Atom=NULL;
static ATerm Empty=NULL;

struct vector_domain {
	int size;
};

vdom_t vdom_create(int n){
	vdom_t dom=(vdom_t)RTmalloc(sizeof(struct vector_domain));
	dom->size=n;
	if (!Empty) set_init();
	return dom;
}

struct vector_set {
	vdom_t dom;
	ATerm set;
	int p_len;
	int proj[];
};

vset_t vset_create(vdom_t dom,int k,int* proj){
	vset_t set=(vset_t)RTmalloc(sizeof(struct vector_set)+k*sizeof(int));
	set->dom=dom;
	set->set=Empty;
	ATprotect(&set->set);
	set->p_len=k;
	for(int i=0;i<k;i++) set->proj[i]=proj[i];
	return set;
}

void vset_add(vset_t set,const int* e){
  int N=set->p_len?set->p_len:set->dom->size;
  // ATbool new;
  set->set=set_add(set->set,e,N,NULL);
  //  if (new) {
  //    fprintf(stderr,"add: ");
  //    print_array(e,N);
  //  }
}

int vset_member(vset_t set,const int* e){
  return set_member(set->set,e);
}

int vset_is_empty(vset_t set){
  return ATisEqual(set->set,Empty);
}

void vset_clear(vset_t set){
  set->set=Empty;
}

void vset_copy(vset_t dst,vset_t src){
  dst->set=src->set;
}

static vset_element_cb global_cb;
static void* global_context;

static int vset_enum_wrap(int *a,int len){
  // fprintf(stderr,"cbk: ");
  // print_array(a,len);
  global_cb(global_context,a);
  return 0;
}

void vset_enum(vset_t set,vset_element_cb cb,void* context){
	int N=set->p_len?set->p_len:set->dom->size;
	int vec[N];
	global_cb=cb;
	global_context=context;
	set_enum(set->set,vec,N,vset_enum_wrap);
}

/***************************/

static inline ATerm Cons(ATerm down,ATerm left,ATerm right) {
  return (ATerm)ATmakeAppl3(cons,down,left,right);
}
static inline ATerm Down(ATerm e) {
  return ATgetArgument(e,0);
}
static inline ATerm Left(ATerm e) {
  return ATgetArgument(e,1);
}
static inline ATerm Right(ATerm e) {
  return ATgetArgument(e,2);
}

ATbool set_member2(ATerm set, int x, const int*a) {
  if (set==Empty) return ATfalse;
  else if (x==1) return set_member(Down(set),a);
  else {
    int odd = x & 0x0001;
    x = x>>1;
    if (odd)
      return set_member2(Right(set),x,a);
    else
      return set_member2(Left(set),x,a);
  }
}

ATbool set_member(ATerm set,const int *a){
  if (set==Empty) return ATfalse;
  else if (set==Atom) return ATtrue;
  else return set_member2(set,a[0]+1,a+1);
}

ATerm singleton2(int x, const int *a, int len) {
  if (x==1) return Cons(singleton(a,len-1),Empty,Empty);
  else {
    int odd = x & 0x0001;
    x = x>>1;
    if (odd)
      return Cons(Empty,Empty,singleton2(x,a,len));
    else
      return Cons(Empty,singleton2(x,a,len),Empty);
  }
}

ATerm singleton(const int *a,int len){
  if (len==0) return Atom;
  else return singleton2(a[0]+1,a+1,len); // only values >0 can be stored
}

ATerm set_add2(ATerm set,int x, const int *a,int len,ATbool *new){
  if (set==Empty) {
    if (new) *new=ATtrue; 
    return singleton2(x,a,len);
  }
  else if (x==1) return Cons(set_add(Down(set),a,len-1,new),Left(set),Right(set));
  else {
    int odd = x & 0x0001;
    x = x>>1;
    if (odd)
      return Cons(Down(set),Left(set),set_add2(Right(set),x,a,len,new));
    else
      return Cons(Down(set),set_add2(Left(set),x,a,len,new),Right(set));
  }
}

ATerm set_add(ATerm set,const int *a,int len,ATbool *new){
  if (set==Atom) {
    if (new) *new=ATfalse; 
    return Atom;
  }
  else if (set==Empty) {
    if (new) *new=ATtrue; 
    return singleton(a,len);
  }
  else return set_add2(set,a[0]+1,a+1,len,new);
}

static int set_enum_2(ATerm set,int *a,int len,int (*callback)(int*,int),int ofs,int shift, int cur){
  int tmp;
  if (set==Atom) return callback(a,ofs);
  else if (set==Empty) return 0;
  else {
    if (ofs<len) {
      a[ofs]=shift+cur-1; // Recall that 0 cannot be stored
      tmp=set_enum_2(Down(set),a,len,callback,ofs+1,1,0);
      if (tmp) return tmp;
    }
    set_enum_2(Left(set),a,len,callback,ofs,shift<<1,cur);
    set_enum_2(Right(set),a,len,callback,ofs,shift<<1,shift|cur);
  }
}


int set_enum(ATerm set,int *a,int len,int (*callback)(int *,int)){
  return set_enum_2(set,a,len,callback,0,1,0);
}

void set_init(){
	ATprotect(&Empty);
	ATprotect(&Atom);
	Empty=ATmake("VSET_E");
	Atom=ATmake("VSET_A");
	cons=ATmakeAFun("VSET_C",3,ATfalse);
	ATprotectAFun(cons);
}

void vset_project(vset_t dst,vset_t src) {abort();}
void vset_union(vset_t dst,vset_t src) {abort();}
void vset_minus(vset_t dst,vset_t src) {abort();}
void vset_zip(vset_t dst,vset_t src) {abort();}
void vset_count(vset_t set,long *nodes,long long *elements) {abort();}
void vrel_add(vrel_t rel,const int* src, const int* dst) {abort();}
vrel_t vrel_create(vdom_t dom,int k,int* proj) {abort();}
int vset_equal(vset_t set1,vset_t set2) {abort();}
void vset_next(vset_t dst,vset_t src,vrel_t rel) {abort();}


int print_array(const int *a,int len){
	fprintf(stderr,"[");
	if (len) {
		int i;
		fprintf(stderr,"%2d",a[0]);
		for(i=1;i<len;i++) fprintf(stderr,",%2d",a[i]);
	}
	fprintf(stderr,"]\n");
	return 0;
}
