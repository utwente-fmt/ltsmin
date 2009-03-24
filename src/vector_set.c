#include <stdlib.h>

#include <vdom_object.h>
#include <runtime.h>


extern struct poptOption atermdd_options[];
extern struct poptOption buddy_options[];

typedef enum { AtermDD_list=1 , AtermDD_tree=2, BuDDy_fdd=3 } vset_implementation_t;

static vset_implementation_t vset_default_domain=AtermDD_list;

static void vset_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
	case POPT_CALLBACK_REASON_POST:
		Fatal(1,error,"unexpected call to vset_popt");
	case POPT_CALLBACK_REASON_OPTION:
		if (!strcmp(opt->longName,"vset")){
			int res=linear_search((si_map_entry*)data,arg);
			if (res<0) {
				Warning(error,"unknown vector set implementation %s",arg);
				RTexitUsage(EXIT_FAILURE);
			}
			vset_default_domain=res;
			return;
		}
		Fatal(1,error,"unexpected call to vset_popt");
	}
}


static si_map_entry setonly_table[]={
	{"list",AtermDD_list},
	{"tree",AtermDD_tree},
	{"fdd",BuDDy_fdd},
	{NULL,0}
};

static si_map_entry set_and_rel_table[]={
	{"list",AtermDD_list},
	{"fdd",BuDDy_fdd},
	{NULL,0}
};


struct poptOption vset_setonly_options[]={
	{ NULL, 0 , POPT_ARG_CALLBACK , (void*)vset_popt , 0 , (void*)setonly_table ,NULL },
	{ "vset" , 0 , POPT_ARG_STRING , NULL , 0 , "select a vector set implementation from ATermDD with *list* encoding,"
		" ATermDD with *tree* encoding, or BuDDy using the *fdd* feature  (default: list)" , "<list|tree|fdd>" },
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , atermdd_options , 0 , "ATermDD options" , NULL},
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , buddy_options , 0 , "BuDDy options" , NULL},
	POPT_TABLEEND
};

struct poptOption vset_full_options[]={
	{ NULL, 0 , POPT_ARG_CALLBACK , (void*)vset_popt , 0 ,  (void*)set_and_rel_table , NULL},
	{ "vset" , 0 , POPT_ARG_STRING , NULL , 0 , "select a vector set implementation from ATermDD with list encoding,"
		" or BuDDy using the fdd feature  (default ATermDD list)" , "<list|fdd>" },
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , atermdd_options , 0 , "ATermDD options", NULL},
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , buddy_options , 0 , "BuDDy options", NULL},
	POPT_TABLEEND
};

vdom_t vdom_create_default(int n){
	switch(vset_default_domain){
	case AtermDD_list: return vdom_create_list(n);
	case AtermDD_tree: return vdom_create_tree(n);
	case BuDDy_fdd: return vdom_create_fdd(n);
	}
	return NULL;
}

struct vector_domain {
	struct vector_domain_shared shared;
};

struct vector_set {
	vdom_t dom;
};

struct vector_relation {
	vdom_t dom;
};

void vdom_init_shared(vdom_t dom,int n){
	dom->shared.size=n;
	dom->shared.set_create=NULL;
	dom->shared.set_add=NULL;
	dom->shared.set_member=NULL;
	dom->shared.set_is_empty=NULL;
	dom->shared.set_equal=NULL;
	dom->shared.set_clear=NULL;
	dom->shared.set_copy=NULL;
	dom->shared.set_enum=NULL;
	dom->shared.set_enum_match=NULL;
	dom->shared.set_count=NULL;
	dom->shared.set_union=NULL;
	dom->shared.set_minus=NULL;
	dom->shared.set_zip=NULL;
	dom->shared.set_project=NULL;
	dom->shared.rel_create=NULL;
	dom->shared.rel_add=NULL;
	dom->shared.set_next=NULL;
}

vset_t vset_create(vdom_t dom,int k,int* proj){
	return dom->shared.set_create(dom,k,proj);
}

vrel_t vrel_create(vdom_t dom,int k,int* proj){
	return dom->shared.rel_create(dom,k,proj);
}

void vset_add(vset_t set,const int* e){
	set->dom->shared.set_add(set,e);
}

int vset_member(vset_t set,const int* e){
	return set->dom->shared.set_member(set,e);
}

int vset_is_empty(vset_t set){
	return set->dom->shared.set_is_empty(set);
}

int vset_equal(vset_t set1,vset_t set2){
	return set1->dom->shared.set_equal(set1,set2);
}

void vset_clear(vset_t set){
	set->dom->shared.set_clear(set);
}

void vset_copy(vset_t dst,vset_t src){
	dst->dom->shared.set_copy(dst,src);
}

void vset_enum(vset_t set,vset_element_cb cb,void* context){
	set->dom->shared.set_enum(set,cb,context);
}

void vset_enum_match(vset_t set,int p_len,int* proj,int*match,vset_element_cb cb,void* context){
	set->dom->shared.set_enum_match(set,p_len,proj,match,cb,context);
}

void vset_count(vset_t set,long *nodes,long long *elements){
	set->dom->shared.set_count(set,nodes,elements);
}

void vset_union(vset_t dst,vset_t src){
	dst->dom->shared.set_union(dst,src);
}

void vset_minus(vset_t dst,vset_t src){
	dst->dom->shared.set_minus(dst,src);
}

void vset_zip(vset_t dst,vset_t src){
	dst->dom->shared.set_zip(dst,src);
}

void vset_next(vset_t dst,vset_t src,vrel_t rel){
	dst->dom->shared.set_next(dst,src,rel);
}

void vset_project(vset_t dst,vset_t src){
	dst->dom->shared.set_project(dst,src);
}

void vrel_add(vrel_t rel,const int* src, const int* dst){
	rel->dom->shared.rel_add(rel,src,dst);
}

