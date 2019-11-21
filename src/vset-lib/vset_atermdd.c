#include <hre/config.h>
#include <stdlib.h>
#include <assert.h>

#include <aterm2.h>
#include <hre/user.h>
#include <vset-lib/vdom_object.h>

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
	Abort("ATerror");
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
		if (HREglobal()!=NULL) { // work around for the case when HRE is not yet initialised
            ATinit(1, argv, (ATerm*) HREstackBottom());
            ATsetWarningHandler(WarningHandler);
            ATsetErrorHandler(ErrorHandler);
        }
		return;
	}
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Abort("unexpected call to atermdd_popt");
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
    expand_cb expand;
    void *expand_ctx;
	ATerm rel;
	int p_len;
	int proj[];
};

static ATerm emptyset=NULL;
static AFun cons;
static AFun zip,min,sum,intersect,pi,reach,inv_reach,match,match3,rel_prod;
static ATerm atom;
static ATerm Atom=NULL;
static ATerm Empty=NULL;
static ATermTable global_ct=NULL;
static ATermTable global_rc=NULL; // Used in saturation - relational product
static ATermTable global_sc=NULL; // Used in saturation - saturation results
static ATermTable global_uc=NULL; // Used in saturation - union results

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

static void
set_init()
{
	ATprotect(&emptyset);
	ATprotect(&atom);
	emptyset=ATparse("VSET_E");
	atom=ATparse("VSET_A");
	cons=ATmakeAFun("VSET_C",3,ATfalse);
	ATprotectAFun(cons);
	zip=ATmakeAFun("VSET_ZIP",2,ATfalse);
	ATprotectAFun(zip);
	min=ATmakeAFun("VSET_MINUS",2,ATfalse);
	ATprotectAFun(min);
	sum=ATmakeAFun("VSET_UNION",2,ATfalse);
	ATprotectAFun(sum);
    intersect=ATmakeAFun("VSET_INTERSECT",2,ATfalse);
    ATprotectAFun(intersect);
	match=ATmakeAFun("VSET_MATCH",1,ATfalse);
	ATprotectAFun(match);
	match3=ATmakeAFun("VSET_MATCH3",3,ATfalse);
	ATprotectAFun(match3);
	pi=ATmakeAFun("VSET_PI",1,ATfalse);
	ATprotectAFun(pi);
	reach=ATmakeAFun("VSET_REACH",2,ATfalse);
	ATprotectAFun(reach);
    inv_reach=ATmakeAFun("VSET_INV_REACH",2,ATfalse);
	ATprotectAFun(inv_reach);
    rel_prod = ATmakeAFun("VSET_REL_PROD", 4, ATfalse);
    ATprotectAFun(rel_prod);
	// used for vector_set_tree:
	Empty=ATparse("VSET_E");
	ATprotect(&Empty);
	Atom=ATparse("VSET_A");
	ATprotect(&Atom);
	set_reset_ct();
}

static vset_t set_create_both(vdom_t dom,int k,int* proj){
    int l = (k < 0)?0:k;
	vset_t set=(vset_t)RTmalloc(sizeof(struct vector_set)+sizeof(int[l]));
	set->dom=dom;
	set->set=emptyset;
	ATprotect(&set->set);
	set->p_len=k;
	for(int i=0;i<k;i++) set->proj[i]=proj[i];
	return set;
}

static void set_destroy_both(vset_t set) {
    ATunprotect(&set->set);
    set->p_len = 0;
    RTfree(set);
}

static vrel_t rel_create_both(vdom_t dom,int k,int* proj){
    assert(k >= 0);
	vrel_t rel=(vrel_t)RTmalloc(sizeof(struct vector_relation)+sizeof(int[k]));
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
	assert(dst->p_len == src->p_len);
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
      elem_count=RTrealloc(elem_count,elem_size*sizeof(bn_int_t));
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

static void
set_count_t(ATerm set, long *nodes, double *elements)
{
  long idx;

  count_is=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
  elem_size=HASH_INIT;
  elem_count=RTmalloc(elem_size*sizeof(bn_int_t));
  for(int i=0;i<elem_size;i++) bn_init(&elem_count[i]);
  node_count=2; // atom and emptyset
  idx=ATindexedSetPut(count_is,Empty,NULL);
  assert(idx<elem_size);
  bn_set_digit(&elem_count[idx],0);
  idx=ATindexedSetPut(count_is,Atom,NULL);
  assert(idx<elem_size);
  bn_set_digit(&elem_count[idx],1);
  idx=count_set_t2(set);
  if (elements != NULL) *elements = bn_int2double(&elem_count[idx]);
  ATindexedSetDestroy(count_is);
  for(int i=0;i<elem_size;i++) bn_clear(&elem_count[i]);
  RTfree(elem_count);

  if (set == Atom || set == Empty)
      node_count = 1;

  if (nodes != NULL) *nodes=node_count;
}

static void set_count_tree(vset_t set,long *nodes,double *elements){
  set_count_t(set->set,nodes,elements);
}

static void rel_count_tree(vrel_t rel,long *nodes,double *elements){
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
      elem_count=RTrealloc(elem_count,elem_size*sizeof(bn_int_t));
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

static void
count_set(ATerm set, long *nodes, double *elements)
{
  long idx;

  count_is=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
  elem_size=HASH_INIT;
  elem_count=RTmalloc(elem_size*sizeof(bn_int_t));
  for(int i=0;i<elem_size;i++) bn_init(&elem_count[i]);
  node_count=2; // atom and emptyset
  idx=ATindexedSetPut(count_is,(ATerm)emptyset,NULL);
  assert(idx<elem_size);
  bn_set_digit(&elem_count[idx],0);
  idx=ATindexedSetPut(count_is,(ATerm)atom,NULL);
  assert(idx<elem_size);
  bn_set_digit(&elem_count[idx],1);
  idx=count_set_2(set);
  if (elements != NULL) *elements = bn_int2double(&elem_count[idx]);
  ATindexedSetDestroy(count_is);
  for(int i=0;i<elem_size;i++) bn_clear(&elem_count[i]);
  RTfree(elem_count);

  if (set == atom || set == emptyset)
      node_count = 1;

  *nodes=node_count;
}

static void set_count_list(vset_t set,long *nodes,double *elements){
  count_set(set->set,nodes,elements);
}

static void rel_count_list(vrel_t rel,long *nodes,double *elements){
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
	int N=(set->p_len < 0)?set->dom->shared.size:set->p_len;
	ATerm vec[N];
	for(int i=0;i<N;i++) vec[i]=(ATerm)ATmakeInt(e[i]);
	set->set=set_add(set->set,vec,N);
}

static void rel_add_list(vrel_t rel,const int* src, const int* dst){
	int N=(rel->p_len < 0)?rel->dom->shared.size:rel->p_len;
	ATerm vec[2*N];
	for(int i=0;i<N;i++) {
		vec[i+i]=(ATerm)ATmakeInt(src[i]);
		vec[i+i+1]=(ATerm)ATmakeInt(dst[i]);
	}
	rel->rel=set_add(rel->rel,vec,2*N);
}

static int set_member_list(vset_t set,const int* e){
	int N=(set->p_len < 0)?set->dom->shared.size:set->p_len;
	ATerm vec[N];
	for(int i=0;i<N;i++) vec[i]=(ATerm)ATmakeInt(e[i]);
	return set_member(set->set,vec);
}



static ATbool set_member_tree_2(ATerm set,const int *a);
static ATerm singleton_tree(const int *a,int len);
static ATerm set_add_tree_2(ATerm set, const int *a,int len,ATbool *new);

static void rel_add_tree(vrel_t rel,const int* src, const int* dst){
	int N=(rel->p_len < 0)?rel->dom->shared.size:rel->p_len;
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
    else if (set==Atom) return ATtrue;
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
    int N=(set->p_len < 0)?set->dom->shared.size:set->p_len;
  // ATbool new;
  set->set=set_add_tree_2(set->set,e,N,NULL);
}

int set_member_tree(vset_t set,const int* e){
  return set_member_tree_2(set->set,e);
}

union enum_context {
    // Used by set_enum and enum_wrap functions:
    struct {
        vset_element_cb cb;
        void *context;
    } enum_cb;
    // Used by set_example and enum_first functions:
    int *enum_elem;
};

static int
vset_enum_wrap(ATerm *a, int len, union enum_context *ctx)
{
    int vec[len];

    for(int i = 0; i < len; i++)
        vec[i] = ATgetInt((ATermInt)a[i]);

    ctx->enum_cb.cb(ctx->enum_cb.context, vec);
    return 0;
}

static int
vset_enum_first(ATerm *a, int len, union enum_context *ctx)
{
    for(int i = 0; i < len; i++)
        ctx->enum_elem[i] = ATgetInt((ATermInt)a[i]);

    return 1;
}

static int
set_enum_2(ATerm set, ATerm *a, int len,
               int (*callback)(ATerm*, int, union enum_context*), int ofs,
               union enum_context *ctx)
{
    int tmp;
    while(ATgetAFun(set) == cons){
        if (ofs < len) {
            a[ofs] = ATgetArgument(set, 0);
            tmp = set_enum_2(ATgetArgument(set, 1), a, len, callback,
                                 ofs + 1, ctx);
            if (tmp) return tmp;
        }

        set = ATgetArgument(set, 2);
    }

    if (ATisEqual(set, atom)) {
        return callback(a, ofs, ctx);
    }

    return 0;
}

static void
set_enum_list(vset_t set, vset_element_cb cb, void* context)
{
    union enum_context ctx = {.enum_cb = {cb, context}};
    int N = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    ATerm vec[N];

    set_enum_2(set->set, vec, N, vset_enum_wrap, 0, &ctx);
}

static void
set_example_list(vset_t set, int *e)
{
    union enum_context ctx = {.enum_elem = e};
    int N = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    ATerm vec[N];

    set_enum_2(set->set, vec, N, vset_enum_first, 0, &ctx);
}

static int
vset_enum_wrap_tree(int *a, int len, union enum_context *ctx)
{
  (void)len;

  ctx->enum_cb.cb(ctx->enum_cb.context, a);
  return 0;
}

static int
vset_enum_tree_first(int *a, int len, union enum_context *ctx)
{
  for (int i = 0; i < len; i++)
      ctx->enum_elem[i] = a[i];

  return 1;
}


static
int set_enum_t2(ATerm set, int *a, int len,
                    int (*callback)(int*, int, union enum_context*),
                    int ofs, int shift, int cur, union enum_context *ctx)
{
  int tmp;

  if (set == Atom)
      return callback(a, ofs, ctx);
  else if (set == Empty)
      return 0;
  else {
    if (ofs < len) {
      a[ofs] = shift + cur - 1; // Recall that 0 cannot be stored
      tmp = set_enum_t2(Down(set), a, len, callback, ofs + 1, 1, 0, ctx);
      if (tmp) return tmp;
    }

    set_enum_t2(Left(set), a, len, callback, ofs, shift<<1, cur, ctx);
    set_enum_t2(Right(set), a, len, callback, ofs, shift<<1, shift|cur, ctx);
    return 0;
  }
}

static void
set_enum_tree(vset_t set, vset_element_cb cb, void* context)
{
    union enum_context ctx = {.enum_cb = {cb, context}};
    int N = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    int vec[N];

    set_enum_t2(set->set, vec, N, vset_enum_wrap_tree, 0, 1, 0, &ctx);
}

static void
set_example_tree(vset_t set, int *e)
{
    union enum_context ctx = {.enum_elem = e};
    int N = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    int vec[N];

    set_enum_t2(set->set,vec,N,vset_enum_tree_first, 0, 1, 0, &ctx);
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
	int N=(set->p_len < 0)?set->dom->shared.size:set->p_len;
	ATerm vec[N];
	ATerm pattern[p_len];
	for(int i=0;i<p_len;i++) pattern[i]=(ATerm)ATmakeInt(match[i]);
	match_cb=cb;
	match_context=context;
	ATermIndexedSet dead_branches=ATindexedSetCreate(HASH_INIT,HASH_LOAD);
	set_enum_match_2(dead_branches,set->set,vec,N,pattern,proj,p_len,vset_match_wrap,0);
	ATindexedSetDestroy(dead_branches);
}

/* return NULL : error, or matched vset */
static ATerm
set_copy_match_2(ATerm set, int len, ATerm*pattern, int *proj, int p_len, int ofs) {
    ATerm key, res, tmp = emptyset;

    // lookup in cache
    key = (ATerm)ATmakeAppl1(match,set);
    res = ATtableGet(global_ct,key);
    if (res) return res;

    res = emptyset;

    if (ATgetAFun(set) == cons) {
        ATerm el=ATgetArgument(set,0);
        if (ofs<len) {
            // if still matching and this offset matches,
            // compare the aterm and return it if it matches
            if (p_len && proj[0]==ofs) {
                // does it match?
                if (ATisEqual(pattern[0],el)) {
                    // try to match the next element, return the result
                    tmp=set_copy_match_2(ATgetArgument(set,1),len,pattern+1, proj+1, p_len-1,ofs+1);
                }
            // not matching anymore or the offset doesn't match
            } else {
                tmp=set_copy_match_2(ATgetArgument(set,1),len,pattern, proj, p_len,ofs+1);
            }
        }
        // test matches in second argument
        if (ofs<=len) {
            res=set_copy_match_2(ATgetArgument(set,2),len,pattern, proj, p_len,ofs);
        }
        // combine results
        res = MakeCons(el, tmp, res);
    } else {
        if (ATisEqual(set,atom) && ofs==len) {
            res=set;
        } else {
            res=emptyset;
        }
    }
    ATtablePut(global_ct,key,res);
    return res;
}

static void set_copy_match_list(vset_t dst,vset_t src,int p_len,int* proj,int*match) {
	int N=(src->p_len < 0)?src->dom->shared.size:src->p_len;
	ATerm pattern[p_len];
	for(int i=0;i<p_len;i++) pattern[i]=(ATerm)ATmakeInt(match[i]);
	dst->set = set_copy_match_2(src->set,N,pattern,proj,p_len,0);

	ATtableReset(global_ct);
}

static ATerm set_union_2(ATerm s1, ATerm s2,char lookup) {
  if (s1==atom) return atom;
  else if (s1==emptyset) return s2;
  else if (s2==emptyset) return s1;
  else if (s1==s2) return s1;
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
    assert(dst->p_len == src->p_len);
	dst->set=set_union_2(dst->set,src->set,0);
	ATtableReset(global_ct);
}

static ATerm set_intersect_2(ATerm s1, ATerm s2,char lookup) {
  if (s1==atom && s2==atom) return atom;
  else if (s1==emptyset) return emptyset;
  else if (s2==emptyset) return emptyset;
  else { ATerm key=NULL,res=NULL;
    if (lookup) {
      key = (ATerm)ATmakeAppl2(intersect,s1,s2);
      res = ATtableGet(global_ct,key);
      if (res) return res;
    }
    { // either not looked up, or not found in cache: compute
      ATerm x = ATgetArgument(s1,0);
      ATerm y = ATgetArgument(s2,0);
      int c = ATcmp(x,y);
      if (c==0)     res=MakeCons(x, set_intersect_2(ATgetArgument(s1,1),ATgetArgument(s2,1),1),
			            set_intersect_2(ATgetArgument(s1,2),ATgetArgument(s2,2),1));
      // x < y
      else if (c<0) res=set_intersect_2(ATgetArgument(s1,2),s2,1);
      else          res = set_intersect_2(s1, ATgetArgument(s2,2),1);

      if (lookup) ATtablePut(global_ct,key,res);
      return res;
    }
  }
}

static void set_intersect_list(vset_t dst,vset_t src){
    assert(dst->p_len == src->p_len);
	dst->set=set_intersect_2(dst->set,src->set,0);
	ATtableReset(global_ct);
}

static ATerm set_minus_2(ATerm a,ATerm b, char lookup) {
  if (b==emptyset) return a;
  else if (a==emptyset) return emptyset;
  else if (b==atom) return emptyset;
  else if (a==b) return emptyset;
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
    assert(dst->p_len == src->p_len);
	dst->set=set_minus_2(dst->set,src->set,0);
	ATtableReset(global_ct);
}

static void set_zip_2(ATerm in1,ATerm in2,ATerm *out1,ATerm *out2, char lookup){
  if (in1==atom) {*out1=atom; *out2=emptyset; return;}
  else if (in1==emptyset) {*out1=*out2=in2; return;}
  else if (in2==emptyset) {*out1=in1; *out2=in2; return;}
  else if (in1==in2) {*out1=in1,*out2=emptyset; return;}
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
    assert(dst->p_len >= 0 && src->p_len < 0);
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
  while ((ATgetAFun(set)==cons)&&(ATgetAFun(trans)==cons)){
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
  if (set == emptyset || trans == emptyset)
    return emptyset;
  else if (p_len==0)
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
    assert(dst->p_len < 0 && src->p_len < 0);
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
    while ((ATgetAFun(trans)==cons) && ATgetAFun(set)==cons) {
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
    if (set == emptyset || trans == emptyset) {
        return emptyset;
    } if (p_len == 0) {
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

void set_prev_list(vset_t dst, vset_t src, vrel_t rel, vset_t univ) {
    assert(dst->p_len < 0 && src->p_len < 0);
    dst->set=set_inv_reach(src->set, rel->rel, rel->proj, rel->p_len, 0, 0);
    ATtableReset(global_ct);
    set_intersect_list(dst, univ);
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
    assert(dst->p_len == src->p_len);
	dst->set=set_union_tree_2(dst->set,src->set,0);
	ATtableReset(global_ct);
}


#if 0
intersect Atom Atom = Atom
intersect Empty _   = Empty
intersect _ Empty = Empty
intersect (Cons dstdown dstleft dstright) (Cons srcdown srcleft srcright) = Cons (intersect dstdown srcdown) (intersect dstleft srcleft) (intersect dstright srcright)
#endif

static ATerm set_intersect_tree_2(ATerm s1, ATerm s2) {
    if (s1==Atom && s2==Atom) return Atom;
    else if (s1==Empty) return Empty;
    else if (s2==Empty) return Empty;
    else {
        ATerm key=NULL,res=NULL;
        key = (ATerm)ATmakeAppl2(intersect,s1,s2);
        res = ATtableGet(global_ct,key);
        if (res) return res;
        // either not looked up, or not found in cache: compute
        res = MCons(
                set_intersect_tree_2(Down(s1),  Down(s2)),
                set_intersect_tree_2(Left(s1),  Left(s2)),
                set_intersect_tree_2(Right(s1), Right(s2))
              );
        ATtablePut(global_ct,key,res);
        return res;
    }
}

static void set_intersect_tree(vset_t dst, vset_t src) {
    assert(dst->p_len == src->p_len);
	dst->set=set_intersect_tree_2(dst->set,src->set);
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
    assert(dst->p_len >= 0 && src->p_len < 0);
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
    assert(dst->p_len == src->p_len);
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
    assert(dst->p_len < 0 && src->p_len < 0);
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
                     copy_level_prev_tree(Left(set) , trans, proj, p_len, ofs),
                     copy_level_prev_tree(Right(set), trans, proj, p_len, ofs));
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

void set_prev_tree(vset_t dst,vset_t src,vrel_t rel,vset_t univ){
    assert(dst->p_len < 0 && src->p_len < 0);
    dst->set=set_prev_tree_2(src->set, rel->rel, rel->proj, rel->p_len, 0, 0);
    ATtableReset(global_ct);
    set_intersect_list(dst,univ);
}


static void reorder() {}

static ATerm
set_copy_match_tree_2(ATerm set, int len,int *matchv, int *proj, int p_len, int ofs, int shift, int cur) {
    ATerm key, res;

    // lookup in cache
    key = (ATerm)ATmakeAppl3(match3,set, (ATerm)ATmakeInt(p_len), (ATerm)ATmakeInt(shift+cur-1));
    res = ATtableGet(global_ct,key);
    if (res) return res;

    res = Empty;

    if (ATgetAFun(set) == cons) {
        // should always be true, check anyway
        if (ofs<=len) {
            // if still matching and this offset matches,
            // compare the match to the computed current-1
            if (p_len && proj[0]==ofs) {
                // does it match?
                if (matchv[0] == shift+cur-1) { // remember: can't store zero, thus -1
                    // try to match the next element, return the result
                    res = MCons(
                            set_copy_match_tree_2(Down(set), len, matchv+1, proj+1, p_len-1, ofs+1, 1, 0),
                            Empty,
                            Empty
                          );
                } else {
                    res = MCons(
                            Empty,
                            set_copy_match_tree_2(Left(set), len, matchv, proj, p_len, ofs, shift<<1, cur),
                            set_copy_match_tree_2(Right(set), len, matchv, proj, p_len, ofs, shift<<1, shift|cur)
                          );
                }
            // not matching anymore or the offset doesn't match
            } else {
                res = MCons(
                        set_copy_match_tree_2(Down(set), len, matchv, proj, p_len, ofs+1, 1, 0),
                        set_copy_match_tree_2(Left(set), len, matchv, proj, p_len, ofs, shift<<1, cur),
                        set_copy_match_tree_2(Right(set), len, matchv, proj, p_len, ofs, shift<<1, shift|cur)
                      );
            }
        }
    } else {
        if (ATisEqual(set,Atom) && ofs>=len) {
            res=set;
        } else {
            res=Empty;
        }
    }
    ATtablePut(global_ct,key,res);
    return res;
}

static void set_copy_match_tree(vset_t dst,vset_t src, int p_len,int* proj,int*match){
    assert(dst->p_len == src->p_len);
    int N=(src->p_len < 0)?src->dom->shared.size:src->p_len;
    if (p_len == 0) {
        dst->set = src->set;
    } else {
        dst->set = set_copy_match_tree_2(src->set,N,match,proj,p_len,0, 1, 0);
        ATtableReset(global_ct);
    }
}

static void
set_enum_match_tree(vset_t set, int p_len, int *proj, int *match,
                        vset_element_cb cb, void *context)
{
    int N = (set->p_len < 0)?set->dom->shared.size:set->p_len;
    ATerm match_set;

    if (p_len == 0) {
        match_set = set->set;
    } else {
        match_set = set_copy_match_tree_2(set->set, N, match, proj, p_len,
                                              0, 1, 0);
        ATtableReset(global_ct);
    }

    union enum_context ctx = {.enum_cb = {cb, context}};
    int vec[N];

    set_enum_t2(match_set, vec, N, vset_enum_wrap_tree, 0, 1, 0, &ctx);
}

// Structure for storing transition groups at top levels.
typedef struct {
    int tg_len;
    int *top_groups;
} top_groups_info;

static vrel_t *rel_set;
static vset_t *proj_set;
static top_groups_info *top_groups;

static ATerm saturate(int level, ATerm set);
static ATerm sat_rel_prod(ATerm set, ATerm trans, int *proj, int p_len,
                          int ofs, int grp);

// Initialize a global memoization table
static ATermTable
reset_table(ATermTable table)
{
    ATermTable new_table = table;

    if (new_table != NULL)
        ATtableDestroy(new_table);

    return ATtableCreate(HASH_INIT, HASH_LOAD);
}

static ATerm
set_union_sat(ATerm s1, ATerm s2, int lookup)
{
    if (s1 == atom) return atom;
    if (s1 == emptyset) return s2;
    if (s2 == emptyset) return s1;
    if (s1 == s2) return s1;

    ATerm key = NULL, res = NULL;

    if (lookup) {
        key = (ATerm)ATmakeAppl2(sum, s1, s2);
        res = ATtableGet(global_uc, key);
        if (res) return res;
    }

    // not looked up or not found in cache: compute
    ATerm x = ATgetArgument(s1, 0);
    ATerm y = ATgetArgument(s2, 0);
    int c = ATcmp(x, y);

    if (c==0)
        res=Cons(x, set_union_sat(ATgetArgument(s1,1),ATgetArgument(s2,1),1),
                 set_union_sat(ATgetArgument(s1,2),ATgetArgument(s2,2),0));
    else if (c<0)
        res=Cons(x, ATgetArgument(s1,1),
                 set_union_sat(ATgetArgument(s1,2),s2,0));
    else
        res = Cons(y, ATgetArgument(s2,1),
                   set_union_sat(s1,ATgetArgument(s2,2),0));

    if (lookup) ATtablePut(global_uc, key, res);
    return res;
}

static ATerm
copy_level_sat(ATerm set, ATerm trans, int *proj, int p_len, int ofs, int grp)
{
    if (set==emptyset)
        return emptyset;
    else
        return MakeCons(ATgetArgument(set, 0),
                          sat_rel_prod(ATgetArgument(set, 1), trans, proj,
                                       p_len, ofs + 1, grp),
                          copy_level_sat(ATgetArgument(set, 2), trans, proj,
                                         p_len, ofs, grp));
}

static ATerm
trans_level_sat(ATerm set, ATerm trans, int *proj, int p_len, int ofs, int grp)
{
    if (trans == emptyset)
        return emptyset;
    else
        return MakeCons(ATgetArgument(trans, 0),
                            sat_rel_prod(set, ATgetArgument(trans, 1),
                                         proj + 1, p_len-1, ofs + 1, grp),
                            trans_level_sat(set, ATgetArgument(trans, 2),
                                            proj, p_len, ofs, grp));
}

static ATerm
apply_rel_prod(ATerm set, ATerm trans, int *proj, int p_len, int ofs, int grp)
{
    ATerm res = emptyset;

    while((ATgetAFun(set) == cons) && (ATgetAFun(trans) == cons)) {
        int c = ATcmp(ATgetArgument(set, 0), ATgetArgument(trans, 0));

        if (c < 0)
            set = ATgetArgument(set, 2);
        else if (c > 0)
            trans = ATgetArgument(trans, 2);
        else {
            res = set_union_sat(res, trans_level_sat(ATgetArgument(set,1),
                                                     ATgetArgument(trans,1),
                                                     proj, p_len, ofs, grp), 0);
            set = ATgetArgument(set,2);
            trans = ATgetArgument(trans,2);
        }
    }

    return res;
}

// Get memoized rel_prod value
static inline ATerm
get_rel_prod_value(int lvl, int grp, ATerm set, ATerm trans)
{
    ATerm lvlNode = (ATerm) ATmakeInt(lvl);
    ATerm grpNode = (ATerm) ATmakeInt(grp);
    ATerm key = (ATerm) ATmakeAppl4(rel_prod, lvlNode, grpNode, set, trans);
    return ATtableGet(global_rc, key);
}

// Memoize rel_prod value
static inline void
put_rel_prod_value(int lvl, int grp, ATerm set, ATerm trans, ATerm value)
{
    ATerm lvlNode = (ATerm) ATmakeInt(lvl);
    ATerm grpNode = (ATerm) ATmakeInt(grp);
    ATerm key = (ATerm) ATmakeAppl4(rel_prod, lvlNode, grpNode, set, trans);
    ATtablePut(global_rc, key, value);
}

static ATerm
sat_rel_prod(ATerm set, ATerm trans, int *proj, int p_len, int ofs, int grp)
{
    if (p_len == 0)
        return set;
    else {
        ATerm res = get_rel_prod_value(ofs, grp, set, trans);
        if (res)
            return res;

        if (proj[0] == ofs)
            res = apply_rel_prod(set,trans,proj,p_len,ofs,grp);
        else
            res = copy_level_sat(set,trans,proj,p_len,ofs,grp);

        res = saturate(ofs, res);
        put_rel_prod_value(ofs, grp, set, trans, res);
        return res;
    }
}

static ATerm
apply_rel_fixpoint(ATerm set, ATerm trans, int *proj, int p_len,
                   int ofs, int grp)
{
    ATerm res=set;

    while((ATgetAFun(set) == cons) && (ATgetAFun(trans) == cons)) {
        int c = ATcmp(ATgetArgument(set, 0), ATgetArgument(trans, 0));

        if (c < 0)
            set = ATgetArgument(set, 2);
        else if (c > 0)
            trans = ATgetArgument(trans, 2);
        else {
            ATerm new_res     = res;
            ATerm trans_value = ATgetArgument(trans, 0);
            ATerm res_value   = ATgetArgument(res, 0);

            while (!ATisEqual(res_value, trans_value)) {
                new_res   = ATgetArgument(new_res, 2);
                res_value = ATgetArgument(new_res, 0);
            }

            res = set_union_sat(res, trans_level_sat(ATgetArgument(new_res, 1),
                                                     ATgetArgument(trans, 1),
                                                     proj, p_len, ofs, grp), 0);
            set = ATgetArgument(set,2);
            trans = ATgetArgument(trans,2);
        }
    }

    return res;
}

// Start fixpoint calculations on the MDD at a given level for transition groups
// whose top is at that level. Continue performing fixpoint calculations until
// the MDD does not change anymore.
static ATerm
sat_fixpoint(int level, ATerm set)
{
    if (ATisEqual(set, emptyset) || ATisEqual(set, atom))
        return set;

    top_groups_info groups_info = top_groups[level];
    ATerm old_set = emptyset;
    ATerm new_set = set;

    while (!ATisEqual(old_set, new_set)) {
        old_set = new_set;
        for (int i = 0; i < groups_info.tg_len; i++) {
            int grp = groups_info.top_groups[i];

            // Update transition relations
            if (rel_set[grp]->expand != NULL) {
                proj_set[grp]->set = set_project_2(new_set, level,
                                                   proj_set[grp]->proj,
                                                   proj_set[grp]->p_len, 0);
                rel_set[grp]->expand(rel_set[grp], proj_set[grp],
                                     rel_set[grp]->expand_ctx);
                proj_set[grp]->set = emptyset;
                ATtableReset(global_ct);
            }

            new_set = apply_rel_fixpoint(new_set, rel_set[grp]->rel,
                                         rel_set[grp]->proj,
                                         rel_set[grp]->p_len, level, grp);
        }
    }

    return new_set;
}

// Traverse the local state values of an MDD node recursively:
// - Base case: end of MDD node is reached OR MDD node is atom node.
// - Induction: construct a new MDD node with the link to next entry of the
//   MDD node handled recursively
static ATerm
saturate_level(int level, ATerm node_set)
{
    if (ATisEqual(node_set, emptyset) || ATisEqual(node_set, atom))
        return node_set;

    ATerm sat_set = saturate(level + 1, ATgetArgument(node_set, 1));
    ATerm new_node_set = saturate_level(level, ATgetArgument(node_set, 2));
    return MakeCons(ATgetArgument(node_set, 0), sat_set, new_node_set);
}

// Saturation process for the MDD at a given level
static ATerm
saturate(int level, ATerm set)
{
    ATerm new_set = ATtableGet(global_sc, set);

    if (new_set)
        return new_set;

    new_set = saturate_level(level, set);
    new_set = sat_fixpoint(level, new_set);
    ATtablePut(global_sc, set, new_set);
    return new_set;
}

// Perform fixpoint calculations using the "General Basic Saturation" algorithm
static void
set_least_fixpoint_list(vset_t dst, vset_t src, vrel_t rels[], int rel_count)
{
    // Only implemented if not projected
    assert(src->p_len < 0 && dst->p_len < 0);

    // Initialize global hash tables.
    global_ct = reset_table(global_ct);
    global_sc = reset_table(global_sc);
    global_rc = reset_table(global_rc);
    global_uc = reset_table(global_uc);

    // Initialize partitioned transition relations and expansions.
    rel_set = rels;

    // Initialize top_groups_info array
    // This stores transition groups per topmost level
    int  init_state_len = src->dom->shared.size;
    top_groups = RTmalloc(sizeof(top_groups_info[init_state_len]));
    proj_set = RTmalloc(sizeof(vset_t[rel_count]));

    for (int lvl = 0; lvl < init_state_len; lvl++) {
        top_groups[lvl].top_groups = RTmalloc(sizeof(int[rel_count]));
        top_groups[lvl].tg_len = 0;
    }

    for (int grp = 0; grp < rel_count; grp++) {
        proj_set[grp] = set_create_both(rels[grp]->dom, rels[grp]->p_len,
                                        rels[grp]->proj);

        if (rels[grp]->p_len == 0)
            continue;

        int top_lvl = rels[grp]->proj[0];
        top_groups[top_lvl].top_groups[top_groups[top_lvl].tg_len] = grp;
        top_groups[top_lvl].tg_len++;
    }

    // Saturation on initial state set
    dst->set = saturate(0, src->set);

    // Clean-up
    for (int grp = 0; grp < rel_count; grp++) {
        if (rels[grp]->p_len == 0 && rels[grp]->expand != NULL) {
            proj_set[grp]->set = set_project_2(dst->set, 0, NULL, 0, 0);
            rel_set[grp]->expand(rel_set[grp], proj_set[grp],
                                 rel_set[grp]->expand_ctx);
            ATtableReset(global_ct);
        }

        vset_destroy(proj_set[grp]);
    }

    for (int lvl = 0; lvl < init_state_len; lvl++)
        RTfree(top_groups[lvl].top_groups);

    rel_set = NULL;

    RTfree(proj_set);
    RTfree(top_groups);
    ATtableReset(global_ct);
    ATtableReset(global_sc);
    ATtableReset(global_rc);
    ATtableReset(global_uc);
}

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
	dom->shared.set_enum_match=set_enum_match_tree;
	dom->shared.set_copy_match=set_copy_match_tree;
	dom->shared.set_example=set_example_tree;
	dom->shared.set_count=set_count_tree;
	dom->shared.rel_count=rel_count_tree;
    dom->shared.set_union=set_union_tree;
    dom->shared.set_intersect=set_intersect_tree;
    dom->shared.set_minus=set_minus_tree;
	dom->shared.set_zip=set_zip_tree;
	dom->shared.set_project=set_project_tree;
	dom->shared.rel_create=rel_create_both; // initializes with emptyset instead of empty? wrong? -> all both functions mix this..
	dom->shared.rel_add=rel_add_tree;
	dom->shared.set_next=set_next_tree;
	dom->shared.set_prev=set_prev_tree;
	dom->shared.reorder=reorder;
    dom->shared.set_destroy=set_destroy_both;
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
	dom->shared.set_copy_match=set_copy_match_list;
	dom->shared.set_example=set_example_list;
	dom->shared.set_count=set_count_list;
	dom->shared.rel_count=rel_count_list;
	dom->shared.set_union=set_union_list;
    dom->shared.set_intersect=set_intersect_list;
	dom->shared.set_minus=set_minus_list;
	dom->shared.set_zip=set_zip_list;
	dom->shared.set_project=set_project_list;
	dom->shared.rel_create=rel_create_both;
	dom->shared.rel_add=rel_add_list;
	dom->shared.set_next=set_next_list;
	dom->shared.set_prev=set_prev_list;
	dom->shared.reorder=reorder;
    dom->shared.set_destroy=set_destroy_both;
    dom->shared.set_least_fixpoint= set_least_fixpoint_list;
	return dom;
}

