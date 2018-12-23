#include <hre/config.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-group.h>
#include <pins2lts-sym/alg/aux.h>
#include <pins2lts-sym/alg/reach.h>
#include <pins2lts-sym/alg/scc.h>
#include <pins2lts-sym/aux/options.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/atomics.h>
#include <mc-lib/bitvector-ll.h>
#include <util-lib/bitset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>

#ifdef HAVE_SYLVAN
#include <sylvan.h>
#endif

static int sweep_line;
#define SWEEP_MAX 100
static int atomic_idx;
static int atomic2_idx;
static vset_t non_atomic_tmp;
static vset_next_t vset_next_fn_old = NULL;

static vset_t level[SWEEP_MAX];
static vset_t sweep_tmp;

void
sweep_print_lines ()
{
    double states;
    long int nodes;
    Warning (info, "Sweep line queues:");
    for (int i = sweep_line; i < SWEEP_MAX; i++) {
        vset_count (level[i], &nodes, &states);
        if (states == 0) continue;
        Printf (info, "%d=%'.0f, ", i, states);
    }
    Printf (info, "\n");
}

void
print_level (const char *name, long eg_count, long next_count, long guard_count)
{
    if (PINS_USE_GUARDS) {
        Warning(info, "%s level %d exploration took %ld group checks, %ld next state calls and %ld guard evaluation calls",
                      name, sweep_line, eg_count, next_count, guard_count);
    } else {
        Warning(info, "%s level %d exploration took %ld group checks and %ld next state calls",
                        name, sweep_line, eg_count, next_count);
    }
    Warning (info, " ");//Sweep line %d finished. Removing visited states.", sweep_level);
}

void
gc_clean_atomic (vset_t v, int idx)
{
    int zero = 0;
    double states1, states2;
    long int nodes1, nodes2;
    vset_count (v, &nodes1, &states1);
    if (states1 == 0) return;
    vset_copy_match (v, v, 1, &idx, &zero);
    vset_count (v, &nodes2, &states2);
    Warning(info, "Pre GC cleanup of atomic states, kept %'.0f/%'.0f states (%'ld/%'ld nodes)",
                    states2, states1, nodes2, nodes1);
}

void
gc_pre_hook()
{
    int idx = lts_type_find_state(ltstype, "atomic");
    if (idx == -1) return;

    gc_clean_atomic (visited, idx);
    gc_clean_atomic (temp, idx);
    //gc_clean_atomic (new_reduced, idx);
    //gc_clean_atomic (new_states, idx);

    if (sweep) sweep_print_lines();
}

void
print_enum_cb (void *ctx,int *e)
{
    Printf (error, "%d", e[0]);
}

void
print_sweep_set (vset_t dst)
{
    vset_t set = vset_create (domain, 1, &sweep_idx);
    vset_project (set, dst);
    vset_enum (set, print_enum_cb, NULL);
    Printf (error, "\n");
    vset_destroy(set);
}

void
sweep_vset_next (vset_t dst, vset_t src, int group)
{
    vset_next_fn_old(dst, src, group);

    // filter non-atomic states (AWARI)
    int zero = 0;
    vset_copy_match (non_atomic_tmp, dst, 1, &atomic_idx, &zero);
    vset_copy_match (non_atomic_tmp, non_atomic_tmp, 1, &atomic2_idx, &zero);
    vset_minus(dst, non_atomic_tmp);

    // spread non-atomic states over buckets (sweep_idx should be a variable counting all taken stones)
    int l = sweep_line;
    while (!vset_is_empty(non_atomic_tmp)) {
        if (l >= SWEEP_MAX) {
            Warning (error, "Sweep level %d. Untreated values: ", sweep_line);
            print_sweep_set (non_atomic_tmp);
            Abort("Exiting");
        }
        vset_copy_match(sweep_tmp, non_atomic_tmp, 1, &sweep_idx, &l);
        HREassert (l != 1 || vset_is_empty(sweep_tmp));
        vset_union(level[l], sweep_tmp);
        vset_minus(non_atomic_tmp, sweep_tmp);
        l++;
    }
    vset_clear(non_atomic_tmp);
}

void
sweep_search(sat_proc_t sat_proc, reach_proc_t reach_proc, vset_t visited,
             char *etf_output)
{
    (void)etf_output;

    sweep_idx = lts_type_find_state(ltstype, sweep);
    if (sweep_idx == -1) Abort("Sweep line method: state slot '%s' does not exist.", sweep);
    Warning(info, "Starting sweep line for slot '%s' [%d]", sweep, sweep_idx);
    vset_next_fn_old = vset_next_fn;
    vset_next_fn = sweep_vset_next;

#ifdef HAVE_SYLVAN
    //sylvan_gc_hook_pregc (gc_pre_hook);
#endif

    bitvector_t reach_groups;
    long eg_count = 0;
    long next_count = 0;
    long guard_count = 0;
    double states;
    long int nodes;
    bitvector_create(&reach_groups, nGrps);
    bitvector_invert(&reach_groups);

    atomic_idx = lts_type_find_state(ltstype, "home");
    atomic2_idx = lts_type_find_state(ltstype, "next");
    non_atomic_tmp = vset_create(domain, -1, NULL);

    sweep_tmp = vset_create(domain, -1, NULL);
    for (int i = 0; i < SWEEP_MAX; i++)
        level[i] = vset_create (domain, -1, NULL);
    vset_copy(level[0], visited);
    Warning(info, "Starting sweep line with: ");
    print_sweep_set (visited);

    for (sweep_line = 0; sweep_line < SWEEP_MAX; sweep_line++) {
        vset_count (level[sweep_line], &nodes, &states);
        if (states == 0) continue;
        Warning(info, "-----------------------------------------");
        Warning(info, "Starting sweep line %'d with %'.0f states", sweep_line, states);

        do {
            if (log_active(infoLong)) {
                Printf (info, "\n");
                sweep_print_lines();
            }
            eg_count = 0;
            next_count = 0;
            guard_count = 0;
            vset_copy(visited, level[sweep_line]);
            sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);
            print_level ("Atomic", eg_count, next_count, guard_count);
        } while (!vset_is_empty(level[sweep_line]));

        Warning(info, "-----------------------------------------");
        print_level ("SWEEP", eg_count, next_count, guard_count);
        vset_clear(visited);
        vset_clear(new_reduced);
        vset_clear(new_states);
        vset_clear(temp);
        vset_destroy(level[sweep_line]);
    }
    vset_destroy(sweep_tmp);

    bitvector_free(&reach_groups);
}

