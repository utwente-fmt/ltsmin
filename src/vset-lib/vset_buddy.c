#include <hre/config.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include <fdd.h>
#include <hre/user.h>
#include <vset-lib/vdom_object.h>

static int fdd_bits=16;
static int cacheratio=64;
static int maxincrease=1000000;
static int minfreenodes=20;
static const char *fdd_reorder_opt="none";
static int fdd_order_strat=BDD_REORDER_NONE;

static void vset_fdd_gbchandler(int pre, bddGbcStat *s) {
  if (!pre && log_active(infoLong)) {
    Warning(info,"Garbage collection #%d: %d nodes / %d free / %.1fs / %.1fs total",
	    s->num, s->nodes, s->freenodes,
	    (float)s->time/(float)(CLOCKS_PER_SEC),
	    (float)s->sumtime/(float)CLOCKS_PER_SEC);
  }
}

static void buddy_init(){
	static int initialized=0;
	if (!initialized) {
		bdd_init(1000000, 100000);
		Warning (info,"Buddy dynamic reordering strategy: %s",fdd_reorder_opt);
		Warning(info,"ratio %d, maximum increase %d, minimum free %d",cacheratio,maxincrease,minfreenodes);
		bdd_setcacheratio(cacheratio);
		bdd_setmaxincrease(maxincrease);
		bdd_setminfreenodes(minfreenodes);
                bdd_gbc_hook(vset_fdd_gbchandler);
		initialized=1;
	}
}

static void buddy_popt(poptContext con,
			enum poptCallbackReason reason,
			const struct poptOption * opt,
			const char * arg, void * data) {
  (void)con;(void)opt;(void)arg;(void)data;
  if (reason != POPT_CALLBACK_REASON_POST) 
    Abort("unexpected call to buddy_popt");
  
  if (!strcmp(fdd_reorder_opt,"none"))
    fdd_order_strat=BDD_REORDER_NONE;
  else if (!strcmp(fdd_reorder_opt,"win2"))
    fdd_order_strat=BDD_REORDER_WIN2;
  else if (!strcmp(fdd_reorder_opt,"win2ite"))
    fdd_order_strat=BDD_REORDER_WIN2ITE;
  else if (!strcmp(fdd_reorder_opt,"win3"))
    fdd_order_strat=BDD_REORDER_WIN3;
  else if (!strcmp(fdd_reorder_opt,"win3ite"))
    fdd_order_strat=BDD_REORDER_WIN3ITE;
  else if (!strcmp(fdd_reorder_opt,"sift"))
    fdd_order_strat=BDD_REORDER_SIFT;
  else if (!strcmp(fdd_reorder_opt,"siftite"))
    fdd_order_strat=BDD_REORDER_SIFTITE;
  else if (!strcmp(fdd_reorder_opt,"random"))
    fdd_order_strat=BDD_REORDER_RANDOM;
  else
    Abort("BuDDy reordering strategy not recognized: %s",fdd_reorder_opt);

  return;
}

struct poptOption buddy_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)buddy_popt , 0 , NULL , NULL },

	{ "cache-ratio",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cacheratio , 0 , "set cache ratio","<nodes/slot>"},
	{ "max-increase" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxincrease , 0 , "set maximum increase","<number>"},
	{ "min-free-nodes", 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &minfreenodes , 0 , "set minimum free node percentage","<percentage>"},
	{ "fdd-bits" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &fdd_bits , 0 , "set the number of bits for each fdd variable","<number>"},
	{ "fdd-reorder", 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &fdd_reorder_opt, 0 , "set the dynamic reordering strategy","<none | win2 | win2ite | win3 | win3ite | sift | siftite | random>" },
	POPT_TABLEEND
};

struct vector_domain {
	struct vector_domain_shared shared;
	//BDD **vals;
	int *vars;
	BDD varset;
	int *vars2;
	int *proj;
};

static BDD mkvar(vdom_t dom,int idx,int val){
  	return bdd_addref(fdd_ithvar(dom->vars[idx],val));
}
static void rmvar(BDD var) {
  	bdd_delref(var);
}
static BDD mkvar2(vdom_t dom,int idx,int val){
  	return bdd_addref(fdd_ithvar(dom->vars2[idx],val));
}
static void rmvar2(BDD var) {
  	bdd_delref(var);
}

struct vector_set {
	vdom_t dom;
	BDD bdd;
	BDD p_set; // variables in the projection.
	BDD c_set; // variables in the complement.
	int p_len;
	int* proj;
};

struct vector_relation {
	vdom_t dom;
    expand_cb expand;
    void *expand_ctx;
	BDD bdd;
	BDD p_set; // variables in the projection.
	BDD p_prime_set; // primed variables in the projection
	int p_len;
	int* proj;
	BDD rel_set; // variables + primed variables in the projection.
	bddPair *pairs;
	bddPair *inv_pairs;
};

static vset_t set_create_fdd(vdom_t dom,int k,int* proj){
	vset_t set=(vset_t)RTmalloc(sizeof(struct vector_set));
	set->dom=dom;
	set->bdd=bddfalse;
	if (k >= 0 && k<dom->shared.size) {
		int vars[k];
		int complement[dom->shared.size-k];
		set->p_len=k;
		set->proj=(int*)RTmalloc(sizeof(int[k]));
		int i=0;
		int j=0;
		for(int v=0;v<dom->shared.size;v++){
			if (i < k && v == proj[i]) {
				// influenced
				vars[i]=dom->vars[v];
				set->proj[i]=v;
				i++;
			} else {
				// not influenced;
				complement[j]=dom->vars[v];
				j++;
			}
		}
		set->p_set=bdd_addref(fdd_makeset(vars,k));
		set->c_set=bdd_addref(fdd_makeset(complement,dom->shared.size-k));
	} else {
		set->p_len=dom->shared.size;
		set->p_set=dom->varset;
		set->c_set=bddtrue;
		set->proj=dom->proj;
	}
	return set;
}

static void set_destroy_fdd(vset_t set) {
    // the domain is a public object, don't clear
    bdd_delref(set->bdd);
    if (set->p_set != set->dom->varset)
        bdd_delref(set->p_set);
    if (set->c_set != bddtrue)
        bdd_delref(set->c_set);
    // free projection complement variables
    set->p_len = 0;
    if (set->proj != set->dom->proj)
        RTfree(set->proj);
    // free set
    RTfree(set);
    return;
}

static vrel_t rel_create_fdd(vdom_t dom,int k,int* proj){
    vrel_t rel=(vrel_t)RTmalloc(sizeof(struct vector_relation));
    rel->dom=dom;
    rel->bdd=bddfalse;

    assert (0 <= k && k <= dom->shared.size);

    int vars[k];
    int vars2[k];
    int allvars[2*k];
    rel->p_len=k;
    if (k==dom->shared.size) {
        rel->proj = dom->proj;
    } else {
        rel->proj=(int*)RTmalloc(sizeof(int[k]));
    }
    for(int i=0;i<k;i++) {
        if (k!=dom->shared.size) rel->proj[i]=proj[i];
        vars[i] = dom->vars[proj[i]];
        vars2[i]= dom->vars2[proj[i]];
        allvars[2*i]=vars[i];
        allvars[2*i+1]=vars2[i]; // hidden assumption on encoding
    }
    // for next function
    rel->pairs=bdd_newpair();
    int res=fdd_setpairs(rel->pairs,vars2,vars,k);
    if (res<0){
        Abort("BuDDy error: %s",bdd_errstring(res));
    }
    // for prev function
    rel->inv_pairs=bdd_newpair();
    res=fdd_setpairs(rel->inv_pairs,vars,vars2,k);
    if (res<0){
        Abort("BuDDy error: %s",bdd_errstring(res));
    }
    if (k==dom->shared.size) {
        rel->p_set = dom->varset;
    } else {
        rel->p_set=bdd_addref(fdd_makeset(vars,k));
    }
    rel->rel_set=bdd_addref(fdd_makeset(allvars,2*k));
    rel->p_prime_set=bdd_addref(bdd_replace(rel->p_set, rel->inv_pairs));
	return rel;
}

static inline BDD fdd_element(vset_t set,const int* e){
	int N=set->p_len;
	BDD bdd=bddtrue;
	for(int i=N-1;i>=0;i--){
		BDD val=mkvar(set->dom,set->proj[i],e[i]);
		BDD tmp=bdd;
		bdd=bdd_addref(bdd_and(bdd,val));
		bdd_delref(tmp);
		rmvar(val);
	}
	//printf("element: ");
	//fdd_printset(bdd);
	//printf("\n");
	return bdd;
}

static BDD fdd_pair(vrel_t rel,const int* e1,const int*e2){
//	Warning(info,"args %x %x %x",rel,e1,e2);
	int N=rel->p_len;
//	Warning(info,"N: %d %d",N,rel->p_len);
	BDD bdd=bddtrue;
	for(int i=N-1;i>=0;i--){
		BDD val=mkvar2(rel->dom,rel->proj[i],e2[i]);
		BDD tmp=bdd;
		bdd=bdd_addref(bdd_and(bdd,val));
		bdd_delref(tmp);
		rmvar2(val);
		val=mkvar(rel->dom,rel->proj[i],e1[i]);
		tmp=bdd;
		bdd=bdd_addref(bdd_and(bdd,val));
		bdd_delref(tmp);
		rmvar(val);
	}
	//printf("element: ");
	//fdd_printset(bdd);
	//printf("\n");
	return bdd;
}

static void set_add_fdd(vset_t set,const int* e){
	BDD bdd=fdd_element(set,e);
	BDD tmp=set->bdd;
	set->bdd=bdd_addref(bdd_or(set->bdd,bdd));
	bdd_delref(bdd);
	bdd_delref(tmp);
	//printf("set: ");
	//fdd_printset(set->bdd);
	//printf("\n");
}
/*
static void set_add_check_fdd(vset_t set,int* e,int*new_e){
	BDD bdd=fdd_element(set,e);
	BDD tmp=set->bdd;
	set->bdd=bdd_addref(bdd_or(set->bdd,bdd));
	*new_e=(tmp!=set->bdd);
	bdd_delref(bdd);
	bdd_delref(tmp);
}
*/

static int set_member_fdd(vset_t set,const int* e){
	BDD ebdd=fdd_element(set,e);
	int res=(bdd_and(set->bdd,ebdd)!=bddfalse);
	bdd_delref(ebdd);
	return res;
}

static int set_is_empty_all(vset_t set){
	return (set->bdd==bddfalse);
}

static int set_equal_all(vset_t set1,vset_t set2){
	return (set1->bdd==set2->bdd);
}

static void set_clear_all(vset_t set){
	bdd_delref(set->bdd);
	set->bdd=bddfalse;
}

static void set_copy_all(vset_t dst,vset_t src){
	bdd_delref(dst->bdd);
	dst->bdd=src->bdd;
	bdd_addref(dst->bdd);
}

static void vset_enum_do_fdd(vdom_t dom,BDD set,int* proj,int *vec,int i,vset_element_cb cb,void* context){
    if (set == bddfalse) {
        return;
	} else if (i == -1 || set == bddtrue) {
		cb(context,vec);
	} else {
		for(;;){
			int v=fdd_scanvar(set,dom->vars[proj[i]]);
			if (v<0) break;
			vec[i]=v;
			BDD val=mkvar(dom,proj[i],v);
			BDD subset=bdd_addref(bdd_and(set,val));
			BDD tmp=set;
			set=bdd_addref(bdd_apply(set,val,bddop_diff));
			bdd_delref(tmp);
			rmvar(val);
			vset_enum_do_fdd(dom,subset,proj,vec,i-1,cb,context);
		}
	}
	bdd_delref(set);
}

static void set_enum_fdd(vset_t set,vset_element_cb cb,void* context){
    int N=set->p_len;
    int vec[N];
    bdd_addref(set->bdd);
    vset_enum_do_fdd(set->dom,set->bdd,set->proj,vec,N-1,cb,context);
}

static void vset_example_do_fdd(vdom_t dom, BDD set, int *proj, int *vec, int i){
    if (i < 0) {
        return;
    } else {
        int v = fdd_scanvar(set, dom->vars[proj[i]]);
        assert(v >= 0); // set cannot be empty
        vec[i] = v;
        BDD val = mkvar(dom, proj[i], v);
        BDD subset = bdd_addref(bdd_and(set, val));
        rmvar(val);
        vset_example_do_fdd(dom, subset, proj, vec, i - 1);
        bdd_delref(subset);
    }
}

static void set_example_fdd(vset_t set, int *e){
    assert(set->bdd != bddfalse);
    int N = set->p_len;
    bdd_addref(set->bdd);
    vset_example_do_fdd(set->dom, set->bdd, set->proj, e, N-1);
    bdd_delref(set->bdd);
}

static void set_enum_match_fdd(vset_t set,int p_len,int* proj,int*match,vset_element_cb cb,void* context){
    BDD subset;
    if (p_len == 0 && set->bdd != bddfalse)
        subset = bddtrue;
    else
        subset = set->bdd;
    bdd_addref(subset);
    for(int i=0;i<p_len;i++){
        BDD val=mkvar(set->dom,proj[i],match[i]);
        BDD tmp=bdd_addref(bdd_and(subset,val));
        bdd_delref(subset);
        subset=tmp;
        rmvar(val);
    }
    int N=set->p_len;
    int vec[N];
    vset_enum_do_fdd(set->dom,subset,set->proj,vec,N-1,cb,context);
}

static void set_copy_match_fdd(vset_t dst,vset_t src,int p_len,int* proj,int*match){
    assert(p_len >= 0);
    // delete reference to dst
    bdd_delref(dst->bdd);
    // use dst->bdd as subset
    if (p_len == 0 && src->bdd != bddfalse)
        dst->bdd = bddtrue;
    else
        dst->bdd=src->bdd;
    bdd_addref(dst->bdd);
    for(int i=0;i<p_len;i++){
        BDD val=mkvar(src->dom,proj[i],match[i]);
        BDD tmp=bdd_addref(bdd_and(dst->bdd,val));
        bdd_delref(dst->bdd);
        dst->bdd=tmp;
        rmvar(val);
    }
}

static void count_fdd(BDD bdd, BDD p_set,long *nodes,double *elements)
{
    if (nodes != NULL) *nodes=bdd_nodecount(bdd);
    if (elements != NULL) {
        *elements=bdd_satcountlnset(bdd,p_set);
        //Warning(info,"log of satcount is %f",count);
        if (*elements == 0.0 && bdd != bddtrue) {
            // count is zero or one
            *elements=bdd_satcountset(bdd, p_set);
        } else {
            *elements=round(pow(2.0, *elements));
        }
    }
}

static void set_count_fdd(vset_t set,long *nodes,double *elements){
  count_fdd(set->bdd,set->p_set,nodes,elements);
}

static void rel_count_fdd(vrel_t rel,long *nodes,double *elements){
  count_fdd(rel->bdd,rel->rel_set,nodes,elements);
}

static void set_union_fdd(vset_t dst,vset_t src){
	BDD tmp=dst->bdd;
	dst->bdd=bdd_addref(bdd_apply(tmp,src->bdd,bddop_or));
	bdd_delref(tmp);
}

static void set_intersect_fdd(vset_t dst,vset_t src){
	BDD tmp=dst->bdd;
	dst->bdd=bdd_addref(bdd_apply(tmp,src->bdd,bddop_and));
	bdd_delref(tmp);
}

static void set_minus_fdd(vset_t dst,vset_t src){
	BDD tmp=dst->bdd;
	dst->bdd=bdd_addref(bdd_apply(tmp,src->bdd,bddop_diff));
	bdd_delref(tmp);
}

/*
static void set_next_fdd(vset_t dst,vset_t src,vrel_t rel){
	BDD tmp=bdd_addref(bdd_and(src->bdd,rel->bdd));
	bdd_delref(dst->bdd);
	BDD tmp2=bdd_addref(bdd_exist(tmp,rel->p_set));
	bdd_delref(tmp);
	dst->bdd=bdd_addref(bdd_replace(tmp2,rel->pairs));
	bdd_delref(tmp2);
}
*/

static void set_next_appex_fdd(vset_t dst,vset_t src,vrel_t rel){
  BDD tmp=bdd_addref(bdd_appex(src->bdd,rel->bdd,bddop_and,rel->p_set));
  bdd_delref(dst->bdd);
  dst->bdd=bdd_addref(bdd_replace(tmp,rel->pairs));
  bdd_delref(tmp);
}

static void set_prev_appex_fdd(vset_t dst, vset_t src, vrel_t rel, vset_t univ) {
    BDD tmp1=bdd_addref(bdd_replace(src->bdd,rel->inv_pairs));
    bdd_delref(dst->bdd);
    dst->bdd=bdd_addref(bdd_appex(tmp1,rel->bdd,bddop_and,rel->p_prime_set));
    bdd_delref(tmp1);
    set_intersect_fdd(dst,univ);
}

static void set_project_fdd(vset_t dst,vset_t src){
    BDD tmp = dst->bdd;
    dst->bdd=bdd_addref(bdd_exist(src->bdd,dst->c_set));
    bdd_delref(tmp);
}

static void set_zip_fdd(vset_t dst,vset_t src){
	BDD tmp1=dst->bdd;
	BDD tmp2=src->bdd;
	dst->bdd=bdd_addref(bdd_or(tmp1,tmp2));
	src->bdd=bdd_addref(bdd_apply(tmp2,tmp1,bddop_diff));
	bdd_delref(tmp1);
	bdd_delref(tmp2);
}

static void rel_add_fdd(vrel_t rel,const int* src,const int *dst){
	BDD bdd=fdd_pair(rel,src,dst);
	BDD tmp=rel->bdd;
	rel->bdd=bdd_addref(bdd_or(rel->bdd,bdd));
	bdd_delref(bdd);
	bdd_delref(tmp);
}

static void vset_fdd_reorder() {
  static int lastnum = 1000;
  if (fdd_order_strat!=BDD_REORDER_NONE) {
    bdd_gbc();
    if (bdd_getnodenum()>2*lastnum) {
      lastnum = bdd_getnodenum();
      Warning(info,"Starting dynamic reordering: %d",bdd_getnodenum());
      bdd_reorder(fdd_order_strat);
      Warning(info,"Finished dynamic reordering: %d",bdd_getnodenum());
    }
  }
}

vdom_t vdom_create_fdd(int n){
	Warning(info,"Creating a BuDDy fdd domain.");
	buddy_init();
	vdom_t dom=(vdom_t)RTmalloc(sizeof(struct vector_domain));
	vdom_init_shared(dom,n);
	int res;
	int domain[2];
	domain[0]=1<<fdd_bits;
	domain[1]=1<<fdd_bits;
	dom->vars=(int*)RTmalloc(n*sizeof(int));
	dom->vars2=(int*)RTmalloc(n*sizeof(int));
	dom->proj=(int*)RTmalloc(n*sizeof(int));
	for(int i=0;i<n;i++){
		res=fdd_extdomain(domain,2);
		if (res<0){
			Abort("BuDDy error: %s",bdd_errstring(res));
		}
		dom->vars[i]=res;
		dom->vars2[i]=res+1;
		dom->proj[i]=i;
		fdd_intaddvarblock(res,res+1,BDD_REORDER_FREE); // requires patch in buddy/src/fdd.c
	}
	dom->varset=bdd_addref(fdd_makeset(dom->vars,n));
	if (dom->varset==bddfalse) {
		Abort("fdd_makeset failed");
	}
	dom->shared.set_create=set_create_fdd;
	dom->shared.set_add=set_add_fdd;
	dom->shared.set_member=set_member_fdd;
	dom->shared.set_is_empty=set_is_empty_all;
	dom->shared.set_equal=set_equal_all;
	dom->shared.set_clear=set_clear_all;
	dom->shared.set_copy=set_copy_all;
	dom->shared.set_enum=set_enum_fdd;
	dom->shared.set_enum_match=set_enum_match_fdd;
	dom->shared.set_copy_match=set_copy_match_fdd;
        dom->shared.set_example=set_example_fdd;
	dom->shared.set_count=set_count_fdd;
	dom->shared.set_union=set_union_fdd;
    dom->shared.set_intersect=set_intersect_fdd;
	dom->shared.set_minus=set_minus_fdd;
	dom->shared.set_zip=set_zip_fdd;
	dom->shared.set_project=set_project_fdd;
	dom->shared.rel_create=rel_create_fdd;
	dom->shared.rel_add=rel_add_fdd;
	dom->shared.rel_count=rel_count_fdd;
	dom->shared.set_next=set_next_appex_fdd;
	dom->shared.set_prev=set_prev_appex_fdd;
	dom->shared.reorder=vset_fdd_reorder;
    dom->shared.set_destroy=set_destroy_fdd;
	return dom;
}

