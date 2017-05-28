// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <sylvan_int.h>
#include <hre/user.h>

/* Globals */
typedef struct set
{
    MDD mdd;   // LDD of the set
    int k;     // number of things in projection
    int *proj; // projection
    int size;  // size of state vectors in this set
    MDD meta;  // "meta" for set_project (vs full vector)
} *set_t;

typedef struct relation
{
    MDD mdd;              // LDD of the relation
    MDD meta;             // "meta" for set_next, set_prev
    int r_k, w_k;         // number of read/write in this relation
    int *r_proj, *w_proj; // read/write projection metadata
} *rel_t;

static int vector_size; // size of vector
static int next_count; // number of partitions of the transition relation
static rel_t *next; // each partition of the transition relation
static int actionbits = 0; // number of bits for action labels
static int has_actions = 0; // set when there are action labels

/* Load a set from file */
#define set_load(f) CALL(set_load, f)
TASK_1(set_t, set_load, FILE*, f)
{
    set_t set = (set_t)malloc(sizeof(struct set));

    if (fread(&set->k, sizeof(int), 1, f) != 1) Abort("Invalid input file!");
    if (set->k != -1) Abort("Invalid input file!");
    set->proj = NULL;

    lddmc_serialize_fromfile(f);
    size_t mdd;
    if (fread(&mdd, sizeof(size_t), 1, f) != 1) Abort("Invalid input file!");
    set->mdd = lddmc_serialize_get_reversed(mdd);
    lddmc_protect(&set->mdd);

    // compute size
    set->size = vector_size;

    // compute meta
    uint32_t meta = -1;
    set->meta = lddmc_cube(&meta, 1);
    lddmc_protect(&set->meta);

    return set;
}

/* Load a relation from file */
#define rel_load_proj(f) CALL(rel_load_proj, f)
TASK_1(rel_t, rel_load_proj, FILE*, f)
{
    int r_k, w_k;
    if (fread(&r_k, sizeof(int), 1, f) != 1) Abort("Invalid file format.");
    if (fread(&w_k, sizeof(int), 1, f) != 1) Abort("Invalid file format.");

    rel_t rel = (rel_t)malloc(sizeof(struct relation));
    rel->r_k = r_k;
    rel->w_k = w_k;
    rel->r_proj = (int*)malloc(sizeof(int[rel->r_k]));
    rel->w_proj = (int*)malloc(sizeof(int[rel->w_k]));

    if (fread(rel->r_proj, sizeof(int), rel->r_k, f) != (size_t)rel->r_k) Abort("Invalid file format.");
    if (fread(rel->w_proj, sizeof(int), rel->w_k, f) != (size_t)rel->w_k) Abort("Invalid file format.");
    
    int *r_proj = rel->r_proj;
    int *w_proj = rel->w_proj;

    /* Compute the meta */
    uint32_t meta[vector_size*2+2];
    memset(meta, 0, sizeof(uint32_t[vector_size*2+2]));
    int r_i=0, w_i=0, i=0, j=0;
    for (;;) {
        int type = 0;
        if (r_i < r_k && r_proj[r_i] == i) {
            r_i++;
            type += 1; // read
        }
        if (w_i < w_k && w_proj[w_i] == i) {
            w_i++;
            type += 2; // write
        }
        if (type == 0) meta[j++] = 0;
        else if (type == 1) { meta[j++] = 3; }
        else if (type == 2) { meta[j++] = 4; }
        else if (type == 3) { meta[j++] = 1; meta[j++] = 2; }
        if (r_i == r_k && w_i == w_k) {
            meta[j++] = 5; // action label
            meta[j++] = (uint32_t)-1;
            break;
        }
        i++;
    }

    rel->meta = lddmc_cube((uint32_t*)meta, j);
    rel->mdd = lddmc_false;

    lddmc_protect(&rel->meta);
    lddmc_protect(&rel->mdd);

    return rel;
}

#define rel_load(f, rel) CALL(rel_load, f, rel)
VOID_TASK_2(rel_load, FILE*, f, rel_t, rel)
{
    lddmc_serialize_fromfile(f);
    size_t mdd;
    if (fread(&mdd, sizeof(size_t), 1, f) != 1) Abort("Invalid input file!");
    rel->mdd = lddmc_serialize_get_reversed(mdd);
}

/**
 * Compute the highest value for each variable level.
 * This method is called for the set of reachable states.
 */
static uint64_t compute_highest_id;
#define compute_highest(dd, arr) CALL(compute_highest, dd, arr)
VOID_TASK_2(compute_highest, MDD, dd, uint32_t*, arr)
{
    if (dd == lddmc_true || dd == lddmc_false) return;

    uint64_t result = 1;
    if (cache_get3(compute_highest_id, dd, 0, 0, &result)) return;
    cache_put3(compute_highest_id, dd, 0, 0, result);

    mddnode_t n = LDD_GETNODE(dd);

    SPAWN(compute_highest, mddnode_getright(n), arr);
    CALL(compute_highest, mddnode_getdown(n), arr+1);
    SYNC(compute_highest);

    if (!mddnode_getcopy(n)) {
        const uint32_t v = mddnode_getvalue(n);
        while (1) {
            const uint32_t cur = *(volatile uint32_t*)arr;
            if (v <= cur) break;
            if (__sync_bool_compare_and_swap(arr, cur, v)) break;
        }
    }
}

/**
 * Compute the highest value for the action label.
 * This method is called for each transition relation.
 */
static uint64_t compute_highest_action_id;
#define compute_highest_action(dd, meta, arr) CALL(compute_highest_action, dd, meta, arr)
VOID_TASK_3(compute_highest_action, MDD, dd, MDD, meta, uint32_t*, target)
{
    if (dd == lddmc_true || dd == lddmc_false) return;
    if (meta == lddmc_true) return;
    
    uint64_t result = 1;
    if (cache_get3(compute_highest_action_id, dd, meta, 0, &result)) return;
    cache_put3(compute_highest_action_id, dd, meta, 0, result);

    /*
     * meta:
     *  0 is skip level
     *  1 is read level
     *  2 is write level
     *  3 is only-read level
     *  4 is only-write level
     *  5 is action label (at end, before -1)
     * -1 is end
     */

    const mddnode_t n = LDD_GETNODE(dd);
    const mddnode_t nmeta = LDD_GETNODE(meta);
    const uint32_t vmeta = mddnode_getvalue(nmeta);
    if (vmeta == (uint32_t)-1) return;

    SPAWN(compute_highest_action, mddnode_getright(n), meta, target);
    CALL(compute_highest_action, mddnode_getdown(n), mddnode_getdown(nmeta), target);
    SYNC(compute_highest_action);

    if (vmeta == 5) {
        has_actions = 1;
        const uint32_t v = mddnode_getvalue(n);
        while (1) {
            const uint32_t cur = *(volatile uint32_t*)target;
            if (v <= cur) break;
            if (__sync_bool_compare_and_swap(target, cur, v)) break;
        }
    }
}

/**
 * Compute the BDD equivalent of the LDD of a set of states.
 */
static uint64_t bdd_from_ldd_id;
#define bdd_from_ldd(dd, bits, firstvar) CALL(bdd_from_ldd, dd, bits, firstvar)
TASK_3(MTBDD, bdd_from_ldd, MDD, dd, MDD, bits_mdd, uint32_t, firstvar)
{
    /* simple for leaves */
    if (dd == lddmc_false) return mtbdd_false;
    if (dd == lddmc_true) return mtbdd_true;

    MTBDD result;
    /* get from cache */
    /* note: some assumptions about the encoding... */
    if (cache_get3(bdd_from_ldd_id, dd, bits_mdd, firstvar, &result)) return result;

    mddnode_t n = LDD_GETNODE(dd);
    mddnode_t nbits = LDD_GETNODE(bits_mdd);
    int bits = (int)mddnode_getvalue(nbits);

    /* spawn right, same bits_mdd and firstvar */
    mtbdd_refs_spawn(SPAWN(bdd_from_ldd, mddnode_getright(n), bits_mdd, firstvar));

    /* call down, with next bits_mdd and firstvar */
    MTBDD down = CALL(bdd_from_ldd, mddnode_getdown(n), mddnode_getdown(nbits), firstvar + 2*bits);

    /* encode current value */
    uint32_t val = mddnode_getvalue(n);
    for (int i=0; i<bits; i++) {
        /* encode with high bit first */
        int bit = bits-i-1;
        if (val & (1LL<<i)) down = mtbdd_makenode(firstvar + 2*bit, mtbdd_false, down);
        else down = mtbdd_makenode(firstvar + 2*bit, down, mtbdd_false);
    }

    /* sync right */
    mtbdd_refs_push(down);
    MTBDD right = mtbdd_refs_sync(SYNC(bdd_from_ldd));

    /* take union of current and right */
    mtbdd_refs_push(right);
    result = sylvan_or(down, right);
    mtbdd_refs_pop(2);

    /* put in cache */
    cache_put3(bdd_from_ldd_id, dd, bits_mdd, firstvar, result);

    return result;
}

/**
 * Compute the BDD equivalent of an LDD transition relation.
 */
static uint64_t bdd_from_ldd_rel_id;
#define bdd_from_ldd_rel(dd, bits, firstvar, meta) CALL(bdd_from_ldd_rel, dd, bits, firstvar, meta)
TASK_4(MTBDD, bdd_from_ldd_rel, MDD, dd, MDD, bits_mdd, uint32_t, firstvar, MDD, meta)
{
    if (dd == lddmc_false) return mtbdd_false;
    if (dd == lddmc_true) return mtbdd_true;
    assert(meta != lddmc_false && meta != lddmc_true);

    /* meta:
     * -1 is end
     *  0 is skip
     *  1 is read
     *  2 is write
     *  3 is only-read
     *  4 is only-write
     */

    MTBDD result;
    /* note: assumptions */
    if (cache_get4(bdd_from_ldd_rel_id, dd, bits_mdd, firstvar, meta, &result)) return result;

    const mddnode_t n = LDD_GETNODE(dd);
    const mddnode_t nmeta = LDD_GETNODE(meta);
    const mddnode_t nbits = LDD_GETNODE(bits_mdd);
    const int bits = (int)mddnode_getvalue(nbits);

    const uint32_t vmeta = mddnode_getvalue(nmeta);
    assert(vmeta != (uint32_t)-1);

    if (vmeta == 0) {
        /* skip level */
        result = bdd_from_ldd_rel(dd, mddnode_getdown(nbits), firstvar + 2*bits, mddnode_getdown(nmeta));
    } else if (vmeta == 1) {
        /* read level */
        assert(!mddnode_getcopy(n));  // do not process read copy nodes for now
        assert(mddnode_getright(n) != mtbdd_true);

        /* spawn right */
        mtbdd_refs_spawn(SPAWN(bdd_from_ldd_rel, mddnode_getright(n), bits_mdd, firstvar, meta));

        /* compute down with same bits / firstvar */
        MTBDD down = bdd_from_ldd_rel(mddnode_getdown(n), bits_mdd, firstvar, mddnode_getdown(nmeta));
        mtbdd_refs_push(down);

        /* encode read value */
        uint32_t val = mddnode_getvalue(n);
        MTBDD part = mtbdd_true;
        for (int i=0; i<bits; i++) {
            /* encode with high bit first */
            int bit = bits-i-1;
            if (val & (1LL<<i)) part = mtbdd_makenode(firstvar + 2*bit, mtbdd_false, part);
            else part = mtbdd_makenode(firstvar + 2*bit, part, mtbdd_false);
        }

        /* intersect read value with down result */
        mtbdd_refs_push(part);
        down = sylvan_and(part, down);
        mtbdd_refs_pop(2);

        /* sync right */
        mtbdd_refs_push(down);
        MTBDD right = mtbdd_refs_sync(SYNC(bdd_from_ldd_rel));

        /* take union of current and right */
        mtbdd_refs_push(right);
        result = sylvan_or(down, right);
        mtbdd_refs_pop(2);
    } else if (vmeta == 2 || vmeta == 4) {
        /* write or only-write level */

        /* spawn right */
        assert(mddnode_getright(n) != mtbdd_true);
        mtbdd_refs_spawn(SPAWN(bdd_from_ldd_rel, mddnode_getright(n), bits_mdd, firstvar, meta));

        /* get recursive result */
        MTBDD down = CALL(bdd_from_ldd_rel, mddnode_getdown(n), mddnode_getdown(nbits), firstvar + 2*bits, mddnode_getdown(nmeta));

        if (mddnode_getcopy(n)) {
            /* encode a copy node */
            for (int i=0; i<bits; i++) {
                int bit = bits-i-1;
                MTBDD low = mtbdd_makenode(firstvar + 2*bit + 1, down, mtbdd_false);
                mtbdd_refs_push(low);
                MTBDD high = mtbdd_makenode(firstvar + 2*bit + 1, mtbdd_false, down);
                mtbdd_refs_pop(1);
                down = mtbdd_makenode(firstvar + 2*bit, low, high);
            }
        } else {
            /* encode written value */
            uint32_t val = mddnode_getvalue(n);
            for (int i=0; i<bits; i++) {
                /* encode with high bit first */
                int bit = bits-i-1;
                if (val & (1LL<<i)) down = mtbdd_makenode(firstvar + 2*bit + 1, mtbdd_false, down);
                else down = mtbdd_makenode(firstvar + 2*bit + 1, down, mtbdd_false);
            }
        }

        /* sync right */
        mtbdd_refs_push(down);
        MTBDD right = mtbdd_refs_sync(SYNC(bdd_from_ldd_rel));

        /* take union of current and right */
        mtbdd_refs_push(right);
        result = sylvan_or(down, right);
        mtbdd_refs_pop(2);
    } else if (vmeta == 3) {
        /* only-read level */
        assert(!mddnode_getcopy(n));  // do not process read copy nodes

        /* spawn right */
        mtbdd_refs_spawn(SPAWN(bdd_from_ldd_rel, mddnode_getright(n), bits_mdd, firstvar, meta));

        /* get recursive result */
        MTBDD down = CALL(bdd_from_ldd_rel, mddnode_getdown(n), mddnode_getdown(nbits), firstvar + 2*bits, mddnode_getdown(nmeta));

        /* encode read value */
        uint32_t val = mddnode_getvalue(n);
        for (int i=0; i<bits; i++) {
            /* encode with high bit first */
            int bit = bits-i-1;
            /* only-read, so write same value */
            if (val & (1LL<<i)) down = mtbdd_makenode(firstvar + 2*bit + 1, mtbdd_false, down);
            else down = mtbdd_makenode(firstvar + 2*bit + 1, down, mtbdd_false);
            if (val & (1LL<<i)) down = mtbdd_makenode(firstvar + 2*bit, mtbdd_false, down);
            else down = mtbdd_makenode(firstvar + 2*bit, down, mtbdd_false);
        }

        /* sync right */
        mtbdd_refs_push(down);
        MTBDD right = mtbdd_refs_sync(SYNC(bdd_from_ldd_rel));

        /* take union of current and right */
        mtbdd_refs_push(right);
        result = sylvan_or(down, right);
        mtbdd_refs_pop(2);
    } else if (vmeta == 5) {
        assert(!mddnode_getcopy(n));  // not allowed!

        /* we assume this is the last value */
        result = mtbdd_true;

        /* encode action value */
        uint32_t val = mddnode_getvalue(n);
        for (int i=0; i<actionbits; i++) {
            /* encode with high bit first */
            int bit = actionbits-i-1;
            /* only-read, so write same value */
            if (val & (1LL<<i)) result = mtbdd_makenode(1000000 + bit, mtbdd_false, result);
            else result = mtbdd_makenode(1000000 + bit, result, mtbdd_false);
        }
    } else {
        assert(vmeta <= 5);
    }

    cache_put4(bdd_from_ldd_rel_id, dd, bits_mdd, firstvar, meta, result);

    return result;
}

/**
 * Compute the BDD equivalent of the meta variable (to a variables cube)
 */
MTBDD
meta_to_bdd(MDD meta, MDD bits_mdd, uint32_t firstvar)
{
    if (meta == lddmc_false || meta == lddmc_true) return mtbdd_true;

    /* meta:
     * -1 is end
     *  0 is skip (no variables)
     *  1 is read (variables added by write)
     *  2 is write
     *  3 is only-read
     *  4 is only-write
     */

    const mddnode_t nmeta = LDD_GETNODE(meta);
    const uint32_t vmeta = mddnode_getvalue(nmeta);
    if (vmeta == (uint32_t)-1) return mtbdd_true;
    
    if (vmeta == 1) {
        /* return recursive result, don't go down on bits */
        return meta_to_bdd(mddnode_getdown(nmeta), bits_mdd, firstvar);
    }

    const mddnode_t nbits = LDD_GETNODE(bits_mdd);
    const int bits = (int)mddnode_getvalue(nbits);

    /* compute recursive result */
    MTBDD res = meta_to_bdd(mddnode_getdown(nmeta), mddnode_getdown(nbits), firstvar + 2*bits);

    /* add our variables if meta is 2,3,4 */
    if (vmeta != 0 && vmeta != 5) {
        for (int i=0; i<bits; i++) {
            res = mtbdd_makenode(firstvar + 2*(bits-i-1) + 1, mtbdd_false, res);
            res = mtbdd_makenode(firstvar + 2*(bits-i-1), mtbdd_false, res);
        }
    }

    return res;
}

/**
 * Lace options
 */
static size_t lace_n_workers = 0; // autodetect
static size_t lace_dqsize = 1024*1024*4; // set large default (virtual memory anyway)
static size_t lace_stacksize = 0; // use default

static struct poptOption lace_options[] = {
    { "lace-workers", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_n_workers , 0 , "set number of Lace workers (threads for parallelization)","<workers>"},
    { "lace-dqsize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_dqsize , 0 , "set length of Lace task queue","<dqsize>"},
    { "lace-stacksize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_stacksize, 0, "set size of program stack in kilo bytes (0=default stack size)", "<stacksize>"},
    POPT_TABLEEND
};

/**
 * Sylvan options (just the sizes of the tables)
 */

static char* sizes = "22,27,21,26";

struct poptOption sylvan_options[] = {
    { "sylvan-sizes", 0, POPT_ARG_STRING, &sizes, 0, "set nodes table and operation cache sizes (powers of 2)", "<tablesize>,<tablemax>,<cachesize>,<cachemax>"},
    POPT_TABLEEND
};

/**
 * Program options
 */
static int check_results = 0; // by default, do not check computed transition relations
static int no_reachable = 0; // by default, write reachable states

static struct poptOption options[] = {
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, lace_options, 0, "Lace options", NULL},
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, sylvan_options, 0, "Sylvan options", NULL},
    { "check-results", 0, 0, &check_results, 0, NULL, NULL },
    { "no-reachable", 0, 0, &no_reachable, 0, NULL, NULL },
    POPT_TABLEEND
};

struct args_t
{
    int argc;
    char **argv;
};

static char *files[2];

VOID_TASK_1(init_hre, hre_context_t, context)
{
    if (LACE_WORKER_ID != 0) {
        HREprocessSet(context);
        HREglobalSet(context);
    }
}

/**
 * Small helper function
 */
static char*
to_h(double size, char *buf)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    int i = 0;
    for (;size>1024;size/=1024) i++;
    sprintf(buf, "%.*f %s", i, size, units[i]);
    return buf;
}

VOID_TASK_0(gc_start)
{
    Print(info, "Starting garbage collection");
}

VOID_TASK_0(gc_end)
{
    Print(info, "Garbage collection done");
}

VOID_TASK_1(actual_main, void*, arg)
{
    int argc = ((struct args_t*)arg)->argc;
    char **argv = ((struct args_t*)arg)->argv;

    /* initialize HRE */
    HREinitBegin(argv[0]);
    HREaddOptions(options, "Convert an LDD file (generated by *lts-sym) to a BDD file\n\nOptions");
    // lts_lib_setup(); // add options for LTS library
    HREinitStart(&argc, &argv, 2, 2, files, "<input> <output>");

    /* initialize HRE on other workers */
    TOGETHER(init_hre, HREglobal());

    int verbose = log_active(infoLong);

    int tablesize, maxtablesize, cachesize, maxcachesize;
    if (sscanf(sizes, "%d,%d,%d,%d", &tablesize, &maxtablesize, &cachesize, &maxcachesize) != 4) {
        Abort("Invalid string for --sylvan-sizes, try e.g. --sylvan-sizes=23,28,22,27");
    }
    if (tablesize < 10 || maxtablesize < 10 || cachesize < 10 || maxcachesize < 10 ||
            tablesize > 40 || maxtablesize > 40 || cachesize > 40 || maxcachesize > 40) {
        Abort("Invalid string for --sylvan-sizes, must be between 10 and 40");
    }
    if (tablesize > maxtablesize) {
        Abort("Invalid string for --sylvan-sizes, tablesize is larger than maxtablesize");
    }
    if (cachesize > maxcachesize) {
        Abort("Invalid string for --sylvan-sizes, cachesize is larger than maxcachesize");
    }

    char buf[32];
    to_h((1ULL<<maxtablesize)*24+(1ULL<<maxcachesize)*36, buf);
    Print(info, "Sylvan allocates %s virtual memory for nodes table and operation cache.", buf);
    to_h((1ULL<<tablesize)*24+(1ULL<<cachesize)*36, buf);
    Print(info, "Initial nodes table and operation cache requires %s.", buf);

    FILE *f = fopen(files[0], "r");
    if (f == NULL) Abort("Cannot open file '%s'!", files[0]);

    // Init Sylvan
    sylvan_set_sizes(1LL<<tablesize, 1LL<<maxtablesize, 1LL<<cachesize, 1LL<<maxcachesize);
    sylvan_init_package();
    sylvan_init_ldd();
    sylvan_init_mtbdd();
    sylvan_gc_hook_pregc(TASK(gc_start));
    sylvan_gc_hook_postgc(TASK(gc_end));

    // Obtain operation ids for the operation cache
    compute_highest_id = cache_next_opid();
    compute_highest_action_id = cache_next_opid();
    bdd_from_ldd_id = cache_next_opid();
    bdd_from_ldd_rel_id = cache_next_opid();

    // Read integers per vector
    if (fread(&vector_size, sizeof(int), 1, f) != 1) Abort("Invalid input file!");

    // Read initial state
    if (verbose) Print(info, "Loading initial state... ");
    set_t initial = set_load(f);

    // Read number of transitions
    if (fread(&next_count, sizeof(int), 1, f) != 1) Abort("Invalid input file!");
    next = (rel_t*)malloc(sizeof(rel_t) * next_count);

    // Read transitions
    if (verbose) Print(info, "Loading transition relations... ");
    for (int i=0; i<next_count; i++) next[i] = rel_load_proj(f);
    for (int i=0; i<next_count; i++) rel_load(f, next[i]);

    // Read whether reachable states are stored
    int has_reachable = 0;
    if (fread(&has_reachable, sizeof(int), 1, f) != 1) Abort("Input file missing reachable states!");
    if (has_reachable == 0) Abort("Input file missing reachable states!");

    // Read reachable states
    if (verbose) Print(info, "Loading reachable states... ");
    set_t states = set_load(f);
    
    // Read number of action labels
    int action_labels_count = 0;
    if (fread(&action_labels_count, sizeof(int), 1, f) != 1) Abort("Input file missing action label count!");

    // Read action labels
    char *action_labels[action_labels_count];
    for (int i=0; i<action_labels_count; i++) {
        uint32_t len;
        if (fread(&len, sizeof(uint32_t), 1, f) != 1) Abort("Invalid input file!");
        action_labels[i] = (char*)malloc(sizeof(char[len+1]));
        if (fread(action_labels[i], sizeof(char), len, f) != len) Abort("Invalid input file!");
        action_labels[i][len] = 0;
    }

    // Close file
    fclose(f);

    // Report that we have read the inpute file
    Print(info, "Read file %s.", files[0]);

    // Report statistics
    if (verbose) {
        Print(info, "%d integers per state, %d transition groups", vector_size, next_count);
        Print(info, "Initial states: %zu LDD nodes", lddmc_nodecount(initial->mdd));
        Print(info, "Reachable states: %zu LDD nodes", lddmc_nodecount(states->mdd));
        for (int i=0; i<next_count; i++) {
            Print(info, "Transition %d: %zu LDD nodes", i, lddmc_nodecount(next[i]->mdd));
        }
    }

    // Report that we prepare BDD conversion
    if (verbose) Print(info, "Preparing conversion to BDD...");

    // Compute highest value at each level (from reachable states)
    uint32_t highest[vector_size];
    for (int i=0; i<vector_size; i++) highest[i] = 0;
    compute_highest(states->mdd, highest);

    // Compute highest action label value (from transition relations)
    uint32_t highest_action = 0;
    for (int i=0; i<next_count; i++) {
        compute_highest_action(next[i]->mdd, next[i]->meta, &highest_action);
    }

    // Compute number of bits for each level
    int bits[vector_size];
    for (int i=0; i<vector_size; i++) {
        bits[i] = 0;
        while (highest[i] != 0) {
            bits[i]++;
            highest[i]>>=1;
        }
        if (bits[i] == 0) bits[i] = 1;
    }

    // Compute number of bits for action label
    actionbits = 0;
    while (highest_action != 0) {
        actionbits++;
        highest_action>>=1;
    }
    if (actionbits == 0 && has_actions) actionbits = 1;

    // Compute bits MDD
    MDD bits_mdd = lddmc_true;
    for (int i=0; i<vector_size; i++) {
        bits_mdd = lddmc_makenode(bits[vector_size-i-1], bits_mdd, lddmc_false);
    }
    lddmc_ref(bits_mdd);

    // Compute total number of bits
    int totalbits = 0;
    for (int i=0; i<vector_size; i++) {
        totalbits += bits[i];
    }

    // Compute state variables
    MTBDD state_vars = mtbdd_true;
    for (int i=0; i<totalbits; i++) {
        state_vars = mtbdd_makenode(2*(totalbits-i-1), mtbdd_false, state_vars);
    }
    mtbdd_protect(&state_vars);

    // Report that we begin the actual conversion
    if (verbose) Print(info, "Converting to BDD...");

    // Create BDD file
    f = fopen(files[1], "w");
    if (f == NULL) Abort("Cannot open file '%s'!", files[1]);

    // Write domain...
    fwrite(&vector_size, sizeof(int), 1, f);
    fwrite(bits, sizeof(int), vector_size, f);
    fwrite(&actionbits, sizeof(int), 1, f);

    // Write initial state...
    MTBDD new_initial = bdd_from_ldd(initial->mdd, bits_mdd, 0);
    assert((size_t)mtbdd_satcount(new_initial, totalbits) == (size_t)lddmc_satcount_cached(initial->mdd));
    mtbdd_refs_push(new_initial);
    {
        fwrite(&initial->k, sizeof(int), 1, f);
        if (initial->k != -1) fwrite(initial->proj, sizeof(int), initial->k, f);
        mtbdd_writer_tobinary(f, &new_initial, 1);
    }

    // Custom operation that converts to BDD given number of bits for each level
    MTBDD new_states = bdd_from_ldd(states->mdd, bits_mdd, 0);
    assert((size_t)mtbdd_satcount(new_states, totalbits) == (size_t)lddmc_satcount_cached(states->mdd));
    mtbdd_refs_push(new_states);  // ref, because written later and used for testing converted relations

    // Report size of BDD
    if (verbose) {
        Print(info, "Initial states: %zu BDD nodes", mtbdd_nodecount(new_initial));
        Print(info, "Reachable states: %zu BDD nodes", mtbdd_nodecount(new_states));
    }

    // Write number of transitions
    fwrite(&next_count, sizeof(int), 1, f);

    // Write meta for each transition
    for (int i=0; i<next_count; i++) {
        fwrite(&next[i]->r_k, sizeof(int), 1, f);
        fwrite(&next[i]->w_k, sizeof(int), 1, f);
        fwrite(next[i]->r_proj, sizeof(int), next[i]->r_k, f);
        fwrite(next[i]->w_proj, sizeof(int), next[i]->w_k, f);
    }

    // Write BDD for each transition
    for (int i=0; i<next_count; i++) {
        // Compute new transition relation
        MTBDD new_rel = bdd_from_ldd_rel(next[i]->mdd, bits_mdd, 0, next[i]->meta);
        mtbdd_refs_push(new_rel);
        mtbdd_writer_tobinary(f, &new_rel, 1);

        // Report number of nodes
        if (verbose) Print(info, "Transition %d: %zu BDD nodes", i, mtbdd_nodecount(new_rel));

        if (check_results) {
            // Compute new <variables> for the current transition relation
            MTBDD new_vars = meta_to_bdd(next[i]->meta, bits_mdd, 0);
            mtbdd_refs_push(new_vars);

            // Test if the transition is correctly converted
            MTBDD test = sylvan_relnext(new_states, new_rel, new_vars);
            mtbdd_refs_push(test);
            MDD succ = lddmc_relprod(states->mdd, next[i]->mdd, next[i]->meta);
            lddmc_refs_push(succ);
            MTBDD test2 = bdd_from_ldd(succ, bits_mdd, 0);
            if (test != test2) Abort("Conversion error!");
            lddmc_refs_pop(1);
            mtbdd_refs_pop(2);
        }

        mtbdd_refs_pop(1);
    }

    // Write reachable states
    if (no_reachable) has_reachable = 0;
    fwrite(&has_reachable, sizeof(int), 1, f);
    if (has_reachable) {
        int set_k = -1;
        fwrite(&set_k, sizeof(int), 1, f);
        mtbdd_writer_tobinary(f, &new_states, 1);
    }
    mtbdd_refs_pop(1);  // new_states

    // Write action labels
    fwrite(&action_labels_count, sizeof(int), 1, f);
    for (int i=0; i<action_labels_count; i++) {
        uint32_t len = strlen(action_labels[i]);
        fwrite(&len, sizeof(uint32_t), 1, f);
        fwrite(action_labels[i], sizeof(char), len, f);
    }

    // Close the file
    fclose(f);

    // Report to the user
    Print(info, "Written file %s.", files[1]);

    // Report Sylvan statistics (if SYLVAN_STATS is set)
    if (verbose) sylvan_stats_report(stdout);
}

int
main (int argc, char *argv[])
{
    poptContext optCon = poptGetContext(NULL, argc, (const char**)argv, lace_options, 0);
    while(poptGetNextOpt(optCon) != -1 ) { /* ignore errors */ }
    poptFreeContext(optCon);

    struct args_t args = (struct args_t){argc, argv};
    lace_init(lace_n_workers, lace_dqsize);
    lace_startup(lace_stacksize, TASK(actual_main), (void*)&args);

    return 0;
}
