#include <hre/config.h>
#include <stdlib.h>

#include <hre/user.h>
#include <vset-lib/vdom_object.h>

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

extern struct poptOption listdd64_options[];
extern vdom_t vdom_create_list64_native(int n);

#ifdef HAVE_SYLVAN
extern struct poptOption sylvan_options[];
extern vdom_t vdom_create_sylvan(int n);
extern vdom_t vdom_create_sylvan_from_file(FILE *f);

extern struct poptOption lddmc_options[];
extern vdom_t vdom_create_lddmc(int n);
extern vdom_t vdom_create_lddmc_from_file(FILE *f);
#endif

vset_implementation_t vset_default_domain = VSET_IMPL_AUTOSELECT;

static void vset_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
	case POPT_CALLBACK_REASON_POST:
		Abort("unexpected call to vset_popt");
	case POPT_CALLBACK_REASON_OPTION:
		if (!strcmp(opt->longName,"vset")){
			int res=linear_search((si_map_entry*)data,arg);
			if (res<0) {
				Warning(error,"unknown vector set implementation %s",arg);
				HREexitUsage(HRE_EXIT_FAILURE);
			}
			vset_default_domain=res;
			return;
		}
		Abort("unexpected call to vset_popt");
	}
}


static si_map_entry vset_table[]={
	{"ldd",VSET_ListDD},
	{"ldd64",VSET_ListDD64},
#ifdef HAVE_ATERM2_H
	{"list",VSET_AtermDD_list},
	{"tree",VSET_AtermDD_tree},
#endif
	{"fdd",VSET_BuDDy_fdd},
#ifdef HAVE_DDD_H
	{"ddd",VSET_DDD},
#endif
#ifdef HAVE_SYLVAN
    {"sylvan",VSET_Sylvan},
    {"lddmc",VSET_LDDmc},
#endif // HAVE_SYLVAN
	{NULL,0}
};


struct poptOption vset_options[]={
    { NULL, 0 , POPT_ARG_CALLBACK , (void*)vset_popt , 0 , (void*)vset_table ,NULL },
    { "vset" , 0 , POPT_ARG_STRING , NULL , 0 ,
      "select a vector set implementation from native ListDD (32-bit or 64-bit),"
      " ATermDD with *list* encoding,"
      " ATermDD with *tree* encoding, BuDDy using the *fdd* feature,"
      " DDD, Sylvan, or multicore ListDD (default: first available)" , "<ldd64|ldd|list|tree|fdd|ddd|sylvan|lddmc>" },
    { NULL,0 , POPT_ARG_INCLUDE_TABLE , listdd_options , 0 , "ListDD options" , NULL},
    { NULL,0 , POPT_ARG_INCLUDE_TABLE , listdd64_options , 0 , "ListDD64 options" , NULL},
#ifdef HAVE_ATERM2_H
    { NULL,0 , POPT_ARG_INCLUDE_TABLE , atermdd_options , 0 , "ATermDD options" , NULL},
#endif
    { NULL,0 , POPT_ARG_INCLUDE_TABLE , buddy_options , 0 , "BuDDy options" , NULL},
#ifdef HAVE_SYLVAN
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , sylvan_options , 0 , "Sylvan options" , NULL},
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , lddmc_options , 0 , "LDDmc options" , NULL},
#endif // HAVE_SYLVAN
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
    case VSET_ListDD64: return vdom_create_list64_native(n);
    case VSET_ListDD: return vdom_create_list_native(n);
#ifdef HAVE_ATERM2_H
    case VSET_AtermDD_list: return vdom_create_list(n);
    case VSET_AtermDD_tree: return vdom_create_tree(n);
#endif
    case VSET_BuDDy_fdd: return vdom_create_fdd(n);
#ifdef HAVE_DDD_H
    case VSET_DDD: return vdom_create_ddd(n);
#endif
#ifdef HAVE_SYLVAN
    case VSET_Sylvan: return vdom_create_sylvan(n);
    case VSET_LDDmc: return vdom_create_lddmc(n);
#endif // HAVE_SYLVAN
    default: return NULL;
    }
}

vdom_t
vdom_create_domain_from_file(FILE *f, vset_implementation_t impl)
{
    if (impl == VSET_IMPL_AUTOSELECT)
        impl = vset_default_domain;
    switch(impl) {
#ifdef HAVE_SYLVAN
    case VSET_Sylvan: return vdom_create_sylvan_from_file(f);
    case VSET_LDDmc: return vdom_create_lddmc_from_file(f);
#endif // HAVE_SYLVAN
    default: return NULL;
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
    expand_cb expand;
    void *expand_ctx;
};

static void
default_zip(vset_t dst, vset_t src)
{
    dst->dom->shared.set_minus(src, dst);
    dst->dom->shared.set_union(dst, src);
}

static void
default_set_project_minus(vset_t dst, vset_t src, vset_t minus)
{
    dst->dom->shared.set_project(dst, src);
    dst->dom->shared.set_minus(dst, minus);
}

static void
default_reorder()
{
    Warning(info,"reorder request ignored");
}

static void 
default_set_example_match(vset_t set, int *e, int p_len, int* proj, int* match) {
  vdom_t domain = set->dom;
  vset_t dst = domain->shared.set_create(domain, -1, NULL);
  domain->shared.set_copy_match(dst,set,p_len,proj,match);
  domain->shared.set_example(dst,e);
  vset_destroy(dst);
}

static void
default_least_fixpoint(vset_t dst, vset_t src, vrel_t rels[], int rel_count)
{
    (void)dst;  (void)src; (void)rels; (void)rel_count;

    Abort("Decision diagram package does not support least fixpoint");
}

struct default_set_update_context
{
    vset_t set; // set to add to
    vset_update_cb cb; // callback to call
    void *context; // context for the callback
};

static void
default_set_update_cb(void *context, int *src)
{
    struct default_set_update_context *ctx = (struct default_set_update_context*)context;
    ctx->cb(ctx->set, ctx->context, src);
}

static void
default_set_update(vset_t dst, vset_t set, vset_update_cb cb, void *context)
{
    struct default_set_update_context ctx;
    ctx.set = dst;
    ctx.cb = cb;
    ctx.context = context;
    vset_enum(set, default_set_update_cb, &ctx);
}

struct default_rel_update_context
{
    vrel_t rel;
    vrel_update_cb cb;
    void *context;
};

static void
default_rel_update_cb(void *context, int *src)
{
    struct default_rel_update_context *ctx = (struct default_rel_update_context*)context;
    ctx->cb(ctx->rel, ctx->context, src);
}

static void
default_rel_update(vrel_t rel, vset_t set, vrel_update_cb cb, void *context)
{
    struct default_rel_update_context ctx;
    ctx.rel = rel;
    ctx.cb = cb;
    ctx.context = context;
    vset_enum(set, default_rel_update_cb, &ctx);
}

void vdom_init_shared(vdom_t dom,int n)
{
    memset(&dom->shared, 0, sizeof(dom->shared));

	dom->shared.size=n;
	dom->shared.set_example_match=default_set_example_match;
	dom->shared.set_zip=default_zip;
    dom->shared.rel_update=default_rel_update;
    dom->shared.set_update=default_set_update;
	dom->shared.reorder=default_reorder;
	dom->shared.set_least_fixpoint=default_least_fixpoint;
    dom->shared.set_project_minus=default_set_project_minus;
    dom->shared.names = RTmalloc(n * sizeof(char*));
    for (int i = 0; i < n; i++) dom->shared.names[i] = NULL;
}

void vdom_set_name(vdom_t dom, int i, char* name) {
    if (i >= dom->shared.size) Abort("Variable does not exist");
    dom->shared.names[i] = name;
}

char* vdom_get_name(vdom_t dom, int i) {
    if (i >= dom->shared.size) { Abort("Variable %d does not exist", i); }
    return dom->shared.names[i];
}

int vdom_separates_rw(vdom_t dom) {
    if (dom->shared.separates_rw == NULL) return 0;
    return dom->shared.separates_rw();
}

int vdom_supports_cpy(vdom_t dom) {
    if (dom->shared.supports_cpy == NULL) return 0;
    return dom->shared.supports_cpy();
}

vset_t vset_create(vdom_t dom,int k,int* proj){
	return dom->shared.set_create(dom,k,proj);
}

void vset_save(FILE* f, vset_t set){
    if (set->dom->shared.set_save==NULL){
        Abort("Saving of sets not supported by the current BDD implementation.")
    } else {
        set->dom->shared.set_save(f,set);
    }
}

vset_t vset_load(FILE* f, vdom_t dom){
    if (dom->shared.set_load==NULL){
        Abort("Loading of sets not supported by the current BDD implementation.")
    } else {
        return dom->shared.set_load(f,dom);
    }
}

vrel_t vrel_create(vdom_t dom,int k,int* proj){
    vrel_t rel = dom->shared.rel_create(dom, k, proj);
    rel->expand = NULL;
    rel->expand_ctx = NULL;
    return rel;
}

vrel_t vrel_create_rw(vdom_t dom,int r_k,int* r_proj,int w_k,int* w_proj){
    vrel_t rel = dom->shared.rel_create_rw(dom, r_k, r_proj, w_k, w_proj);
    rel->expand = NULL;
    rel->expand_ctx = NULL;
    return rel;
}

void vrel_save_proj(FILE* f, vrel_t rel){
    if (rel->dom->shared.rel_save_proj==NULL){
        Abort("Saving of relations not supported by the current BDD implementation.")
    } else {
        rel->dom->shared.rel_save_proj(f,rel);
    }
}

void vrel_save(FILE* f, vrel_t rel){
    if (rel->dom->shared.rel_save==NULL){
        Abort("Saving of relations not supported by the current BDD implementation.")
    } else {
        rel->dom->shared.rel_save(f,rel);
    }
}

vrel_t vrel_load_proj(FILE* f, vdom_t dom){
    if (dom->shared.rel_load_proj==NULL){
        Abort("Loading of relations not supported by the current BDD implementation.")
    } else {
        return dom->shared.rel_load_proj(f,dom);
    }
}

void vrel_load(FILE* f, vrel_t rel){
    if (rel->dom->shared.rel_load==NULL){
        Abort("Loading of relations not supported by the current BDD implementation.")
    } else {
        rel->dom->shared.rel_load(f,rel);
    }
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

void vset_copy_match_proj(vset_t dst,vset_t src,int p_len,int* proj,int p_id,int*match){
    if (dst->dom->shared.set_copy_match_proj==NULL){
        dst->dom->shared.set_copy_match(dst,src,p_len,proj,match);
    } else {
        dst->dom->shared.set_copy_match_proj(dst,src,p_len,proj,p_id,match);
    }
}

int vproj_create(vdom_t dom, int p_len, int* proj){
    if (dom->shared.proj_create==NULL){
        return -1;
    } else {
        return dom->shared.proj_create(p_len, proj);
    }
}

void vset_example(vset_t set,int *e){
       set->dom->shared.set_example(set,e);
}

void vset_random(vset_t set,int *e){
    if (set->dom->shared.set_random==NULL) {
        Warning(hre_debug, "Generating random elements not supported.");
        set->dom->shared.set_example(set, e);
    } else {
       set->dom->shared.set_random(set, e);
    }
}

void vset_example_match(vset_t set,int *e, int p_len, int* proj, int* match){
        set->dom->shared.set_example_match(set,e,p_len,proj,match);
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

void vset_prev(vset_t dst,vset_t src,vrel_t rel,vset_t univ){
	dst->dom->shared.set_prev(dst,src,rel,univ);
}

void vset_universe(vset_t dst,vset_t src){
    dst->dom->shared.set_universe(dst,src);
}

void vset_project(vset_t dst,vset_t src){
	dst->dom->shared.set_project(dst,src);
}

void vset_project_minus(vset_t dst,vset_t src,vset_t minus){
    dst->dom->shared.set_project_minus(dst,src,minus);
}

void vrel_add(vrel_t rel,const int* src, const int* dst){
	rel->dom->shared.rel_add(rel,src,dst);
}

void vrel_add_cpy(vrel_t rel,const int* src, const int* dst, const int* cpy){
    rel->dom->shared.rel_add_cpy(rel,src,dst,cpy);
}

static void
default_rel_add_act(vrel_t rel, const int* src, const int* dst, const int* cpy, const int act)
{
    rel->dom->shared.rel_add(rel,src,dst);
    (void)cpy;
    (void)act;
}

static void
default_rel_add_act_cpy(vrel_t rel, const int* src, const int* dst, const int* cpy, const int act)
{
    rel->dom->shared.rel_add_cpy(rel,src,dst,cpy);
    (void)act;
}

void
vrel_add_act(vrel_t rel,const int* src, const int* dst, const int* cpy, const int act)
{
    if (rel->dom->shared.rel_add_act == NULL) {
        if (vdom_supports_cpy(rel->dom)) {
            Warning(info, "vrel_add_act not supported; falling back to vrel_add_cpy");
            rel->dom->shared.rel_add_act = default_rel_add_act_cpy;
        } else {
            Warning(info, "vrel_add_act not supported; falling back to vrel_add");
            rel->dom->shared.rel_add_act = default_rel_add_act;
        }
    }

    rel->dom->shared.rel_add_act(rel,src,dst,cpy,act);
}

void vrel_update(vrel_t rel, vset_t set, vrel_update_cb cb, void *context) {
    rel->dom->shared.rel_update(rel, set, cb, context);
}

void vset_update(vset_t dst, vset_t src, vset_update_cb cb, void *context) {
    dst->dom->shared.set_update(dst, src, cb, context);
}

void vset_reorder(vdom_t dom) {
  dom->shared.reorder();
}

void vset_destroy(vset_t set) {
    set->dom->shared.set_destroy(set);
}

void vrel_set_expand(vrel_t rel, expand_cb cb, void *context) {
    rel->expand = cb;
    rel->expand_ctx = context;
}

void vset_least_fixpoint(vset_t dst, vset_t src, vrel_t rels[], int rel_count) {
    src->dom->shared.set_least_fixpoint(dst, src, rels, rel_count);
}

void vset_dot(FILE* fp, vset_t src) {
    if (src->dom->shared.set_dot==NULL){
        Abort("Exporting sets to dot not supported by the current BDD implementation.")
    } else {
        src->dom->shared.set_dot(fp,src);
    }
}

void vrel_dot(FILE* fp, vrel_t src) {
    if (src->dom->shared.rel_dot==NULL){
        Abort("Exporting relations to dot not supported by the current BDD implementation.")
    } else {
        src->dom->shared.rel_dot(fp,src);
    }
}

void vset_join(vset_t dst, vset_t left, vset_t right) {
    if (dst->dom->shared.set_join==NULL){
        Abort("Vector set implementation does not support vset_join operation.");
    } else {
        dst->dom->shared.set_join(dst,left,right);
    }
}

void
vset_pre_save(FILE *f, vdom_t dom)
{
    if (dom->shared.pre_save != NULL) dom->shared.pre_save(f, dom);
}

void
vset_post_save(FILE *f, vdom_t dom)
{
    if (dom->shared.post_save != NULL) dom->shared.post_save(f, dom);
}

void
vset_pre_load(FILE *f, vdom_t dom)
{
    if (dom->shared.pre_load != NULL) dom->shared.pre_load(f, dom);
}

void
vset_post_load(FILE *f, vdom_t dom)
{
    if (dom->shared.post_load != NULL) dom->shared.post_load(f, dom);
}

void
vdom_save(FILE *f, vdom_t dom)
{
    if (dom->shared.dom_save == NULL) {
        //Abort("Saving of domains not supported by the current VSet implementation.")
    } else {
        dom->shared.dom_save(f, dom);
    }
}

int
vdom_vector_size(vdom_t dom)
{
    return dom->shared.size;
}
