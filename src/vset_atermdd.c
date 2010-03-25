#include <config.h>
#include <stdlib.h>
#include <assert.h>

#include <vdom_object.h>
#include <runtime.h>
#include <aterm2.h>

static void WarningHandler(const char *format, va_list args) {
	FILE* f=log_get_stream(info);
	if (f) {
		fprintf(f,"%s: ",get_label());
		ATvfprintf(f, format, args);
		fprintf(f,"\n");
	}
}

static void ErrorHandler(const char *format, va_list args) {
	FILE* f=log_get_stream(error);
	if (f) {
		fprintf(f,"%s: ",get_label());
		ATvfprintf(f, format, args);
		fprintf(f,"\n");
	}
	Fatal(1,error,"ATerror");
	exit(EXIT_FAILURE);
}

static void atermdd_popt(poptContext con,
                            enum poptCallbackReason reason,
                            const struct poptOption * opt,
                            const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST: {
		char*argv[]={"xxx",NULL};
		ATinit(1, argv, (ATerm*) RTstackBottom());
		ATsetWarningHandler(WarningHandler);
		ATsetErrorHandler(ErrorHandler);
		return;
	}
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Fatal(1,error,"unexpected call to atermdd_popt");
}

struct poptOption atermdd_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)atermdd_popt , 0 , NULL , NULL },
	POPT_TABLEEND
};

struct vector_domain {
	struct vector_domain_shared shared;
};

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

static ATerm emptyset=NULL;
static AFun cons;
static AFun zip,min,sum,pi,reach,inv_reach;
static ATerm atom;
static ATerm Atom=NULL;
static ATerm Empty=NULL;
static ATermTable global_ct=NULL;

#define ATcmp ATcompare
//define ATcmp(t1,t2) (((long)t1)-((long)t2))

#define HASH_INIT 1024
#define HASH_LOAD 75

static void set_reset_ct(){
	if (global_ct!=NULL) {
		ATtableDestroy(global_ct);
	}
	global_ct=ATtableCreate(HASH_INIT,HASH_LOAD);
}

static void set_init(){
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
    inv_reach=ATmakeAFun("VSET_INV_REACH",2,ATfalse);
	ATprotectAFun(inv_reach);
	// used for vector_set_tree:
	Empty=ATmake("VSET_E");
	ATprotect(&Empty);
	Atom=ATmake("VSET_A");
	ATprotect(&Atom);
	set_reset_ct();
}

static vset_t set_create_both(vdom_t dom,int k,int* proj){
	vset_t set=(vset_t)RTmalloc(sizeof(struct vector_set)+k*sizeof(int));
	set->dom=dom;
	set->set=emptyset;
	ATprotect(&set->set);
	set->p_len=k;
	for(int i=0;i<k;i++) set->proj[i]=proj[i];
	return set;
}

static vrel_t rel_create_both(vdom_t dom,int k,int* proj){
	vrel_t rel=(vrel_t)RTmalloc(sizeof(struct vector_relation)+k*sizeof(int));
	rel->dom=dom;
	rel->rel=emptyset;
	ATprotect(&rel->rel);
	rel->p_len=k;
	for(int i=0;i<k;i++) rel->proj[i]=proj[i];
	return rel;
}

static int set_is_empty_both(vset_t set){
	return ATisEqual(set->set,emptyset);
}

static int set_equal_both(vset_t set1,vset_t set2){
	return ATisEqual(set1->set,set2->set);
}

static void set_clear_both(vset_t set){
	set->set=emptyset;
}

static void set_copy_both(vset_t dst,vset_t src){
	//Maybe we should check if the lengths match!
	dst->set=src->set;
}


// these are used for counting:

static ATermIndexedSet count_is;
static long node_count;
static bn_int_t *elem_count;
static long elem_size;

static long count_set_t2(ATerm set){
  ATbool new;
  long idx, idx_0, idx_1, idx_2;

  idx=ATindexedSetPut(count_is,(ATerm)set,&new);
  if(new){
    node_count++;
    if (idx>=elem_size){
      long elem_size_old=elem_size;
      elem_size=elem_size+(elem_size>>1);
      elem_count=realloc(elem_count,elem_size*sizeof(bn_int_t));
      //ATwarning("resize %d %d %x",idx,elem_size,elem_count);
      for(int i=elem_size_old;i<elem_size;i++) bn_init(&elem_count[i]);
    }
    idx_0=count_set_t2(ATgetArgument(set,0));
    idx_1=count_set_t2(ATgetArgument(set,1));
    idx_2=count_set_t2(ATgetArgument(set,2));
    bn_add(&elem_count[idx_0],&elem_count[idx],&elem_count[idx]);
    bn_add(&elem_count[idx_1],&elem_count[idx],&elem_count[idx]);
    bn_add(&elem_count[idx_2],&elem_count[idx],&elem_count[idx]);
    return idx;
  }
  else
    return idx;
}

static void set_count_t(ATerm set,long *nodes,bn_int_t *elements){
  long idx;

  count_is=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
  elem_size=HASH_INIT;
  elem_count=malloc(elem_size*sizeof(bn_int_t));
  for(int i=0;i<elem_size;i++) bn_init(&elem_count[i]);
  node_count=2; // atom and emptyset
  idx=ATindexedSetPut(count_is,Empty,NULL);
  assert(idx<elem_size);
  bn_set_digit(&elem_count[idx],0);
  idx=ATindexedSetPut(count_is,Atom,NULL);
  assert(idx<elem_size);
  bn_set_digit(&elem_count[idx],1);
  idx=count_set_t2(set);
  bn_init_copy(elements,&elem_count[idx]);
  ATindexedSetDestroy(count_is);
  for(int i=0;i<elem_size;i++) bn_clear(&elem_count[i]);
  free(elem_count);
  *nodes=node_count;}

static void set_count_tree(vset_t set,long *nodes,bn_int_t *elements){
  set_count_t(set->set,nodes,elements);
}

static void rel_count_tree(vrel_t rel,long *nodes,bn_int_t *elements){
  set_count_t(rel->rel,nodes,elements);
}

static long count_set_2(ATerm set){
  ATbool new;
  long idx, idx_1, idx_2;

  idx=ATindexedSetPut(count_is,(ATerm)set,&new);
  if(new){
    node_count++;
    if (idx>=elem_size){
      long elem_size_old=elem_size;
      elem_size=elem_size+(elem_size>>1);
      elem_count=realloc(elem_count,elem_size*sizeof(bn_int_t));
      //ATwarning("resize %d %d %x",idx,elem_size,elem_count);
      for(int i=elem_size_old;i<elem_size;i++) bn_init(&elem_count[i]);
    }
    idx_1=count_set_2(ATgetArgument(set,1));
    idx_2=count_set_2(ATgetArgument(set,2));
    bn_add(&elem_count[idx_1],&elem_count[idx_2],&elem_count[idx]);
    return idx;
  }
  else
    return idx;
}

static void count_set(ATerm set,long *nodes,bn_int_t *elements){
  long idx;

  count_is=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
  elem_size=HASH_INIT;
  elem_count=malloc(elem_size*sizeof(bn_int_t));
  for(int i=0;i<elem_size;i++) bn_init(&elem_count[i]);
  node_count=2; // atom and emptyset
  idx=ATindexedSetPut(count_is,(ATerm)emptyset,NULL);
  assert(idx<elem_size);
  bn_set_digit(&elem_count[idx],0);
  idx=ATindexedSetPut(count_is,(ATerm)atom,NULL);
  assert(idx<elem_size);
  bn_set_digit(&elem_count[idx],1);
  idx=count_set_2(set);
  bn_init_copy(elements,&elem_count[idx]);
  ATindexedSetDestroy(count_is);
  for(int i=0;i<elem_size;i++) bn_clear(&elem_count[i]);
  free(elem_count);
  *nodes=node_count;}

static void set_count_list(vset_t set,long *nodes,bn_int_t *elements){
  count_set(set->set,nodes,elements);
}

static void rel_count_list(vrel_t rel,long *nodes,bn_int_t *elements){
  count_set(rel->rel,nodes,elements);
}

static ATbool set_member(ATerm set,ATerm *a){
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

static ATerm MCons(ATerm down,ATerm left,ATerm right) { // used for tree
    if (ATisEqual(down,Empty) &&
        ATisEqual(left,Empty) &&
        ATisEqual(right,Empty))
        return Empty;
    return Cons(down,left,right);
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


static ATerm singleton(ATerm *a,int len){
  ATerm set=atom;
  for (int i=len-1;i>=0;i--)
    set=Cons(a[i],set,emptyset);
  return set;
}

static ATerm set_add(ATerm set,ATerm *a,int len){
  if (set==emptyset) return singleton(a,len);
  else if (set==atom) return atom;
  else {
    ATerm x=ATgetArgument(set,0);
    int c = ATcmp(a[0],x);
    if (c<0)
      return Cons(a[0],singleton(a+1,len-1),set);
    else {
      ATerm set1 = ATgetArgument(set,1);
      ATerm set2 = ATgetArgument(set,2);
      if (c==0)
	return Cons(x,set_add(set1,a+1,len-1),set2);
      else
	return Cons(x,set1,set_add(set2,a,len));
    }
  }
}

static void set_add_list(vset_t set,const int* e){
	int N=set->p_len?set->p_len:set->dom->shared.size;
	ATerm vec[N];
	for(int i=0;i<N;i++) vec[i]=(ATerm)ATmakeInt(e[i]);
	set->set=set_add(set->set,vec,N);
}

static void rel_add_list(vrel_t rel,const int* src, const int* dst){
	int N=rel->p_len?rel->p_len:rel->dom->shared.size;
	ATerm vec[2*N];
	for(int i=0;i<N;i++) {
		vec[i+i]=(ATerm)ATmakeInt(src[i]);
		vec[i+i+1]=(ATerm)ATmakeInt(dst[i]);
	}
	rel->rel=set_add(rel->rel,vec,2*N);
}

static int set_member_list(vset_t set,const int* e){
	int N=set->p_len?set->p_len:set->dom->shared.size;
	ATerm vec[N];
	for(int i=0;i<N;i++) vec[i]=(ATerm)ATmakeInt(e[i]);
	return set_member(set->set,vec);
}



static ATbool set_member_tree_2(ATerm set,const int *a);
static ATerm singleton_tree(const int *a,int len);
static ATerm set_add_tree_2(ATerm set, const int *a,int len,ATbool *new);

static void rel_add_tree(vrel_t rel,const int* src, const int* dst){
	int N=rel->p_len?rel->p_len:rel->dom->shared.size;
	int vec[2*N];
	for(int i=0;i<N;i++) {
		vec[i+i]=src[i];
		vec[i+i+1]=dst[i];
	}
	rel->rel=set_add_tree_2(rel->rel,vec,2*N,NULL);
}


static ATbool set_member_tree_2(ATerm set, const int *a) {
  for (;;) {
    if (set==Empty) return ATfalse;
    else if (set==Atom ) return ATtrue;
    else {
      int x=a++[0]+1;
      while (x!=1) {
	int odd = x & 0x0001;
	x = x>>1;
	if (odd)
	  set = Right(set);
	else
	  set = Left(set);
	if (set==Empty) return ATfalse;
      }
    }
    set = Down(set);
  }
}

static ATerm singleton2(int x, const int *a, int len) {
  if (x==1) return Cons(singleton_tree(a,len-1),Empty,Empty);
  else {
    int odd = x & 0x0001;
    x = x>>1;
    if (odd)
      return Cons(Empty,Empty,singleton2(x,a,len));
    else
      return Cons(Empty,singleton2(x,a,len),Empty);
  }
}

static ATerm singleton_tree(const int *a,int len){
  if (len==0) return Atom;
  else return singleton2(a[0]+1,a+1,len); // only values >0 can be stored
}

static ATerm set_add2(ATerm set,int x, const int *a,int len,ATbool *new){
  if (set==Empty) {
    if (new) *new=ATtrue;
    return singleton2(x,a,len);
  }
  else if (x==1) return Cons(set_add_tree_2(Down(set),a,len-1,new),Left(set),Right(set));
  else {
    int odd = x & 0x0001;
    x = x>>1;
    if (odd)
      return Cons(Down(set),Left(set),set_add2(Right(set),x,a,len,new));
    else
      return Cons(Down(set),set_add2(Left(set),x,a,len,new),Right(set));
  }
}

static ATerm set_add_tree_2(ATerm set,const int *a,int len,ATbool *new){
  if (set==Atom) {
    if (new) *new=ATfalse;
    return Atom;
  }
  else if (set==Empty) {
    if (new) *new=ATtrue;
    return singleton_tree(a,len);
  }
  else return set_add2(set,a[0]+1,a+1,len,new);
}


void set_add_tree(vset_t set,const int* e){
  int N=set->p_len?set->p_len:set->dom->shared.size;
  // ATbool new;
  set->set=set_add_tree_2(set->set,e,N,NULL);
}

int set_member_tree(vset_t set,const int* e){
  return set_member_tree_2(set->set,e);
}



static vset_element_cb global_cb;
static void* global_context;
static int* global_elem;


static int vset_enum_wrap(ATerm *a,int len){
	int vec[len];
	for(int i=0;i<len;i++) vec[i]=ATgetInt((ATermInt)a[i]);
	global_cb(global_context,vec);
	return 0;
}

static int vset_enum_first(ATerm *a,int len){
	for(int i=0;i<len;i++) global_elem[i]=ATgetInt((ATermInt)a[i]);
	return 1;
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

static void set_enum_list(vset_t set,vset_element_cb cb,void* context){
	int N=set->p_len?set->p_len:set->dom->shared.size;
	ATerm vec[N];
	global_cb=cb;
	global_context=context;
	set_enum_2(set->set,vec,N,vset_enum_wrap,0);
}

static void set_example_list(vset_t set,int *e){
	int N=set->p_len?set->p_len:set->dom->shared.size;
	ATerm vec[N];
	global_elem=e;
	set_enum_2(set->set,vec,N,vset_enum_first,0);
}

int vset_enum_wrap_tree(int *a,int len){
  (void)len;
  global_cb(global_context,a);
  return 0;
}

int vset_enum_tree_first(int *a,int len){
  for(int i=0; i<len; i++) global_elem[i]=a[i];
  return 1;
}


static int set_enum_t2(ATerm set,int *a,int len,int (*callback)(int*,int),int ofs,int shift, int cur){
  int tmp;
  if (set==Atom) return callback(a,ofs);
  else if (set==Empty) return 0;
  else {
    if (ofs<len) {
      a[ofs]=shift+cur-1; // Recall that 0 cannot be stored
      tmp=set_enum_t2(Down(set),a,len,callback,ofs+1,1,0);
      if (tmp) return tmp;
    }
    set_enum_t2(Left(set),a,len,callback,ofs,shift<<1,cur);
    set_enum_t2(Right(set),a,len,callback,ofs,shift<<1,shift|cur);
    return 0;
  }
}

static void set_enum_tree(vset_t set,vset_element_cb cb,void* context){
	int N=set->p_len?set->p_len:set->dom->shared.size;
	int vec[N];
	global_cb=cb;
	global_context=context;
	set_enum_t2(set->set,vec,N,vset_enum_wrap_tree,0,1,0);
}

static void set_example_tree(vset_t set,int *e){
	int N=set->p_len?set->p_len:set->dom->shared.size;
	int vec[N];
	global_elem=e;
	set_enum_t2(set->set,vec,N,vset_enum_tree_first,0,1,0);
}

static vset_element_cb match_cb;
static void* match_context;

static int vset_match_wrap(ATerm *a,int len){
	int vec[len];
	for(int i=0;i<len;i++) vec[i]=ATgetInt((ATermInt)a[i]);
	match_cb(match_context,vec);
	return 0;
}

/* return < 0 : error, 0 no matches, >0 matches found */
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

static void set_enum_match_list(vset_t set,int p_len,int* proj,int*match,vset_element_cb cb,void* context){
	int N=set->p_len?set->p_len:set->dom->shared.size;
	ATerm vec[N];
	ATerm pattern[p_len];
	for(int i=0;i<p_len;i++) pattern[i]=(ATerm)ATmakeInt(match[i]);
	match_cb=cb;
	match_context=context;
	ATermIndexedSet dead_branches=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
	set_enum_match_2(dead_branches,set->set,vec,N,pattern,proj,p_len,vset_match_wrap,0);
	ATindexedSetDestroy(dead_branches);
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

static void set_union_list(vset_t dst,vset_t src){
	dst->set=set_union_2(dst->set,src->set,0);
	ATtableReset(global_ct);
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

static void set_minus_list(vset_t dst,vset_t src){
	dst->set=set_minus_2(dst->set,src->set,0);
	ATtableReset(global_ct);
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

static void set_zip_list(vset_t dst,vset_t src){
	set_zip_2(dst->set,src->set,&dst->set,&src->set,0);
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


static void set_project_list(vset_t dst,vset_t src){
	dst->set=set_project_2(src->set,0,dst->proj,dst->p_len,0);
	ATtableReset(global_ct);
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

void set_next_list(vset_t dst,vset_t src,vrel_t rel){
	dst->set=set_reach_2(src->set,rel->rel,rel->proj,rel->p_len,0,0);
	ATtableReset(global_ct);
}

static ATerm set_inv_reach(ATerm set, ATerm trans, int *proj, int p_len, int ofs, char lookup);

static ATerm copy_level_inv(ATerm set, ATerm trans, int *proj, int p_len, int ofs) {
    if (set==emptyset) {
        return emptyset;
    } else {
        return MakeCons(ATgetArgument(set, 0),
                        set_inv_reach(ATgetArgument(set, 1), trans, proj, p_len, ofs + 1, 1),
                        copy_level_inv(ATgetArgument(set, 2), trans, proj, p_len, ofs)
                       );
    }
}

static ATerm trans_level_inv(ATerm trans_src, ATerm set, ATerm trans, int *proj, int p_len, int ofs)
{
    int c;
    ATerm res = emptyset;
    for(;(ATgetAFun(trans)==cons) && ATgetAFun(set)==cons;) {
        // compare 2nd argument
        c = ATcmp(ATgetArgument(set,0), ATgetArgument(trans,0));
        if (c < 0) {
            set = ATgetArgument(set, 2);
        } else if (c > 0) {
            trans=ATgetArgument(trans,2);
        } else {
            // equal, match rest and return
            //ATprintf("match, %t -> %t\n", trans_src, ATgetArgument(trans, 0));
            ATerm tail = set_inv_reach(ATgetArgument(set, 1), ATgetArgument(trans, 1), proj + 1, p_len - 1, ofs+1, 1);
            res = set_union_2(res, MakeCons(trans_src, tail, emptyset), 0);

            set=ATgetArgument(set,2);
            trans=ATgetArgument(trans,2);
        }
    }
    return res;
}


static ATerm apply_inv_reach(ATerm set, ATerm trans, int *proj, int p_len, int ofs){
    ATerm res=emptyset;

    for(;(ATgetAFun(trans)==cons);) {
        res = set_union_2(
            res,
            trans_level_inv(ATgetArgument(trans, 0),
                set,
                ATgetArgument(trans,1),
                proj,
                p_len,
                ofs),
            0);
        trans = ATgetArgument(trans, 2);
    }
    return res;
}


static ATerm set_inv_reach(ATerm set, ATerm trans, int *proj, int p_len, int ofs, char lookup)
{
    if (p_len == 0) {
        return set;
    } else {
        ATerm key = NULL, res = NULL;
        if (lookup) {
            // do lookup
            key = (ATerm)ATmakeAppl2(inv_reach, set, trans);
            res = ATtableGet(global_ct, key);
            if (res) return res;
        }

        if (proj[0] == ofs) {
            // apply the relation backwards
            res = apply_inv_reach(set, trans, proj, p_len, ofs);
        } else {
            // copy, nothing in projection
            res = copy_level_inv(set, trans, proj, p_len, ofs);
        }

        if (lookup) {
            // put in cache
            ATtablePut(global_ct, key, res);
        }
        return res;
    }
}

void set_prev_list(vset_t dst, vset_t src, vrel_t rel) {
    dst->set=set_inv_reach(src->set, rel->rel, rel->proj, rel->p_len, 0, 0);
    ATtableReset(global_ct);
}


#if 0
union Atom _ = Atom
union Empty s = s
union s Empty = s
union (Cons dstdown dstleft dstright) (Cons srcdown srcleft srcright) = Cons (union dstdown srcdown) (union dstleft srcleft) (union dstright srcright)
#endif

static ATerm set_union_tree_2(ATerm s1, ATerm s2, char lookup) {
    if (s1==Atom) return Atom;
    else if (s1==Empty) return s2;
    else if (s2==Empty) return s1;
    else {
        ATerm key=NULL,res=NULL;
        if (lookup) {
            key = (ATerm)ATmakeAppl2(sum,s1,s2);
            res = ATtableGet(global_ct,key);
            if (res) return res;
        }
        // either not looked up, or not found in cache: compute
        res = Cons(
                set_union_tree_2(Down(s1),  Down(s2), 1),
                set_union_tree_2(Left(s1),  Left(s2), 1),
                set_union_tree_2(Right(s1), Right(s2), 1)
              );
        if (lookup) ATtablePut(global_ct,key,res);
        return res;
    }
}

static void set_union_tree(vset_t dst, vset_t src) {
	dst->set=set_union_tree_2(dst->set,src->set,0);
	ATtableReset(global_ct);
}


#if 0
project Empty _ _ = Empty
project _ _ [] = Atom
project (Cons x s t) i (j:l) | i == j = Cons (project x (i+1) (l)) (project s i (j:l)) (project t i (j:l))
project (Cons x s t) i (j:l) | i < j  = union (project x (i+1) (j:l)) (union (project s i (j:l)) (project t i (j:l)) )
#endif

static ATerm set_project_tree_2(ATerm set,int ofs,int *proj,int len,char lookup) {
  // WARNING: cache results may never be reused from different toplevel calls to project!!
    if (set==Empty) return Empty;
    else if (len==0) return Atom;
    else {
        ATerm key=NULL, res=NULL;
        if (lookup) {
          key=(ATerm)ATmakeAppl1(pi,set);
          res=ATtableGet(global_ct,key);
          if (res) return res;
        }
        // not looked up, or not found in cache: compute
        if (ofs==proj[0]) {
            // check: projection of non-empty set is always nonempty...
            res = Cons(set_project_tree_2(Down(set) , ofs+1, proj+1, len-1, 1),
                       set_project_tree_2(Left(set) , ofs  , proj  , len  , 1),
                       set_project_tree_2(Right(set), ofs  , proj  , len  , 1));
        } else {
            res = set_union_tree_2(
                    set_project_tree_2(Down(set), ofs+1, proj, len, 1),
                    set_union_tree_2(
                        set_project_tree_2(Left(set) , ofs, proj, len, 1),
                        set_project_tree_2(Right(set), ofs, proj, len, 1),
                        1
                    ), 1);
        }
        if (lookup) ATtablePut(global_ct,key,res);
        return res;
    }
}


static void set_project_tree(vset_t dst,vset_t src){
	dst->set=set_project_tree_2(src->set,0,dst->proj,dst->p_len,0);
	ATtableReset(global_ct);
}

#if 0
minus s Empty = s
minus Empty s = Empty
minus _ Atom = Empty
minus (Cons dstdown dstleft dstright) (Cons srcdown srcleft srcright) = mcons (minus dstdown srcdown) (minus dstleft srcleft) (minus dstright srcright)
#endif

static ATerm set_minus_tree_2(ATerm a, ATerm b, char lookup) {
    if (b==Empty) return a;
    else if (a==Empty) return Empty;
    else if (b==Atom) return Empty;
    else {
        ATerm key=NULL, res=NULL;
        if (lookup) {
            key=(ATerm)ATmakeAppl2(min,a,b);
            res=ATtableGet(global_ct,key);
            if (res) return res;
        }
        // not looked up, or not found in cache.
        res = MCons(
            set_minus_tree_2(Down(a) ,Down(b) ,1),
            set_minus_tree_2(Left(a) ,Left(b) ,1),
            set_minus_tree_2(Right(a),Right(b),1)
            );
        if (lookup) ATtablePut(global_ct,key,res);
        return res;
    }
}

static void set_minus_tree(vset_t dst,vset_t src){
	dst->set=set_minus_tree_2(dst->set,src->set,0);
	ATtableReset(global_ct);
}

#if 0
tzip (Atom, _)  = (Atom, Empty)
tzip (s, Empty) = (s, Empty)
tzip (Empty, s) = (s, s)
tzip (Cons x s1 s2, Cons y t1 t2) = (Cons u u1 v1, mcons v u2 v2)
	where
		(u,v) =   tzip (x,y);
		(u1,u2) = tzip (s1,t1);
		(v1,v2) = tzip (s2,t2);
#endif

static void set_zip_tree_2(ATerm in1, ATerm in2, ATerm *out1, ATerm *out2, char lookup){
    if (in1==Atom) { *out1=Atom; *out2=Empty; return; }
    else if (in1==Empty) { *out1=*out2=in2; return; }
    else if (in2==Empty) { *out1=in1; *out2=in2; return; }
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
        // not looked up, or not found in cache: compute
        ATerm u , m;
        set_zip_tree_2(Down(in1), Down(in2),&u,&m,1);
        ATerm u1,m1;
        set_zip_tree_2(Left(in1), Left(in2),&u1,&m1,1);
        ATerm u2,m2;
        set_zip_tree_2(Right(in1), Right(in2),&u2,&m2,1);
        *out1=Cons(u,u1,u2);
        *out2=MCons(m,m1,m2);

        if (lookup)
            ATtablePut(global_ct,key,(ATerm)ATmakeAppl2(zip,*out1,*out2));
    }
}

static void set_zip_tree(vset_t dst,vset_t src){
	set_zip_tree_2(dst->set,src->set,&dst->set,&src->set,0);
	ATtableReset(global_ct);
}


static ATerm set_reach_tree_2(ATerm set, ATerm trans, int *proj, int p_len, int ofs, char lookup);

#if 0
copy Empty _ _ _ = Empty
copy (Cons down left right) rel i (j:k) = mcons (next down rel (i+1) (j:k)) (copy left rel i (j:k)) (copy right rel i (j:k))
#endif

static ATerm copy_level_next_tree(ATerm set, ATerm trans, int *proj, int p_len, int ofs) {
    if (set==Empty) {
        return Empty;
    } else {
        return MCons(set_reach_tree_2(    Down(set) , trans, proj, p_len, ofs+1, 1),
                     copy_level_next_tree(Left(set) , trans, proj, p_len, ofs),
                     copy_level_next_tree(Right(set), trans, proj, p_len, ofs));
    }
}


#if 0
trans src Atom i (j:k) = Atom
trans src Empty i (j:k) = Empty
trans src (Cons down left right) i (j:k) = mcons (next src down (i+1) k) (trans src left i (j:k)) (trans src right i (j:k))
#endif

static ATerm trans_level_next_tree(ATerm set, ATerm trans, int *proj, int p_len, int ofs) {
    if (trans==Atom || trans==Empty) {
        return trans;
    } else {
        return MCons(set_reach_tree_2(     set , Down(trans) , proj+1, p_len-1, ofs+1, 1),
                     trans_level_next_tree(set , Left(trans) , proj  , p_len  , ofs),
                     trans_level_next_tree(set , Right(trans), proj  , p_len  , ofs));
    }
}

#if 0
apply Empty _ _ _ = Empty
apply _ Empty _ _ = Empty -- not matched
apply (Cons x y z) (Cons u v w) i proj = union (trans x u i proj) (union (apply y v i proj) (apply z w i proj))
#endif

static ATerm apply_reach_next_tree(ATerm set, ATerm trans, int *proj, int p_len, int ofs) {
    if (set==Empty) { return Empty; }
    if (trans==Empty) { return Empty; }
    return set_union_tree_2(
                trans_level_next_tree(Down(set), Down(trans), proj, p_len, ofs),
                set_union_tree_2(
                    set_reach_tree_2(Left(set), Left(trans), proj, p_len, ofs, 1),
                    set_reach_tree_2(Right(set), Right(trans), proj, p_len, ofs, 1), 1), 1);
}

#if 0
next Empty _ _ _ = Empty -- nothing left in src set
next _ Empty _ _ = Empty -- nothing left in relation
next src rel i [] = src
next src rel i (j:k) | i == j = apply src rel i (j:k)
next src rel i (j:k) | i < j  = copy  src rel i (j:k)
#endif

static ATerm set_reach_tree_2(ATerm set, ATerm trans, int *proj, int p_len, int ofs, char lookup) {
    if (set==Empty) { return Empty; }
    if (trans==Empty) { return Empty; }

    if (p_len==0) {
        return set;
    } else {
        ATerm key=NULL, res=NULL;
        if (lookup) {
            key = (ATerm)ATmakeAppl2(reach,set,trans);
            res=ATtableGet(global_ct,key);
            if (res) return res;
        }
        if (proj[0]==ofs) {
            res = apply_reach_next_tree(set,trans,proj,p_len,ofs);
        } else {
            res = copy_level_next_tree(set,trans,proj,p_len,ofs);
        }
        if (lookup) ATtablePut(global_ct,key,res);
        return res;
    }
}


void set_next_tree(vset_t dst,vset_t src,vrel_t rel){
    dst->set=set_reach_tree_2(src->set, rel->rel, rel->proj, rel->p_len, 0, 0);
    ATtableReset(global_ct);
}

static ATerm set_prev_tree_2(ATerm set, ATerm trans, int *proj, int p_len, int ofs, char lookup);

#if 0
pcopy Empty _ _ _ = Empty
pcopy (Cons down left right) rel i (j:k) = mcons (prev down rel (i+1) (j:k)) (pcopy left rel i (j:k)) (pcopy right rel i (j:k))
#endif

static ATerm copy_level_prev_tree(ATerm set, ATerm trans, int *proj, int p_len, int ofs) {
    if (set==Empty) {
        return Empty;
    } else {
        return MCons(set_prev_tree_2(     Down(set) , trans, proj, p_len, ofs+1, 1),
                     copy_level_next_tree(Left(set) , trans, proj, p_len, ofs),
                     copy_level_next_tree(Right(set), trans, proj, p_len, ofs));
    }
}

#if 0
match Empty _ _ _ = Empty
match _ Empty _ _ = Empty
match (Cons x y z) (Cons u v w) i (j:k) = union (prev x u (i+1) k) (union (match y v i (j:k)) (match z w i (j:k)))
#endif

static ATerm match_prev_tree(ATerm set, ATerm trans, int *proj, int p_len, int ofs) {
    if (set==Empty) return Empty;
    if (trans==Empty) return Empty;
    return set_union_tree_2(
                set_prev_tree_2(Down(set), Down(trans), proj+1, p_len-1, ofs+1, 1),
                set_union_tree_2(
                    match_prev_tree(Left(set) , Left(trans) , proj, p_len, ofs),
                    match_prev_tree(Right(set), Right(trans), proj, p_len, ofs), 1), 1);
}

#if 0
merge src Empty _ _= Empty
merge src (Cons u v w) i proj = mcons (match src u i proj) (merge src v i proj) (merge src w i proj)
#endif

static ATerm merge_prev_tree(ATerm set, ATerm trans, int *proj, int p_len, int ofs) {
    if (trans==Empty) { return Empty; }
    return MCons(
                    match_prev_tree(set, Down(trans) , proj, p_len, ofs),
                    merge_prev_tree(set, Left(trans) , proj, p_len, ofs),
                    merge_prev_tree(set, Right(trans), proj, p_len, ofs)
                );
}

#if 0
prev Empty _ _ _ = Empty
prev _ Empty _ _ = Empty
prev src rel i [] = src
prev src rel i (j:k) | i == j = merge src rel i (j:k)
prev src rel i (j:k) | i <  j = pcopy src rel i (j:k)
#endif

static ATerm set_prev_tree_2(ATerm set, ATerm trans, int *proj, int p_len, int ofs, char lookup) {
    if (set==Empty) { return Empty; }
    if (trans==Empty) { return Empty; }

    if (p_len==0) {
        return set;
    } else {
        ATerm key=NULL, res=NULL;
        if (lookup) {
            key = (ATerm)ATmakeAppl2(inv_reach,set,trans);
            res=ATtableGet(global_ct,key);
            if (res) return res;
        }
        if (proj[0]==ofs) {
            res = merge_prev_tree(set,trans,proj,p_len,ofs);
        } else {
            res = copy_level_prev_tree(set,trans,proj,p_len,ofs);
        }
        if (lookup) ATtablePut(global_ct,key,res);
        return res;
    }
}

void set_prev_tree(vset_t dst,vset_t src,vrel_t rel){
    dst->set=set_prev_tree_2(src->set, rel->rel, rel->proj, rel->p_len, 0, 0);
    ATtableReset(global_ct);
}


static void reorder() {}


vdom_t vdom_create_tree(int n){
	Warning(info,"Creating an AtermDD tree domain.");
	vdom_t dom=(vdom_t)RTmalloc(sizeof(struct vector_domain));
	vdom_init_shared(dom,n);
	if (!emptyset) set_init();
	dom->shared.set_create=set_create_both;
	dom->shared.set_add=set_add_tree;
	dom->shared.set_member=set_member_tree;
	dom->shared.set_is_empty=set_is_empty_both;
	dom->shared.set_equal=set_equal_both;
	dom->shared.set_clear=set_clear_both;
	dom->shared.set_copy=set_copy_both;
	dom->shared.set_enum=set_enum_tree;
	dom->shared.set_enum_match=set_enum_match_list; // TODO
	dom->shared.set_example=set_example_tree;
	dom->shared.set_count=set_count_tree;
	dom->shared.rel_count=rel_count_tree;
    dom->shared.set_union=set_union_tree;
    dom->shared.set_minus=set_minus_tree;
	dom->shared.set_zip=set_zip_tree;
	dom->shared.set_project=set_project_tree;
	dom->shared.rel_create=rel_create_both; // initializes with emptyset instead of empty? wrong? -> all both functions mix this..
	dom->shared.rel_add=rel_add_tree;
	dom->shared.set_next=set_next_tree;
	dom->shared.set_prev=set_prev_tree;
	dom->shared.reorder=reorder;

	return dom;
}

vdom_t vdom_create_list(int n){
	Warning(info,"Creating an AtermDD list domain.");
	vdom_t dom=RT_NEW(struct vector_domain);
	vdom_init_shared(dom,n);
	if (!emptyset) set_init();
	dom->shared.set_create=set_create_both;
	dom->shared.set_add=set_add_list;
	dom->shared.set_member=set_member_list;
	dom->shared.set_is_empty=set_is_empty_both;
	dom->shared.set_equal=set_equal_both;
	dom->shared.set_clear=set_clear_both;
	dom->shared.set_copy=set_copy_both;
	dom->shared.set_enum=set_enum_list;
	dom->shared.set_enum_match=set_enum_match_list;
	dom->shared.set_example=set_example_list;
	dom->shared.set_count=set_count_list;
	dom->shared.rel_count=rel_count_list;
	dom->shared.set_union=set_union_list;
	dom->shared.set_minus=set_minus_list;
	dom->shared.set_zip=set_zip_list;
	dom->shared.set_project=set_project_list;
	dom->shared.rel_create=rel_create_both;
	dom->shared.rel_add=rel_add_list;
	dom->shared.set_next=set_next_list;
	dom->shared.set_prev=set_prev_list;
	dom->shared.reorder=reorder;
	return dom;
}

