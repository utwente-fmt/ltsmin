#include <stdlib.h>
#include <math.h>
#include <fdd.h>

#include <runtime.h>
#include <vdom_object.h>

static int fdd_bits=16;
static int cacheratio=64;
static int maxincrease=1000000;
static int minfreenodes=20;

static void buddy_init(){
	static int initialized=0;
	if (!initialized) {
		bdd_init(1000000, 100000);
		Warning(info,"ratio %d, maxixum increase %d, minimum free %d",cacheratio,maxincrease,minfreenodes);
		bdd_setcacheratio(cacheratio);
		bdd_setmaxincrease(maxincrease);
		bdd_setminfreenodes(minfreenodes);
		initialized=1;
	}
}

struct poptOption buddy_options[]= {
	{ "cache-ratio",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cacheratio , 0 , "set cache ratio","<nodes/slot>"},
	{ "max-increase" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxincrease , 0 , "set maximum increase","<number>"},
	{ "min-free-nodes", 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &minfreenodes , 0 , "set minimum free node percentage","<percentage>"},
	{ "fdd-bits" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &fdd_bits , 0 , "set the number of bits for each fdd variable","<number>"},
	POPT_TABLEEND
};

struct vector_domain {
	struct vector_domain_shared shared;
	//BDD **vals;
	int *vars;
	BDD varset;
	int *vars2;
	bddPair *pairs;
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
	BDD bdd;
	BDD p_set; // variables in the projection.
	int p_len;
	int* proj;
};

static vset_t set_create_fdd(vdom_t dom,int k,int* proj){
	vset_t set=(vset_t)RTmalloc(sizeof(struct vector_set));
	set->dom=dom;
	set->bdd=bddfalse;
	if (k && k<dom->shared.size) {
		int vars[k];
		int complement[dom->shared.size-k];
		set->p_len=k;
		set->proj=(int*)RTmalloc(k*sizeof(int));
		int i=0;
		int j=0;
		for(int v=0;v<dom->shared.size;v++){
			if (v==proj[i] && i<k) {
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

static vrel_t rel_create_fdd(vdom_t dom,int k,int* proj){
	vrel_t rel=(vrel_t)RTmalloc(sizeof(struct vector_relation));
	rel->dom=dom;
	rel->bdd=bddfalse;
	if (k && k<dom->shared.size) {
		int vars[k];
		rel->p_len=k;
		rel->proj=(int*)RTmalloc(k*sizeof(int));
		for(int i=0;i<k;i++) {
			rel->proj[i]=proj[i];
			vars[i]=dom->vars[proj[i]];
		}
		rel->p_set=bdd_addref(fdd_makeset(vars,k));
	} else {
		rel->p_len=dom->shared.size;
		rel->p_set=dom->varset;
		rel->proj=dom->proj;
	}
	return rel;
}

static inline BDD fdd_element(vset_t set,const int* e){
	int N=set->p_len;
	BDD bdd=bddtrue;
	for(int i=0;i<N;i++){
	//for(int i=N-1;i>=0;i--){
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
	for(int i=0;i<N;i++){
	//for(int i=N-1;i>=0;i--){
		BDD val=mkvar(rel->dom,rel->proj[i],e1[i]);
		BDD tmp=bdd;
		bdd=bdd_addref(bdd_and(bdd,val));
		bdd_delref(tmp);
		rmvar(val);
		val=mkvar2(rel->dom,rel->proj[i],e2[i]);
		tmp=bdd;
		bdd=bdd_addref(bdd_and(bdd,val));
		bdd_delref(tmp);
		rmvar2(val);
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

static void vset_enum_do_fdd(vdom_t dom,BDD set,int* proj,int *vec,int N,int i,vset_element_cb cb,void* context){
	if (i==N) {
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
			vset_enum_do_fdd(dom,subset,proj,vec,N,i+1,cb,context);
		}
	}
	bdd_delref(set);
}

static void set_enum_fdd(vset_t set,vset_element_cb cb,void* context){
	int N=set->p_len;
	int vec[N];
	bdd_addref(set->bdd);
	vset_enum_do_fdd(set->dom,set->bdd,set->proj,vec,N,0,cb,context);
}

static void set_enum_match_fdd(vset_t set,int p_len,int* proj,int*match,vset_element_cb cb,void* context){
	BDD subset=set->bdd;
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
	vset_enum_do_fdd(set->dom,subset,set->proj,vec,N,0,cb,context);
}

static void set_count_fdd(vset_t set,long *nodes,long long *elements){
	*nodes=bdd_nodecount(set->bdd);
	double count=bdd_satcountlnset(set->bdd,set->p_set);
	//Warning(info,"log of satcount is %f",count);
	count=pow(2.0,count);
	//Warning(info,"satcount is %f",count);
	*elements=llround(count);
}

static void set_union_fdd(vset_t dst,vset_t src){
	BDD tmp=dst->bdd;
	dst->bdd=bdd_addref(bdd_apply(tmp,src->bdd,bddop_or));
	bdd_delref(tmp);
}

static void set_minus_fdd(vset_t dst,vset_t src){
	BDD tmp=dst->bdd;
	dst->bdd=bdd_addref(bdd_apply(tmp,src->bdd,bddop_diff));
	bdd_delref(tmp);
}

static void set_next_fdd(vset_t dst,vset_t src,vrel_t rel){
	BDD tmp=bdd_addref(bdd_and(src->bdd,rel->bdd));
	bdd_delref(dst->bdd);
	BDD tmp2=bdd_addref(bdd_exist(tmp,rel->p_set));
	bdd_delref(tmp);
	dst->bdd=bdd_addref(bdd_replace(tmp2,rel->dom->pairs));
	bdd_delref(tmp2);
}

static void set_project_fdd(vset_t dst,vset_t src){
	bdd_delref(dst->bdd);
	dst->bdd=bdd_addref(bdd_exist(src->bdd,dst->c_set));
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
			Fatal(1,error,"BuDDy error: %s",bdd_errstring(res));
		}
		dom->vars[i]=res;
		dom->vars2[i]=res+1;
		dom->proj[i]=i;
	}
	dom->varset=bdd_addref(fdd_makeset(dom->vars,n));
	if (dom->varset==bddfalse) {
		Fatal(1,error,"fdd_makeset failed");
	}
	dom->pairs=bdd_newpair();
	res=fdd_setpairs(dom->pairs,dom->vars2,dom->vars,n);
	if (res<0){
		Fatal(1,error,"BuDDy error: %s",bdd_errstring(res));
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
	dom->shared.set_count=set_count_fdd;
	dom->shared.set_union=set_union_fdd;
	dom->shared.set_minus=set_minus_fdd;
	dom->shared.set_zip=set_zip_fdd;
	dom->shared.set_project=set_project_fdd;
	dom->shared.rel_create=rel_create_fdd;
	dom->shared.rel_add=rel_add_fdd;
	dom->shared.set_next=set_next_fdd;
	return dom;
}

