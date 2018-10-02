#include <hre/config.h>

#include <float.h>
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <sys/mman.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-guards.h>
#include <pins-lib/pins2pins-group.h>
#include <pins-lib/pins2pins-mucalc.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/property-semantics.h>
#include <pins2lts-sym/alg/aux.h>
#include <pins2lts-sym/alg/bfs.h>
#include <pins2lts-sym/alg/chain.h>
#include <pins2lts-sym/alg/pg.h>
#include <pins2lts-sym/alg/mu.h>
#include <pins2lts-sym/alg/reach.h>
#include <pins2lts-sym/alg/sat.h>
#include <pins2lts-sym/aux/options.h>
#include <pins2lts-sym/aux/output.h>
#include <pins2lts-sym/aux/prop.h>
#include <pins2lts-sym/maxsum/maxsum.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/atomics.h>
#include <mc-lib/bitvector-ll.h>
#include <vset-lib/vector_set.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/bitset.h>

#ifdef HAVE_SYLVAN
#include <sylvan.h>
#else
#define LACE_ME
#define lace_suspend()
#define lace_resume()
#endif

hre_context_t ctx;

int vset_count_dbl(vset_t set, long* nodes, long double* elements);
int vset_count_ldbl(vset_t set, long* nodes, long double* elements);

vset_count_t vset_count_fn = &vset_count_dbl;

vset_next_t vset_next_fn = vset_next;

int
vset_count_dbl(vset_t set, long* nodes, long double* elements)
{
    if (elements != NULL) {
        *elements = 0.0;
        double e;
        vset_count(set, nodes, &e);
        *elements += e;
        if(vdom_supports_ccount(domain) && isinf(e)) {
            vset_count_fn = &vset_count_ldbl;
            vset_count_fn(set, nodes, elements);
            return LDBL_DIG;
        }
    } else {
        vset_count(set, nodes, NULL);
    }
    return DBL_DIG;
}

int
vset_count_ldbl(vset_t set, long* nodes, long double* elements)
{
    vset_ccount(set, nodes, elements);
    return LDBL_DIG;
}

typedef void(*vset_next_t)(vset_t dst, vset_t src, vrel_t rel);

static void
vset_next_union_src(vset_t dst, vset_t src, vrel_t rel)
{
    vset_next_union(dst, src, rel, src);
}

static void
init_model(char *file)
{
    Warning(info, "opening %s", file);
    model = GBcreateBase();
    GBsetChunkMap (model, HREgreyboxTableFactory());

    HREbarrier(HREglobal());
    GBloadFile(model, file, &model);

    HREbarrier(HREglobal());

    if (HREme(HREglobal())==0 && !PINS_USE_GUARDS && no_soundness_check) {
        Abort("Option --no-soundness-check is incompatible with --pins-guards=false");
    }

    if (HREme(HREglobal())==0 && log_active(infoLong) && !no_matrix) {
        fprintf(stderr, "Dependency Matrix:\n");
        GBprintDependencyMatrixCombined(stderr, model);
    }

    ltstype = GBgetLTStype(model);
    N = lts_type_get_state_length(ltstype);
    eLbls = lts_type_get_edge_label_count(ltstype);
    sLbls = GBgetStateLabelInfo(model) == NULL ? 0 : dm_nrows(GBgetStateLabelInfo(model));
    nGrps = dm_nrows(GBgetDMInfo(model));
    max_sat_levels = (N / sat_granularity) + 1;
    if (PINS_USE_GUARDS) {
        nGuards = GBgetStateLabelGroupInfo(model, GB_SL_GUARDS)->count;
        if (HREme(HREglobal())==0) {
            Warning(info, "state vector length is %d; there are %d groups and %d guards", N, nGrps, nGuards);
        }
    } else {
        if (HREme(HREglobal())==0) {
            Warning(info, "state vector length is %d; there are %d groups", N, nGrps);
        }
    }

    int id=GBgetMatrixID(model,"inhibit");
    if (id>=0){
        inhibit_matrix=GBgetMatrix(model,id);
        if (HREme(HREglobal())==0) {
            Warning(infoLong,"inhibit matrix is:");
            if (log_active(infoLong)) dm_print(stderr,inhibit_matrix);
        }
    }
    id = GBgetMatrixID(model,LTSMIN_EDGE_TYPE_ACTION_CLASS);
    if (id>=0){
        class_matrix=GBgetMatrix(model,id);
        if (HREme(HREglobal())==0) {
            Warning(infoLong,"inhibit class matrix is:");
            if (log_active(infoLong)) dm_print(stderr,class_matrix);
        }
    }

    HREbarrier(HREglobal());
}

static void
init_domain(vset_implementation_t impl) {
    domain = vdom_create_domain(N, impl);

    for (int i = 0; i < dm_ncols(GBgetDMInfo(model)); i++) {
        vdom_set_name(domain, i, lts_type_get_state_name(ltstype, i));
    }

    group_next     = (vrel_t*)RTmalloc(nGrps * sizeof(vrel_t));
    group_explored = (vset_t*)RTmalloc(nGrps * sizeof(vset_t));
    group_tmp      = (vset_t*)RTmalloc(nGrps * sizeof(vset_t));
    r_projs        = (ci_list **)RTmalloc(sizeof(ci_list *[nGrps]));
    w_projs        = (ci_list **)RTmalloc(sizeof(ci_list *[nGrps]));

    l_projs        = (ci_list **)RTmalloc(sizeof(ci_list *[sLbls]));
    label_false    = (vset_t*)RTmalloc(sLbls * sizeof(vset_t));
    label_true     = (vset_t*)RTmalloc(sLbls * sizeof(vset_t));
    label_tmp      = (vset_t*)RTmalloc(sLbls * sizeof(vset_t));

    if (!vdom_separates_rw(domain) && !PINS_USE_GUARDS) {
        read_matrix = GBgetDMInfo(model);
        write_matrix = GBgetDMInfo(model);
        Warning(info, "Using GBgetTransitionsShort as next-state function");
        transitions_short = GBgetTransitionsShort;
    } else if (!vdom_separates_rw(domain) && PINS_USE_GUARDS) {
        read_matrix = GBgetMatrix(model, GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS));
        write_matrix = GBgetDMInfo(model);
        Warning(info, "Using GBgetActionsShort as next-state function");
        transitions_short = GBgetActionsShort;
    } else if (vdom_separates_rw(domain) && !PINS_USE_GUARDS) {
        read_matrix = GBgetDMInfoRead(model);
        write_matrix = GBgetDMInfoMayWrite(model);
        Warning(info, "Using GBgetTransitionsShortR2W as next-state function");
        transitions_short = GBgetTransitionsShortR2W;
    } else { // vdom_separates_rw(domain) && PINS_USE_GUARDS
        read_matrix = GBgetMatrix(model, GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS));
        write_matrix = GBgetDMInfoMayWrite(model);
        Warning(info, "Using GBgetActionsShortR2W as next-state function");
        transitions_short = GBgetActionsShortR2W;
    }

    if (PINS_USE_GUARDS) {
        if (no_soundness_check) {
            Warning(info, "Guard-splitting: not checking soundness of the specification, this may result in an incorrect state space!");
        } else {
            Warning(info, "Guard-splitting: checking soundness of specification, this may be slow!");
        }
    }

    r_projs = (ci_list **) dm_rows_to_idx_table (read_matrix);
    w_projs = (ci_list **) dm_rows_to_idx_table (write_matrix);
    for(int i = 0; i < nGrps; i++) {

        if (HREme(HREglobal())==0)
        {
            if (vdom_separates_rw(domain)) {
                group_next[i] = vrel_create_rw (domain, r_projs[i]->count, r_projs[i]->data, w_projs[i]->count, w_projs[i]->data);
            } else {
                group_next[i] = vrel_create (domain, r_projs[i]->count, r_projs[i]->data);
            }

            group_explored[i] = vset_create (domain, r_projs[i]->count, r_projs[i]->data);
            group_tmp[i]      = vset_create (domain, r_projs[i]->count, r_projs[i]->data);

            if (inhibit_matrix != NULL) {
                inhibit_class_count = dm_nrows(inhibit_matrix);
                class_enabled = (vset_t*)RTmalloc(inhibit_class_count * sizeof(vset_t));
                for(int i=0; i<inhibit_class_count; i++) {
                    class_enabled[i] = vset_create(domain, -1, NULL);
                }
            }
        }
    }

    l_projs = (ci_list **) dm_rows_to_idx_table (GBgetStateLabelInfo(model));
    for (int i = 0; i < sLbls; i++) {

        /* Indeed, we skip unused state labels, but allocate memory for pointers
         * (to vset_t's). Is this bad? Maybe a hashmap is worse. */
        if (bitvector_is_set(&state_label_used, i)) {

            if (HREme(HREglobal()) == 0) {
                label_false[i]  = vset_create(domain, l_projs[i]->count, l_projs[i]->data);
                label_true[i]   = vset_create(domain, l_projs[i]->count, l_projs[i]->data);
                label_tmp[i]    = vset_create(domain, l_projs[i]->count, l_projs[i]->data);
            }
        } else {
            label_false[i]  = NULL;
            label_true[i]   = NULL;
            label_tmp[i]    = NULL;
        }
    }

    inv_set = (vset_t *) RTmalloc(sizeof(vset_t[num_inv]));
    for (int i = 0; i < num_inv; i++) {
        inv_set[i] = vset_create (domain, inv_proj[i]->count, inv_proj[i]->data);
        inv_info_prepare (inv_expr[i], inv_parse_env[i], i);
    }
}

#ifdef HAVE_SYLVAN
#define run_reachability(s,e) CALL(run_reachability,s,e)
VOID_TASK_2(run_reachability, vset_t, states, char*, etf_output)
#else
static void run_reachability(vset_t states, char *etf_output)
#endif
{
    sat_proc_t sat_proc = NULL;
    reach_proc_t reach_proc = NULL;
    guided_proc_t guided_proc = NULL;

    switch (strategy) {
    case BFS_P:
        reach_proc = reach_bfs_prev;
        break;
#ifdef HAVE_SYLVAN
    case PAR:
        reach_proc = reach_par;
        break;
    case PAR_P:
        reach_proc = reach_par_prev;
        break;
#endif
    case BFS:
        reach_proc = reach_bfs;
        break;
    case CHAIN_P:
        reach_proc = reach_chain_prev;
        break;
    case CHAIN:
        reach_proc = reach_chain;
        break;
    case NONE:
        reach_proc = reach_none;
        break;
    }

    switch (sat_strategy) {
    case NO_SAT:
        sat_proc = reach_no_sat;
        break;
    case SAT_LIKE:
        sat_proc = reach_sat_like;
        break;
    case SAT_LOOP:
        sat_proc = reach_sat_loop;
        break;
    case SAT_FIX:
        sat_proc = reach_sat_fix;
        break;
    case SAT:
        sat_proc = reach_sat;
        break;
    }

    switch (guide_strategy) {
    case UNGUIDED:
        guided_proc = unguided;
        break;
    case DIRECTED:
        guided_proc = directed;
        break;
    }

    RTstartTimer(reach_timer);
    guided_proc(sat_proc, reach_proc, states, etf_output);
    RTstopTimer(reach_timer);
}

static char *files[2];

struct args_t
{
    int argc;
    char **argv;
};

#ifdef HAVE_SYLVAN
#define init_hre(c) TOGETHER(init_hre, c)
VOID_TASK_1(init_hre, hre_context_t, context)
{
    if (LACE_WORKER_ID != 0) {
        HREprocessSet(context);
        HREglobalSet(context);
    }
}
#else
static void 
init_hre(hre_context_t context)
{       
    HREprocessSet(context);
    HREglobalSet(context); 
}
#endif

#ifdef HAVE_SYLVAN

VOID_TASK_1(actual_main, void*, arg)
#else
static void actual_main(void *arg)
#endif
{
    int argc = ((struct args_t*)arg)->argc;
    char **argv = ((struct args_t*)arg)->argv;

    /* initialize HRE */
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Perform a symbolic reachability analysis of <model>\n"
                  "The optional output of this analysis is an ETF "
                      "representation of the input\n\nOptions");
    lts_lib_setup(); // add options for LTS library
    HREinitStart(&argc,&argv,1,2,files,"<model> [<etf>]");

    /* initialize HRE on other workers */
    init_hre(HREglobal());

    /* check for unsupported options */
    if (PINS_POR != PINS_POR_NONE) Abort("Partial-order reduction and symbolic model checking are not compatible.");
    if (inhibit_matrix != NULL && sat_strategy != NO_SAT) Abort("Maximal progress is incompatibale with saturation.");
    if (files[1] != NULL) {
        char *ext = strrchr(files[1], '.');
        if (ext == NULL || ext == files[1]) {
            Abort("Output filename has no extension!");
        }
        if (strcasecmp(ext, ".etf") != 0) {
            // not ETF
            if (!(vset_default_domain == VSET_Sylvan && strcasecmp(ext, ".bdd") == 0) &&
                !(vset_default_domain == VSET_LDDmc  && strcasecmp(ext, ".ldd") == 0)) {
                Abort("Only supported output formats are ETF, BDD (with --vset=sylvan) and LDD (with --vset=lddmc)");
            }
            if (PINS_USE_GUARDS) {
                Abort("Exporting symbolic state space not comptabile with "
                        "guard-splitting");
            }
        }
    }

#ifdef HAVE_SYLVAN
    if (!USE_PARALLELISM) {
        if (strategy == PAR_P) {
            strategy = BFS_P;
            Print(info, "Front-end not thread-safe; using --order=bfs-prev instead of --order=par-prev.");
        } else if (strategy == PAR) {
            strategy = BFS;
            Print(info, "Front-end not thread-safe; using --order=bfs instead of --order=par.");
        }
    }
#endif

    /* turn off Lace for now to speed up while not using parallelism */
    lace_suspend();

    /* initialize the model and PINS wrappers */
    init_model(files[0]);

    /* initialize action detection */
    act_label = lts_type_find_edge_label_prefix (ltstype, LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    if (act_label != -1) action_typeno = lts_type_get_edge_label_typeno(ltstype, act_label);
    if (act_detect != NULL) init_action_detection();

    bitvector_create(&state_label_used, sLbls);
    if (inv_detect != NULL) init_invariant_detection();
    else if (PINS_USE_GUARDS) {
        for (int i = 0; i < nGuards; i++) {
            bitvector_set(&state_label_used, i);
        }
    }

    init_maxsum(ltstype);

    /* turn on Lace again (for Sylvan) */
    if (vset_default_domain==VSET_Sylvan || vset_default_domain==VSET_LDDmc) {
        lace_resume();
    }

    if (next_union) vset_next_fn = vset_next_union_src;

    init_domain(VSET_IMPL_AUTOSELECT);

    initial = vset_create(domain, -1, NULL);
    int src[N];
    GBgetInitialState(model, src);
    vset_add(initial, src);

    Print(infoShort, "got initial state");

    /* if writing .dot files, open directory first */
    if (dot_dir != NULL) {
        DIR* dir = opendir(dot_dir);
        if (dir) {
            closedir(dir);
        } else if (ENOENT == errno) {
            Abort("Option 'dot-dir': directory '%s' does not exist", dot_dir);
        } else {
            Abort("Option 'dot-dir': failed opening directory '%s'", dot_dir);
        }
    }

    if (vset_dir != NULL) {
        DIR *dir = opendir(vset_dir);
        if (dir) {
            closedir(dir);
        } else if (errno == ENOENT) {
            Abort("Option 'save-levels': directory '%s' does not exist", vset_dir);
        } else {
            Abort("Option 'save-levels': failed opening directory '%s'", vset_dir);
        }
    }

    init_mu_calculus();

    /* determine if we need to generate a symbolic parity game */
    bool spg;
    if (is_pbes_tool) {
	spg = true;
    } else {
	spg = GBhaveMucalc() ? true : false;
    }

    /* if spg, then initialize labeling stuff before reachability */
    if (spg) {
        Print(infoShort, "Generating a Symbolic Parity Game (SPG).");
        init_spg(model);
    }

    /* create timer */
    reach_timer = RTcreateTimer();

    /* fix level 0 */
    visited = vset_create(domain, -1, NULL);
    vset_copy(visited, initial);

    /* check the invariants at level 0 */
    check_invariants(visited, 0);

    /* run reachability */
    run_reachability(visited, files[1]);

    /* report states */
    final_stat_reporting(visited);

    /* save LTS */
    if (files[1] != NULL) {
        char *ext = strrchr(files[1], '.');
        if (strcasecmp(ext, ".etf") == 0) {
            do_output(files[1], visited);
        } else {
            // if not .etf, then the filename ends with .bdd or .ldd, symbolic LTS
            do_dd_output (files[1]);
        }
    }

    compute_maxsum(visited, domain);

    CHECK_MU(visited, src);

    if (max_mu_count > 0) {
        Print(info, "Mu-calculus peak nodes: %ld", max_mu_count);
    }

    /* optionally print counts of all group_next and group_explored sets */
    final_final_stats_reporting ();

    if (spg) { // converting the LTS to a symbolic parity game, save and solve.
        lts_to_pg_solve (visited, src);
    }

#ifdef HAVE_SYLVAN
    /* in case other Lace threads were still suspended... */
    if (vset_default_domain!=VSET_Sylvan && vset_default_domain!=VSET_LDDmc) {
        lace_resume();
    } else if (SYLVAN_STATS) {
        sylvan_stats_report(stderr);
    }
#endif

    GBExit(model);
}

int
main (int argc, char *argv[])
{
    struct args_t args = (struct args_t){argc, argv};
#ifdef HAVE_SYLVAN
    poptContext optCon = poptGetContext(NULL, argc, (const char**)argv, lace_options, 0);
    while(poptGetNextOpt(optCon) != -1 ) { /* ignore errors */ }
    poptFreeContext(optCon);

    lace_init(lace_n_workers, lace_dqsize);
    lace_startup(lace_stacksize, TASK(actual_main), (void*)&args);
#else
    actual_main((void*)&args);
#endif
    return 0;
}
