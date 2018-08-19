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


static int sweep_line;
#define SWEEP_MAX 100

vset_t level[SWEEP_MAX];
vset_t sweep_tmp;

void
print_level (long eg_count, long next_count, long guard_count)
{
    if (PINS_USE_GUARDS) {
        Warning(info, "Sweep level %d exploration took %ld group checks, %ld next state calls and %ld guard evaluation calls",
                      sweep_line, eg_count, next_count, guard_count);
    } else {
        Warning(info, "Sweep level %d exploration took %ld group checks and %ld next state calls",
                      sweep_line, eg_count, next_count);
    }
    Warning (info, " ");//Sweep line %d finished. Removing visited states.", sweep_level);
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
sweep_print_lines ()
{
    double states;
    long int nodes;
    Warning (info, "Sweep line queues:");
    for (int i = sweep_line + 1; i < SWEEP_MAX; i++) {
        vset_count (level[i], &nodes, &states);
        if (states == 0) continue;
        Printf (info, "%d=%'.0f, ", i, states);
    }
    Printf (info, "\n");
}

void
sweep_vset_next (vset_t dst, vset_t src, int group)
{
    vset_next_fn_old(dst, src, group);
    double states;
    long int nodes;

    if (dm_is_set(write_matrix, group, sweep_idx)) {
        int l = sweep_line;
        while (!vset_is_empty(dst)) {
            if (l >= SWEEP_MAX) {
                Warning (error, "Sweep level %d. Untreated values: ", sweep_line);
                print_sweep_set (dst);
                Abort("Exiting");
            }
            vset_copy_match(sweep_tmp, dst, 1, &sweep_idx, &l);
            vset_union(level[l], sweep_tmp);

//            vset_count (sweep_tmp, &nodes, &states);
//            Warning(info, "Sweep line %d --> %d : %.0f states", sweep_line, l, states);

            vset_minus(dst, sweep_tmp);
            l++;
        }
        vset_copy(dst, level[sweep_line]);
        vset_clear(level[sweep_line]);
    }
}

void
sweep_search(sat_proc_t sat_proc, reach_proc_t reach_proc, vset_t visited,
             char *etf_output)
{
    (void)etf_output;
    Warning(info, "Starting sweep line for slot '%s' [%d]", sweep, sweep_idx);

    bitvector_t reach_groups;
    long eg_count = 0;
    long next_count = 0;
    long guard_count = 0;
    double states;
    long int nodes;
    bitvector_create(&reach_groups, nGrps);
    bitvector_invert(&reach_groups);

    sweep_tmp = vset_create(domain, -1, NULL);
    for (int i = 0; i < SWEEP_MAX; i++)
        level[i] = vset_create (domain, -1, NULL);
    vset_copy(level[0], visited);
    Warning(info, "Starting sweep line with: ");
    print_sweep_set (visited);

    for (sweep_line = 0; sweep_line < SWEEP_MAX; sweep_line++) {
        vset_count (level[sweep_line], &nodes, &states);
        if (states == 0) continue;
        Warning(info, "Starting sweep line %'d with %'.0f states", sweep_line, states);

        eg_count = 0;
        next_count = 0;
        guard_count = 0;
        vset_copy(visited, level[sweep_line]);
        sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);
        print_level (eg_count, next_count, guard_count);
        vset_clear(visited);
        set_destroy(level[sweep_line]);
    }
    vset_destroy(sweep_tmp);

    bitvector_free(&reach_groups);
}

