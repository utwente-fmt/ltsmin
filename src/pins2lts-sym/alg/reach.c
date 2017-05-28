#include <hre/config.h>

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <float.h>
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
#include <pins2lts-sym/aux/output.h>
#include <pins2lts-sym/aux/options.h>
#include <pins2lts-sym/aux/prop.h>
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
#else
#define LACE_ME
#define lace_suspend()
#define lace_resume()
#endif

void
reach_chain_stop() {
    if (!no_exit && ErrorActions > 0) {
        RTstopTimer(reach_timer);
        RTprintTimer(info, reach_timer, "action detection took");
        Warning(info, "Exiting now");
        GBExit(model);
        HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
    }
}

void
reach_stop (struct reach_s *node) {
    if (node->unsound_group > -1) {
        Warning(info, "Condition in group %d does not always evaluate to true or false", node->unsound_group);
        HREabort(LTSMIN_EXIT_UNSOUND);
    }
    reach_chain_stop();
}

void
reach_none (vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
            long *eg_count, long *next_count, long *guard_count)
{
    (void) visited; (void) visited_old; (void) reach_groups; (void) eg_count; (void) next_count; (void) guard_count;
    Warning(info, "not doing anything");
}


void
reach_no_sat(reach_proc_t reach_proc, vset_t visited, bitvector_t *reach_groups,
             long *eg_count, long *next_count, long *guard_count)
{
    vset_t old_visited = save_sat_levels?vset_create(domain, -1, NULL):NULL;

    reach_proc(visited, old_visited, reach_groups, eg_count, next_count, guard_count);

    if (save_sat_levels) vset_destroy(old_visited);
}

void
unguided(sat_proc_t sat_proc, reach_proc_t reach_proc, vset_t visited,
         char *etf_output)
{
    (void)etf_output;

    bitvector_t reach_groups;
    long eg_count = 0;
    long next_count = 0;
    long guard_count = 0;

    bitvector_create(&reach_groups, nGrps);
    bitvector_invert(&reach_groups);
    sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);
    bitvector_free(&reach_groups);
    if (PINS_USE_GUARDS) {
        Warning(info, "Exploration took %ld group checks, %ld next state calls and %ld guard evaluation calls",
                eg_count, next_count, guard_count);
    } else {
        Warning(info, "Exploration took %ld group checks and %ld next state calls",
                eg_count, next_count);
    }
}

/**
 * Find a group that overlaps with at least one of the groups in found_groups.
 * If a group is found, 1 is returned and the group argument is set to this
 * group. If no group is found, 0 is returned.
 */
static int
find_overlapping_group(bitvector_t *found_groups, int *group)
{
    bitvector_t row_found, row_new;

    bitvector_create(&row_found, N);
    bitvector_create(&row_new, N);

    for (int i = 0; i < nGrps; i++) {
        if (!bitvector_is_set(found_groups, i)) continue;
        bitvector_clear(&row_found);
        dm_row_union(&row_found, GBgetDMInfoRead(model), i);

        for(int j = 0; j < nGrps; j++) {
            if (bitvector_is_set(found_groups, j)) continue;
            bitvector_clear(&row_new);
            dm_row_union(&row_new, GBgetDMInfoMayWrite(model), j);
            bitvector_intersect(&row_new, &row_found);

            if (!bitvector_is_empty(&row_new)) {
                *group=j;
                bitvector_free(&row_found);
                bitvector_free(&row_new);
                return 1;
            }
        }
    }

    bitvector_free(&row_found);
    bitvector_free(&row_new);
    return 0;
}

static int
establish_group_order(int *group_order, int *initial_count)
{
    int group_total = 0;
    bitvector_t found_groups;

    bitvector_create(&found_groups, nGrps);

    int groups[nGrps];
    const int n = GBgroupsOfEdge(model, act_label, act_index, groups);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            Warning(info, "Found \"%s\" potentially in group %d", act_detect, groups[i]);
            group_order[group_total] = groups[i];
            group_total++;
            bitvector_set(&found_groups, groups[i]);
        }
    } else Abort("No group will ever produce action \"%s\"", act_detect);

    *initial_count = group_total;

    int new_group;

    while(find_overlapping_group(&found_groups, &new_group)){
        group_order[group_total] = new_group;
        group_total++;
        bitvector_set(&found_groups, new_group);
    }

    return group_total;
}

void
directed(sat_proc_t sat_proc, reach_proc_t reach_proc, vset_t visited,
         char *etf_output)
{
    int *group_order = RTmalloc(nGrps * sizeof(int));
    int initial_count, total_count;
    bitvector_t reach_groups;

    if (act_detect == NULL)
        Abort("Guided forward search requires action");

    chunk c = chunk_str(act_detect);
    act_index = pins_chunk_put (model, action_typeno, c); // now only used for guidance heuristics

    total_count = establish_group_order(group_order, &initial_count);

    if (total_count == 0)
        Abort("Action %s does not occur", act_detect);

    bitvector_create(&reach_groups, nGrps);
    for (int i = 0; i < initial_count; i++)
        bitvector_set(&reach_groups, group_order[i]);

    long eg_count = 0;
    long next_count = 0;
    long guard_count = 0;

    // Assumption: reach_proc does not return in case action is found
    Warning(info, "Searching for action using initial groups");
    sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);

    for (int i = initial_count; i < total_count; i++) {
        Warning(info, "Extending action search with group %d", group_order[i]);
        bitvector_set(&reach_groups, group_order[i]);
        sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);
    }

    if (etf_output != NULL || dlk_detect) {
        Warning(info, "Continuing for etf output or deadlock detection");

        for(int i = 0; i < nGrps; i++)
            bitvector_set(&reach_groups, i);

        sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);
    }

    Warning(info, "Exploration took %ld group checks, %ld next state calls and %ld guard evaluation calls",
            eg_count, next_count, guard_count);
    bitvector_free(&reach_groups);
}
