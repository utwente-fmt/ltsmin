#include <hre/config.h>

#include <hre/user.h>
#include <vset-lib/vector_set.h>

#ifdef HAVE_SYLVAN
#include <sylvan.h>
#else
#define LACE_ME
#define lace_suspend()
#define lace_resume()
#endif

struct args_t
{
    int argc;
    char **argv;
};

static void
test_vset_popt(poptContext con, enum poptCallbackReason reason,
               const struct poptOption * opt, const char * arg, void * data)
{
    (void)con; (void)opt; (void)arg; (void)data;

    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        Abort("unexpected call to test_vset_popt");
    case POPT_CALLBACK_REASON_POST: {
        return;
    }
    case POPT_CALLBACK_REASON_OPTION:
        Abort("unexpected call to test_vset_popt");
    }
}

#ifdef HAVE_SYLVAN
static size_t lace_n_workers = 0;
static size_t lace_dqsize = 40960000; // can be very big, no problemo
static size_t lace_stacksize = 0; // use default

static struct poptOption lace_options[] = {
    { "lace-workers", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_n_workers , 0 , "set number of Lace workers (threads for parallelization)","<workers>"},
    { "lace-dqsize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_dqsize , 0 , "set length of Lace task queue","<dqsize>"},
    { "lace-stacksize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_stacksize, 0, "set size of program stack in kilo bytes (0=default stack size)", "<stacksize>"},
POPT_TABLEEND
};
#endif

static struct poptOption options[] = {
    { NULL, 0, POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION, (void*)test_vset_popt, 0, NULL, NULL},
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, vset_options, 0, "Vector set options", NULL},
    POPT_TABLEEND
};

static vdom_t domain;

static int dom_size = 2;
static int element_global[0];

static void
count_cb(void *ctx, int *e)
{
    (void)e;

    int *count = (int*)ctx;

    *count += 1;
}

void
create_project_test()
{
    vset_t set = vset_create(domain, -1, NULL);

    int count = 0;
    vset_enum(set, count_cb, &count);

    if (count == 0) {
        Warning(info, "Empty set count correct");
    } else {
        Abort("Empty set count incorrect");
    }

    int element[2] = {0, 0};
    vset_add(set, element);
    vset_enum(set, count_cb, &count);

    if (count == 1) {
        Warning(info, "Singleton count correct");
    } else {
        Abort("Singleton count incorrect");
    }

    int proj[1] = {1};
    int match_element[1] = {0};
    vset_t match = vset_create(domain, -1, NULL);

    vset_copy_match(match, set, 1, proj, match_element);

    count = 0;
    vset_enum(match, count_cb, &count);

    if (count == 1) {
        Warning(info, "Match count correct");
    } else {
        Abort("Match count incorrect at %d", count);
    }

    count = 0;
    vset_enum_match(set, 1, proj, match_element, count_cb, &count);

    if (count == 1) {
        Warning(info, "Enum match count correct");
    } else {
        Abort("Enum match count incorrect at %d", count);
    }

    match_element[0] = 1;
    vset_copy_match(match, set, 1, proj, match_element);

    count = 0;
    vset_enum(match, count_cb, &count);

    if (count == 0) {
        Warning(info, "Match count correct");
    } else {
        Abort("Match count incorrect at %d", count);
    }

    count = 0;
    vset_enum_match(set, 1, proj, match_element, count_cb, &count);

    if (count == 0) {
        Warning(info, "Enum match count correct");
    } else {
        Abort("Enum match count incorrect at %d", count);
    }
}

void
empty_projection_set_test()
{
    vset_t singleton = vset_create(domain, 0, NULL);
    vset_t copy      = vset_create(domain, 0, NULL);
    vset_t empty     = vset_create(domain, 0, NULL);
    int    element_local[0];

    vset_add(singleton, element_global);

    if (vset_member(singleton, element_local)) {
        Warning(info, "Epsilon element found");
    } else {
        Abort("Epsilon element not found");
    }

    vset_clear(singleton);

    if (!vset_member(singleton, element_local)) {
        Warning(info, "Epsilon element not found");
    } else {
        Abort("Epsilon element found");
    }

    vset_add(singleton, element_local);
    vset_add(singleton, element_global);
    vset_copy(copy, singleton);

    int count = 0;
    vset_enum(copy, count_cb, &count);

    if (count == 1) {
        Warning(info, "Epsilon copy count correct");
    } else {
        Abort("Epsilon copy count incorrect");
    }

    count = 0;
    vset_enum(empty, count_cb, &count);

    if (count == 0) {
        Warning(info, "Empty enum count correct");
    } else {
        Abort("Empty enum count incorrect");
    }

    count = 0;
    vset_example(singleton, element_local);
    Warning(info, "Example found");

    vset_clear(copy);

    vset_union(copy, empty);
    if (!vset_is_empty(copy)) Abort("Union empty, empty failed");
    vset_union(copy, singleton);
    if (vset_is_empty(copy)) Abort("Union empty, singleton failed");
    vset_union(copy, empty);
    if (vset_is_empty(copy)) Abort("Union singeton, empty failed");
    vset_union(copy, singleton);
    if (vset_is_empty(copy)) Abort("Union singleton, singleton failed");
    Warning(info, "Union correct");

    // copy == singleton at this point
    vset_intersect(copy, singleton);
    if (vset_is_empty(copy)) Abort("Intersect singleton, singleton failed");
    vset_intersect(copy, empty);
    if (!vset_is_empty(copy)) Abort("Intersect singleton, empty failed");
    vset_intersect(copy, empty);
    if (!vset_is_empty(copy)) Abort("Intersect empty, empty failed");
    vset_intersect(copy, singleton);
    if (!vset_is_empty(copy)) Abort("Intersect empty, singleton failed");
    Warning(info, "Intersect correct");

    vset_copy(copy, singleton);
    vset_minus(copy, empty);
    if (vset_is_empty(copy)) Abort("Minus singleton, empty failed");
    vset_minus(copy, singleton);
    if (!vset_is_empty(copy)) Abort("Minus singleton, singleton failed");
    vset_minus(copy, empty);
    if (!vset_is_empty(copy)) Abort("Minus empty, empty failed");
    vset_minus(copy, singleton);
    if (!vset_is_empty(copy)) Abort("Minus empty, singleton failed");
    Warning(info, "Minus correct");

    vset_zip(copy, empty);
    if (!vset_is_empty(copy))     Abort("Zip empty, empty failed (1)");
    if (!vset_is_empty(empty))    Abort("Zip empty, empty failed (2)");
    vset_zip(copy, singleton);
    if (vset_is_empty(copy))      Abort("Zip empty, singleton failed (1)");
    if (vset_is_empty(singleton)) Abort("Zip empty, singleton failed (2)");
    vset_zip(singleton, copy);
    if (!vset_is_empty(copy))     Abort("Zip singleton, singleton failed (1)");
    if (vset_is_empty(singleton)) Abort("Zip singleton, singleton failed (2)");
    vset_zip(singleton, copy);
    if (!vset_is_empty(copy))     Abort("Zip singleton, empty failed (1)");
    if (vset_is_empty(singleton)) Abort("Zip singleton, empty failed (2)");
    vset_clear(empty);
    Warning(info, "Zip correct");

    double elements;
    long nodes;

    vset_count(singleton, &nodes, &elements);
    Warning(info, "Singeton has %ld nodes and %f elements", nodes, elements);

    vset_count(empty, &nodes, &elements);
    Warning(info, "Empty has %ld nodes and %f elements", nodes, elements);

    int element_2[2] = {0, 0};
    vset_t singleton_2 = vset_create(domain, -1, NULL);
    vset_t empty_2     = vset_create(domain, -1, NULL);
    vset_t set_2       = vset_create(domain, -1, NULL);
    vset_t projection  = vset_create(domain, 0, NULL);

    vset_add(singleton_2, element_2);

    if (vset_is_empty(singleton_2)) Abort("Singeton empty");

    vset_project(projection, singleton_2);

    if (vset_equal(projection, singleton)) {
        Warning(info, "Singleton projection correct");
    } else {
        Abort("Singleton projection incorrect");
    }

    vset_project(projection, empty_2);

    if (vset_equal(projection, empty)) {
        Warning(info, "Empty set projection correct (equal)");
    } else {
        Abort("Empty set projection incorrect (equal)");
    }

    if (vset_is_empty(projection)) {
        Warning(info, "Empty set projection correct (empty)");
    } else {
        Abort("Empty set projection incorrect (empty)");
    }

    vset_copy_match(set_2, singleton_2, 0, NULL, element_local);
    if (!vset_is_empty(set_2)) {
        Warning(info, "Copy match singleton correct");
    } else {
        Abort("Copy match singleton incorrect");
    }

    count = 0;
    vset_enum_match(singleton_2, 0, NULL, element_local, count_cb, &count);

    if (count == 1) {
        Warning(info, "Enum match singleton correct");
    } else {
        Abort("Enum match singleton incorrect");
    }

    vset_copy_match(set_2, empty_2, 0, NULL, element_local);
    if (vset_is_empty(set_2)) {
        Warning(info, "Copy match empty correct");
    } else {
        Abort("Copy match empty incorrect");
    }

    count = 0;
    vset_enum_match(empty_2, 0, NULL, element_local, count_cb, &count);

    if (count == 0) {
        Warning(info, "Enum match empty correct");
    } else {
        Abort("Enum match empty incorrect");
    }

    vset_destroy(singleton);
    vset_destroy(empty);
    Warning(info, "Elements destroyed");
}

void
empty_projection_rel_test()
{
    vrel_t singleton = vrel_create(domain, 0, NULL);
    vrel_t empty     = vrel_create(domain, 0, NULL);
    int    element_local[0];

    vrel_add(singleton, element_local, element_global);

    double elements;
    long nodes;

    vrel_count(singleton, &nodes, &elements);
    Warning(info, "Singeton rel has %ld nodes and %f elements", nodes, elements);

    vrel_count(empty, &nodes, &elements);
    Warning(info, "Empty rel has %ld nodes and %f elements", nodes, elements);

    vset_t singleton_set = vset_create(domain, -1, NULL);
    vset_t empty_set = vset_create(domain, -1, NULL);
    vset_t set = vset_create(domain, -1, NULL);

    int element[2] = {0, 1};

    vset_add(singleton_set, element);

    vset_next(set, singleton_set, singleton);

    int count = 0;
    vset_enum(set, count_cb, &count);

    if (count == 1) {
        Warning(info, "Next count singleton correct");
    } else {
        Abort("Next count singleton incorrect");
    }

    vset_next(set, singleton_set, empty);
    if (!vset_is_empty(set)) Abort("Next singleton, empty incorrect");
    vset_next(set, empty_set, empty);
    if (!vset_is_empty(set)) Abort("Next empty, empty incorrect");
    vset_next(set, empty_set, singleton);
    if (!vset_is_empty(set)) Abort("Next empty, singleton incorrect");
    Warning(info, "Next empty correct");

    vset_prev(set, singleton_set, singleton, singleton_set);

    count = 0;
    vset_enum(set, count_cb, &count);

    if (count == 1) {
        Warning(info, "Prev count singleton correct");
    } else {
        Abort("Prev count singleton incorrect");
    }

    vset_prev(set, singleton_set, empty, singleton_set);
    if (!vset_is_empty(set)) Abort("Prev singleton, empty incorrect");
    vset_prev(set, empty_set, empty, empty_set);
    if (!vset_is_empty(set)) Abort("Prev empty, empty incorrect");
    vset_prev(set, empty_set, singleton, empty_set);
    if (!vset_is_empty(set)) Abort("Prev empty, singleton incorrect");
    Warning(info, "Prev empty correct");

}

int
main(int argc, char *argv[])
{
#ifdef HAVE_SYLVAN
    struct args_t args = (struct args_t){argc, argv};
    poptContext optCon = poptGetContext(NULL, argc, (const char**)argv, lace_options, 0);
    while(poptGetNextOpt(optCon) != -1 ) { /* ignore errors */ }
    poptFreeContext(optCon);


    lace_init(lace_n_workers, lace_dqsize);
    lace_startup(lace_stacksize, NULL, (void*)&args);
#endif

    HREinitBegin(argv[0]); // the organizer thread is called after the binary
    HREaddOptions(options,"Vector set test\n\nOptions");
    HREinitStart(&argc,&argv,0,0,NULL,NULL);

    vset_implementation_t vset_impl = VSET_IMPL_AUTOSELECT;

    domain = vdom_create_domain(dom_size, vset_impl);

    create_project_test();
    empty_projection_set_test();
    empty_projection_rel_test();
}
