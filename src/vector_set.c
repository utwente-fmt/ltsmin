#include <config.h>
#include <stdlib.h>

#include <vdom_object.h>
#include <runtime.h>

#ifdef HAVE_ATERM2_H
extern struct poptOption atermdd_options[];
extern vdom_t vdom_create_list(int n);
extern vdom_t vdom_create_tree(int n);
#endif

extern struct poptOption buddy_options[];
extern vdom_t vdom_create_fdd(int n);

#ifdef HAVE_DDD_H
extern vdom_t vdom_create_ddd(int n);
#endif

extern struct poptOption listdd_options[];
extern vdom_t vdom_create_list_native(int n);

vset_implementation_t vset_default_domain = VSET_IMPL_AUTOSELECT;

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


static si_map_entry vset_table[]={
#ifdef HAVE_ATERM2_H
	{"list",VSET_AtermDD_list},
	{"tree",VSET_AtermDD_tree},
#endif
	{"fdd",VSET_BuDDy_fdd},
#ifdef HAVE_DDD_H
	{"ddd",VSET_DDD},
#endif
	{"ldd",VSET_ListDD},
	{NULL,0}
};


struct poptOption vset_options[]={
	{ NULL, 0 , POPT_ARG_CALLBACK , (void*)vset_popt , 0 , (void*)vset_table ,NULL },
	{ "vset" , 0 , POPT_ARG_STRING , NULL , 0 ,
		"select a vector set implementation from ATermDD with *list* encoding,"
		" ATermDD with *tree* encoding, BuDDy using the *fdd* feature, or"
		" native ListDD, or DDD (default: first available)" , "<list|tree|fdd|ddd|ldd>" },
#ifdef HAVE_ATERM2_H
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , atermdd_options , 0 , "ATermDD options" , NULL},
#endif
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , buddy_options , 0 , "BuDDy options" , NULL},
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , listdd_options , 0 , "ListDD options" , NULL},
	POPT_TABLEEND
};

vdom_t
vdom_create_domain(int n, vset_implementation_t impl)
{
    if (impl == VSET_IMPL_AUTOSELECT)
        impl = vset_default_domain;
    switch(impl){
    case VSET_IMPL_AUTOSELECT:
        /* fall-through */
#ifdef HAVE_ATERM2_H
    case VSET_AtermDD_list: return vdom_create_list(n);
    case VSET_AtermDD_tree: return vdom_create_tree(n);
#endif
    case VSET_BuDDy_fdd: return vdom_create_fdd(n);
#ifdef HAVE_DDD_H
    case VSET_DDD: return vdom_create_ddd(n);
#endif
    case VSET_ListDD: return vdom_create_list_native(n);
        default:
            return NULL;
    }
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

static void
default_zip(vset_t dst, vset_t src)
{
    dst->dom->shared.set_minus(src, dst);
    dst->dom->shared.set_union(dst, src);
}

static void
default_reorder()
{
    Warning(info,"reorder request ignored");
}

static void
default_least_fixpoint(vset_t dst, vset_t src, vrel_t rels[], int rel_count)
{
    vset_t temp = dst->dom->shared.set_create(dst->dom, 0, NULL);
    vset_t fix  = dst->dom->shared.set_create(dst->dom, 0, NULL);
    vset_t old  = dst->dom->shared.set_create(dst->dom, 0, NULL);

    dst->dom->shared.set_copy(fix, src);

    while (!dst->dom->shared.set_equal(fix, old)) {
        dst->dom->shared.set_copy(old, fix);
        for (int i = 0; i < rel_count; i++) {
            dst->dom->shared.set_next(temp, fix, rels[i]);
            dst->dom->shared.set_union(fix, temp);
        }
    }

    dst->dom->shared.set_copy(dst, fix);
    dst->dom->shared.set_destroy(temp);
    dst->dom->shared.set_destroy(fix);
    dst->dom->shared.set_destroy(old);
}

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
	dom->shared.set_copy_match=NULL;
	dom->shared.set_count=NULL;
	dom->shared.set_union=NULL;
	dom->shared.set_intersect=NULL;
	dom->shared.set_minus=NULL;
	dom->shared.set_zip=default_zip;
	dom->shared.set_project=NULL;
	dom->shared.rel_create=NULL;
	dom->shared.rel_add=NULL;
	dom->shared.rel_count=NULL;
	dom->shared.set_next=NULL;
	dom->shared.set_prev=NULL;
	dom->shared.reorder=default_reorder;
	dom->shared.set_destroy=NULL;
	dom->shared.set_least_fixpoint=default_least_fixpoint;
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

void vset_copy_match(vset_t dst,vset_t src,int p_len,int* proj,int*match){
	dst->dom->shared.set_copy_match(dst,src,p_len,proj,match);
}

void vset_example(vset_t set,int *e){
	set->dom->shared.set_example(set,e);
}

void vset_count(vset_t set,long *nodes,bn_int_t *elements){
	set->dom->shared.set_count(set,nodes,elements);
}

void vrel_count(vrel_t rel,long *nodes,bn_int_t *elements){
	rel->dom->shared.rel_count(rel,nodes,elements);
}

void vset_union(vset_t dst,vset_t src){
	dst->dom->shared.set_union(dst,src);
}

void vset_intersect(vset_t dst, vset_t src) {
    dst->dom->shared.set_intersect(dst,src);
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

void vset_prev(vset_t dst,vset_t src,vrel_t rel){
	dst->dom->shared.set_prev(dst,src,rel);
}

void vset_project(vset_t dst,vset_t src){
	dst->dom->shared.set_project(dst,src);
}

void vrel_add(vrel_t rel,const int* src, const int* dst){
	rel->dom->shared.rel_add(rel,src,dst);
}

void vset_reorder(vdom_t dom) {
  dom->shared.reorder();
}

void vset_destroy(vset_t set) {
    set->dom->shared.set_destroy(set);
}

void vset_least_fixpoint(vset_t dst, vset_t src, vrel_t rels[], int rel_count) {
    src->dom->shared.set_least_fixpoint(dst, src, rels, rel_count);
}
