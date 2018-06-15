#include <hre/config.h>

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
#include <pins2lts-sym/alg/sat.h>
#include <pins2lts-sym/aux/output.h>
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
reach_chain_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                 long *eg_count, long *next_count, long *guard_count)
{
    int level = 0;
    vset_t new_states = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t new_reduced = vset_create(domain, -1, NULL);

    vset_t guard_maybe[nGuards];
    vset_t tmp = NULL;
    vset_t false_states = NULL;
    vset_t maybe_states = NULL;
    if (!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            guard_maybe[i] = vset_create(domain, l_projs[i]->count, l_projs[i]->data);
        }
        false_states = vset_create(domain, -1, NULL);
        maybe_states = vset_create(domain, -1, NULL);
        tmp = vset_create(domain, -1, NULL);
    }

    vset_copy(new_states, visited);
    if (save_sat_levels) vset_minus(new_states, visited_old);

    LACE_ME;
    while (!vset_is_empty(new_states)) {
        stats_and_progress_report(new_states, visited, level);
        level++;
        if (dlk_detect) vset_copy(deadlocks, new_states);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups, i)) continue;
            if (trc_output != NULL) save_level(visited);

            vset_copy(new_reduced, new_states);
            learn_guards_reduce(new_reduced, i, guard_count, guard_maybe, false_states, maybe_states, tmp);

            if (!vset_is_empty(new_reduced)) {
                expand_group_next(i, new_reduced);
                reach_chain_stop();
                (*eg_count)++;
                (*next_count)++;
                vset_next_fn(temp, new_reduced, group_next[i]);
                vset_clear(new_reduced);
                if (dlk_detect) {
                    vset_prev(dlk_temp, temp, group_next[i], deadlocks);
                    reduce(i, dlk_temp);
                    vset_minus(deadlocks, dlk_temp);
                    vset_clear(dlk_temp);
                }

                vset_minus(temp, visited);
                vset_union(new_states, temp);
                vset_clear(temp);
            }
        }

        if (sat_strategy == NO_SAT) check_invariants(new_states, -1);

        // no deadlocks in old new_states
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);

        vset_zip(visited, new_states);
        vset_reorder(domain);
    }

    vset_destroy(new_states);
    vset_destroy(temp);
    vset_destroy(new_reduced);

    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
    if(!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            vset_destroy(guard_maybe[i]);
        }
        vset_destroy(tmp);
        vset_destroy(false_states);
        vset_destroy(maybe_states);
    }
}

void
reach_chain(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
            long *eg_count, long *next_count, long *guard_count)
{
    (void)visited_old;

    int level = 0;
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t new_reduced = vset_create(domain, -1, NULL);

    vset_t guard_maybe[nGuards];
    vset_t tmp = NULL;
    vset_t false_states = NULL;
    vset_t maybe_states = NULL;
    if (!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            guard_maybe[i] = vset_create(domain, l_projs[i]->count, l_projs[i]->data);
        }
        false_states = vset_create(domain, -1, NULL);
        maybe_states = vset_create(domain, -1, NULL);
        tmp = vset_create(domain, -1, NULL);
    }

    LACE_ME;
    while (!vset_equal(visited, old_vis)) {
        vset_copy(old_vis, visited);
        stats_and_progress_report(NULL, visited, level);
        level++;
        if (dlk_detect) vset_copy(deadlocks, visited);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups, i)) continue;
            if (trc_output != NULL) save_level(visited);
            vset_copy(new_reduced, visited);
            learn_guards_reduce(new_reduced, i, guard_count, guard_maybe, false_states, maybe_states, tmp);
            expand_group_next(i, new_reduced);
            reach_chain_stop();
            (*eg_count)++;
            (*next_count)++;
            vset_next_fn(temp, new_reduced, group_next[i]);
            vset_clear(new_reduced);
            vset_union(visited, temp);
            if (dlk_detect) {
                vset_prev(dlk_temp, temp, group_next[i],deadlocks);
                reduce(i, dlk_temp);
                vset_minus(deadlocks, dlk_temp);
                vset_clear(dlk_temp);
            }
            vset_clear(temp);
        }

        if (sat_strategy == NO_SAT) check_invariants(visited, -1);

        // no deadlocks in old_vis
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);
        vset_reorder(domain);
    }

    vset_destroy(old_vis);
    vset_destroy(temp);
    vset_destroy(new_reduced);

    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
    if(!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            vset_destroy(guard_maybe[i]);
        }
        vset_destroy(tmp);
        vset_destroy(false_states);
        vset_destroy(maybe_states);
    }
}
