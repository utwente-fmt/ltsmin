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

#ifdef HAVE_SYLVAN
/*
 * Add parallel operations
 */
// join
#define vset_join_par(dst, left, right) SPAWN(vset_join_par, dst, left, right)
VOID_TASK_3(vset_join_par, vset_t, dst, vset_t, left, vset_t, right) { vset_join(dst, left, right); }

// minus
#define vset_minus_par(dst, src) SPAWN(vset_minus_par, dst, src)
VOID_TASK_2(vset_minus_par, vset_t, dst, vset_t, src) { vset_minus(dst, src); }
#endif

#ifdef HAVE_SYLVAN
#define reach_bfs_reduce(dummy) CALL(reach_bfs_reduce, dummy)
VOID_TASK_1(reach_bfs_reduce, struct reach_red_s *, dummy)
#else
static void reach_bfs_reduce(struct reach_red_s *dummy)
#endif
{
    if (dummy->index >= 0) { // base case
        // check if no states which satisfy other guards
        if (vset_is_empty(dummy->true_container)) return;
        // reduce states in transition group
        int guard = GBgetGuard(model, dummy->group)->guard[dummy->index];
        if (!no_soundness_check) {
            vset_copy(dummy->false_container, dummy->true_container);
            vset_join(dummy->false_container, dummy->false_container, label_false[guard]);
        }
        vset_join(dummy->true_container, dummy->true_container, label_true[guard]);
    } else { // recursive case
        // send set of states downstream
        vset_copy(dummy->left->true_container, dummy->true_container);
        vset_copy(dummy->right->true_container, dummy->true_container);

        // sequentially go left/right (not parallel)
        reach_bfs_reduce(dummy->left);
        reach_bfs_reduce(dummy->right);

        // we intersect every leaf, since we want to reduce the states in the group
        vset_copy(dummy->true_container, dummy->left->true_container);
        vset_intersect(dummy->true_container, dummy->right->true_container);

        if (!no_soundness_check) {

            // compute maybe set
            vset_copy(dummy->left_maybe, dummy->left->false_container);
            vset_intersect(dummy->left_maybe, dummy->left->true_container);
            if (MAYBE_AND_FALSE_IS_FALSE) vset_minus(dummy->left_maybe, dummy->right->false_container);

            vset_copy(dummy->right_maybe, dummy->right->false_container);
            vset_intersect(dummy->right_maybe, dummy->right->true_container);
            vset_minus(dummy->right_maybe, dummy->left->false_container);

            vset_union(dummy->left_maybe, dummy->right_maybe);
            vset_clear(dummy->right_maybe);

            // compute false set
            vset_copy(dummy->false_container, dummy->left->false_container);
            vset_union(dummy->false_container, dummy->right->false_container);
            vset_union(dummy->false_container, dummy->left_maybe);
            vset_clear(dummy->left->false_container);
            vset_clear(dummy->right->false_container);

            // compute true set
            vset_union(dummy->true_container, dummy->left_maybe);

            vset_clear(dummy->left_maybe);
        }

        vset_clear(dummy->left->true_container);
        vset_clear(dummy->right->true_container);
    }
}

#ifdef HAVE_SYLVAN
#define reach_bfs_next(dummy, reach_groups, maybe) CALL(reach_bfs_next, dummy, reach_groups, maybe)
VOID_TASK_3(reach_bfs_next, struct reach_s *, dummy, bitvector_t *, reach_groups, vset_t*, maybe)
#else
static void reach_bfs_next(struct reach_s *dummy, bitvector_t *reach_groups, vset_t *maybe)
#endif
{
    if (dummy->index >= 0) {
        if (!bitvector_is_set(reach_groups, dummy->index)) {
            if (dummy->ancestors != NULL) vset_clear(dummy->ancestors);
            dummy->next_count = 0;
            dummy->eg_count=0;
            return;
        }

        // Check if in current class...
        if (inhibit_matrix != NULL) {
            if (!dm_is_set(class_matrix, dummy->class, dummy->index)) {
                if (dummy->ancestors != NULL) vset_clear(dummy->ancestors);
                dummy->next_count = 0;
                dummy->eg_count=0;
                return;
            }
        }

        if (dummy->red != NULL) { // we have guard-splitting; reduce the set
            // Reduce current level
            vset_copy(dummy->red->true_container, dummy->container);
            reach_bfs_reduce(dummy->red);

            if (vset_is_empty(dummy->red->true_container)) {
                dummy->next_count = 0;
                dummy->eg_count=0;
                return;
            }

            // soundness check
            if (!no_soundness_check) {
                vset_copy(maybe[dummy->index], dummy->red->true_container);
                vset_intersect(maybe[dummy->index], dummy->red->false_container);

                // we don't abort immediately so that other threads can finish cleanly.
                if (!vset_is_empty(maybe[dummy->index])) dummy->unsound_group = dummy->index;
                vset_clear(maybe[dummy->index]);
                vset_clear(dummy->red->false_container);
            }

            vset_copy(dummy->container, dummy->red->true_container);
            vset_clear(dummy->red->true_container);
        }

        // Expand transition relations
        expand_group_next(dummy->index, dummy->container);
        dummy->eg_count = 1;

        // Compute successor states
        vset_next_fn(dummy->container, dummy->container, group_next[dummy->index]);
        dummy->next_count = 1;

        // Compute ancestor states
        if (dummy->ancestors != NULL) {
            vset_prev(dummy->ancestors, dummy->container, group_next[dummy->index], dummy->ancestors);
            reduce(dummy->index, dummy->ancestors);
        }

        // Remove ancestor states from potential deadlock states
        if (dummy->deadlocks != NULL) vset_minus(dummy->deadlocks, dummy->ancestors);

        // If we don't need ancestor states, clear the set
        if (dummy->ancestors != NULL && inhibit_matrix == NULL) vset_clear(dummy->ancestors);
    } else {
        // Send set of states downstream
        vset_copy(dummy->left->container, dummy->container);
        vset_copy(dummy->right->container, dummy->container);

        if (dummy->deadlocks != NULL) {
            vset_copy(dummy->left->deadlocks, dummy->deadlocks);
            vset_copy(dummy->right->deadlocks, dummy->deadlocks);
        }

        if (dummy->ancestors != NULL) {
            vset_copy(dummy->left->ancestors, dummy->ancestors);
            vset_copy(dummy->right->ancestors, dummy->ancestors);
        }

        dummy->left->class = dummy->class;
        dummy->right->class = dummy->class;

        // Sequentially go left/right (BFS, not PAR)
        reach_bfs_next(dummy->left, reach_groups, maybe);
        reach_bfs_next(dummy->right, reach_groups, maybe);

        // Perform union
        vset_copy(dummy->container, dummy->left->container);
        vset_union(dummy->container, dummy->right->container);

        // Clear
        vset_clear(dummy->left->container);
        vset_clear(dummy->right->container);

        // Intersect deadlocks
        if (dummy->deadlocks != NULL) {
            vset_copy(dummy->deadlocks, dummy->left->deadlocks);
            vset_intersect(dummy->deadlocks, dummy->right->deadlocks);
            vset_clear(dummy->left->deadlocks);
            vset_clear(dummy->right->deadlocks);
        }

        // Merge ancestors
        if (inhibit_matrix != NULL) {
            vset_copy(dummy->ancestors, dummy->left->ancestors);
            vset_union(dummy->ancestors, dummy->right->ancestors);
            vset_clear(dummy->left->ancestors);
            vset_clear(dummy->right->ancestors);
        }

        dummy->next_count = dummy->left->next_count + dummy->right->next_count;
        dummy->eg_count = dummy->left->eg_count + dummy->right->eg_count;

        if (dummy->left->unsound_group > -1) dummy->unsound_group = dummy->left->unsound_group;
        if (dummy->right->unsound_group > -1) dummy->unsound_group = dummy->right->unsound_group;
    }
}

/**
 * Parallel reachability implementation
 */
#ifdef HAVE_SYLVAN
VOID_TASK_3(compute_left_maybe, vset_t, left_maybe, vset_t, left_true, vset_t, right_false)
{
    vset_intersect(left_maybe, left_true);
    if (MAYBE_AND_FALSE_IS_FALSE) vset_minus(left_maybe, right_false);
}

VOID_TASK_3(compute_right_maybe, vset_t, right_maybe, vset_t, right_true, vset_t, left_false)
{
    vset_intersect(right_maybe, right_true);
    vset_minus(right_maybe, left_false);
}

VOID_TASK_3(compute_false, vset_t, false_c, vset_t, right_false, vset_t, maybe) {
    vset_union(false_c, right_false);
    vset_union(false_c, maybe);
}

VOID_TASK_1(reach_par_reduce, struct reach_red_s *, dummy)
{
    if (dummy->index >= 0) { // base case
        // check if no states which satisfy other guards
        if (vset_is_empty(dummy->true_container)) return;
        int guard = GBgetGuard(model, dummy->group)->guard[dummy->index];
        if (!no_soundness_check) {
            vset_copy(dummy->false_container, dummy->true_container);
            vset_join_par(dummy->false_container, dummy->false_container, label_false[guard]);
        }
        vset_join_par(dummy->true_container, dummy->true_container, label_true[guard]);
        SYNC(vset_join_par);
        if (!no_soundness_check) SYNC(vset_join_par);
    } else { //recursive case
        // send set of states downstream
        vset_copy(dummy->left->true_container, dummy->true_container);
        vset_copy(dummy->right->true_container, dummy->true_container);

        // go left/right in parallel
        SPAWN(reach_par_reduce, dummy->left);
        SPAWN(reach_par_reduce, dummy->right);
        SYNC(reach_par_reduce);
        SYNC(reach_par_reduce);

        if (!no_soundness_check) {
            // compute maybe set
            vset_copy(dummy->left_maybe, dummy->left->false_container);
            SPAWN(compute_left_maybe, dummy->left_maybe, dummy->left->true_container, dummy->right->false_container);

            vset_copy(dummy->right_maybe, dummy->right->false_container);
            SPAWN(compute_right_maybe, dummy->right_maybe, dummy->right->true_container, dummy->left->false_container);

            SYNC(compute_right_maybe);
            SYNC(compute_left_maybe);

            // compute maybe
            vset_union(dummy->left_maybe, dummy->right_maybe);
            vset_clear(dummy->right_maybe);

            // compute false set
            vset_copy(dummy->false_container, dummy->left->false_container);
            SPAWN(compute_false, dummy->false_container, dummy->right->false_container, dummy->left_maybe);

            // compute true set
            // we intersect every leaf, since we want to reduce the states in the group
            vset_copy(dummy->true_container, dummy->left->true_container);
            vset_intersect(dummy->true_container, dummy->right->true_container);
            vset_union(dummy->true_container, dummy->left_maybe);
            vset_clear(dummy->left_maybe);

            SYNC(compute_false);
            vset_clear(dummy->left->false_container);
            vset_clear(dummy->right->false_container);
        } else {
            // we intersect every leaf, since we want to reduce the states in the group
            vset_copy(dummy->true_container, dummy->left->true_container);
            vset_intersect(dummy->true_container, dummy->right->true_container);
        }

        vset_clear(dummy->left->true_container);
        vset_clear(dummy->right->true_container);
    }
}

VOID_TASK_3(reach_par_next, struct reach_s *, dummy, bitvector_t *, reach_groups, vset_t*, maybe)
{
    if (dummy->index >= 0) {
        if (!bitvector_is_set(reach_groups, dummy->index)) {
            if (dummy->ancestors != NULL) vset_clear(dummy->ancestors);
            dummy->next_count = 0;
            dummy->eg_count=0;
            return;
        }

        // Check if in current class...
        if (inhibit_matrix != NULL) {
            if (!dm_is_set(class_matrix, dummy->class, dummy->index)) {
                if (dummy->ancestors != NULL) vset_clear(dummy->ancestors);
                dummy->next_count = 0;
                dummy->eg_count=0;
                return;
            }
        }

        if (dummy->red != NULL) { // we have guard-splitting; reduce the set
            // Reduce current level
            vset_copy(dummy->red->true_container, dummy->container);
            CALL(reach_par_reduce, dummy->red);

            if (vset_is_empty(dummy->red->true_container)) {
                dummy->next_count = 0;
                dummy->eg_count=0;
                return;
            }

            // soundness check
            if (!no_soundness_check) {
                vset_copy(maybe[dummy->index], dummy->red->true_container);
                vset_intersect(maybe[dummy->index], dummy->red->false_container);

                // we don't abort immediately so that other threads can finish cleanly.
                if (!vset_is_empty(maybe[dummy->index])) dummy->unsound_group = dummy->index;
                vset_clear(maybe[dummy->index]);
                vset_clear(dummy->red->false_container);
            }

            vset_copy(dummy->container, dummy->red->true_container);
            vset_clear(dummy->red->true_container);
        }

        // Expand transition relations
        expand_group_next(dummy->index, dummy->container);
        dummy->eg_count = 1;

        // Compute successor states
        vset_next_fn(dummy->container, dummy->container, group_next[dummy->index]);
        dummy->next_count = 1;

        // Compute ancestor states
        if (dummy->ancestors != NULL) {
            vset_prev(dummy->ancestors, dummy->container, group_next[dummy->index], dummy->ancestors);
            reduce(dummy->index, dummy->ancestors);
        }

        // Remove ancestor states from potential deadlock states
        if (dummy->deadlocks != NULL) vset_minus(dummy->deadlocks, dummy->ancestors);

        // If we don't need ancestor states, clear the set
        if (dummy->ancestors != NULL && inhibit_matrix == NULL) vset_clear(dummy->ancestors);
    } else {
        // Send set of states downstream
        vset_copy(dummy->left->container, dummy->container);
        vset_copy(dummy->right->container, dummy->container);

        if (dummy->deadlocks != NULL) {
            vset_copy(dummy->left->deadlocks, dummy->deadlocks);
            vset_copy(dummy->right->deadlocks, dummy->deadlocks);
        }

        if (dummy->ancestors != NULL) {
            vset_copy(dummy->left->ancestors, dummy->ancestors);
            vset_copy(dummy->right->ancestors, dummy->ancestors);
        }

        dummy->left->class = dummy->class;
        dummy->right->class = dummy->class;

        // Go left/right in parallel
        SPAWN(reach_par_next, dummy->left, reach_groups, maybe);
        SPAWN(reach_par_next, dummy->right, reach_groups, maybe);
        SYNC(reach_par_next);
        SYNC(reach_par_next);

        // Perform union
        vset_copy(dummy->container, dummy->left->container);
        vset_union(dummy->container, dummy->right->container);

        // Clear
        vset_clear(dummy->left->container);
        vset_clear(dummy->right->container);

        // Intersect deadlocks
        if (dummy->deadlocks != NULL) {
            vset_copy(dummy->deadlocks, dummy->left->deadlocks);
            vset_intersect(dummy->deadlocks, dummy->right->deadlocks);
            vset_clear(dummy->left->deadlocks);
            vset_clear(dummy->right->deadlocks);
        }

        // Merge ancestors
        if (inhibit_matrix != NULL) {
            vset_copy(dummy->ancestors, dummy->left->ancestors);
            vset_union(dummy->ancestors, dummy->right->ancestors);
            vset_clear(dummy->left->ancestors);
            vset_clear(dummy->right->ancestors);
        }

        dummy->next_count = dummy->left->next_count + dummy->right->next_count;
        dummy->eg_count = dummy->left->eg_count + dummy->right->eg_count;

        if (dummy->left->unsound_group > -1) dummy->unsound_group = dummy->left->unsound_group;
        if (dummy->right->unsound_group > -1) dummy->unsound_group = dummy->right->unsound_group;
    }
}

void
reach_par(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
          long *eg_count, long *next_count, long *guard_count)
{
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);
    //if (save_sat_levels) vset_minus(current_level, visited_old); // ???

    vset_t maybe[nGrps];
    if (!no_soundness_check) {
        for (int i = 0; i < nGrps; i++) {
            maybe[i] = vset_create(domain, -1, NULL);
        }
    }

    LACE_ME;
    struct reach_s *root = reach_prepare(0, nGrps);

    int level = 0;
    while (!vset_equal(visited, old_vis)) {
        vset_copy(old_vis, visited);

        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(NULL, visited, level);
        level++;

        if (dlk_detect) vset_copy(root->deadlocks, visited);

        if (inhibit_matrix != NULL) {
            // for every class, compute successors, add to next_level
            for (int c=0; c<inhibit_class_count; c++) {
                // set container to current level minus enabled transitions from all inhibiting classes
                vset_copy(root->container, visited);
                for (int i=0; i<c; i++) if (dm_is_set(inhibit_matrix,i,c)) vset_minus(root->container, class_enabled[i]);
                // evaluate all guards
                learn_guards_par(root->container, guard_count);
                // set ancestors to container
                vset_copy(root->ancestors, root->container);
                // carry over root->deadlocks from previous iteration
                // set class and call next function
                root->class = c;
                CALL(reach_par_next, root, reach_groups, maybe);
                reach_stop(root);
                if (!no_soundness_check && PINS_USE_GUARDS) {
                    // For the current level the spec is sound.
                    // This means that every maybe is actually false.
                    // We thus remove all maybe's
                    for (int g = 0; g < nGuards; g++) vset_minus_par(label_true[g], label_false[g]);
                    for (int g = nGuards-1; g >= 0; g--) SYNC(vset_minus_par);
                }
                // update counters
                *next_count += root->next_count;
                *eg_count += root->eg_count;
                // add enabled transitions to class_enabled
                vset_union(class_enabled[c], root->ancestors);
                vset_clear(root->ancestors);
                // remove visited states
                vset_minus(root->container, visited);
                // add new states to next_level
                vset_union(next_level, root->container);
                vset_clear(root->container);
            }
            vset_union(visited, next_level);
            vset_clear(next_level);
        } else {
            // set container to current level
            vset_copy(root->container, visited);
            // evaluate all guards
            learn_guards_par(root->container, guard_count);
            // set ancestors to container
            if (root->ancestors != NULL) vset_copy(root->ancestors, visited);
            // call next function
            CALL(reach_par_next, root, reach_groups, maybe);
            reach_stop(root);
            if (!no_soundness_check && PINS_USE_GUARDS) {
                // For the current level the spec is sound.
                // This means that every maybe is actually false.
                // We thus remove all maybe's
                for (int g = 0; g < nGuards; g++) vset_minus_par(label_true[g], label_false[g]);
                for (int g = nGuards-1; g >= 0; g--) SYNC(vset_minus_par);
            }
            // update counters
            *next_count += root->next_count;
            *eg_count += root->eg_count;
            // add successors to visited set
            vset_union(visited, root->container);
        }

        if (sat_strategy == NO_SAT) check_invariants(visited, level);

        if (dlk_detect) {
            deadlock_check(root->deadlocks, reach_groups);
            vset_clear(root->deadlocks);
        }

        vset_reorder(domain);
    }

    reach_destroy(root);
    vset_destroy(old_vis);
    vset_destroy(next_level);

    if (!no_soundness_check) {
        for(int i = 0; i < nGrps; i++) {
            vset_destroy(maybe[i]);
        }
    }

    return;
    (void)visited_old;
}

void
reach_par_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
               long *eg_count, long *next_count, long *guard_count)
{
    vset_t current_level = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);

    vset_copy(current_level, visited);
    if (save_sat_levels) vset_minus(current_level, visited_old);

    vset_t maybe[nGrps];
    if (!no_soundness_check) {
        for (int i = 0; i < nGrps; i++) {
            maybe[i] = vset_create(domain, -1, NULL);
        }
    }

    LACE_ME;
    struct reach_s *root = reach_prepare(0, nGrps);

    int level = 0;
    while (!vset_is_empty(current_level)) {
        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(current_level, visited, level);
        level++;

        if (dlk_detect) vset_copy(root->deadlocks, current_level);

        if (inhibit_matrix != NULL) {
            // class_enabled holds all states in the current level with transitions in class c
            // only use current_level, so clear class_enabled...
            for (int c=0; c<inhibit_class_count; c++) vset_clear(class_enabled[c]);

            // for every class, compute successors, add to next_level
            for (int c=0; c<inhibit_class_count; c++) {
                // set container to current level minus enabled transitions from all inhibiting classes
                vset_copy(root->container, current_level);
                for (int i=0; i<c; i++) if (dm_is_set(inhibit_matrix,i,c)) vset_minus(root->container, class_enabled[i]);
                // evaluate all guards
                learn_guards_par(root->container, guard_count);
                // set ancestors to container
                vset_copy(root->ancestors, root->container);
                // carry over root->deadlocks from previous iteration
                // set class and call next function
                root->class = c;
                CALL(reach_par_next, root, reach_groups, maybe);
                reach_stop(root);
                if (!no_soundness_check && PINS_USE_GUARDS) {
                    // For the current level the spec is sound.
                    // This means that every maybe is actually false.
                    // We thus remove all maybe's
                    for (int g = 0; g < nGuards; g++) vset_minus_par(label_true[g], label_false[g]);
                    for (int g = nGuards-1; g >= 0; g--) SYNC(vset_minus_par);
                }
                // update counters
                *next_count += root->next_count;
                *eg_count += root->eg_count;
                // add enabled transitions to class_enabled
                vset_copy(class_enabled[c], root->ancestors);
                vset_clear(root->ancestors);
                // remove visited states
                vset_minus(root->container, visited);
                // add new states to next_level
                vset_union(next_level, root->container);
                vset_clear(root->container);
            }
        } else {
            // set container to current level
            vset_copy(root->container, current_level);
            // evaluate all guards
            learn_guards_par(root->container, guard_count);
            // set ancestors to container
            if (root->ancestors != NULL) vset_copy(root->ancestors, current_level);
            // call next function
            CALL(reach_par_next, root, reach_groups, maybe);
            reach_stop(root);
            if (!no_soundness_check && PINS_USE_GUARDS) {
                // For the current level the spec is sound.
                // This means that every maybe is actually false.
                // We thus remove all maybe's
                for (int g = 0; g < nGuards; g++) vset_minus_par(label_true[g], label_false[g]);
                for (int g = nGuards-1; g >= 0; g--) SYNC(vset_minus_par);
            }
            // update counters
            *next_count += root->next_count;
            *eg_count += root->eg_count;
            // set next_level to new states (root->container - visited states)
            vset_copy(next_level, root->container);
            vset_clear(root->container);
            vset_minus(next_level, visited);
        }

        if (sat_strategy == NO_SAT) check_invariants(next_level, level);

        // set current_level to next_level
        vset_copy(current_level, next_level);
        vset_clear(next_level);

        if (dlk_detect) {
            deadlock_check(root->deadlocks, reach_groups);
            vset_clear(root->deadlocks);
        }

        vset_union(visited, current_level);
        vset_reorder(domain);
    }

    reach_destroy(root);
    vset_destroy(current_level);
    vset_destroy(next_level);

    if (!no_soundness_check) {
        for(int i = 0; i < nGrps; i++) {
            vset_destroy(maybe[i]);
        }
    }
}
#endif

void
reach_bfs_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
               long *eg_count, long *next_count, long *guard_count)
{
    vset_t current_level = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);

    vset_copy(current_level, visited);
    if (save_sat_levels) vset_minus(current_level, visited_old);

    vset_t maybe[nGrps];
    if (!no_soundness_check) {
        for (int i = 0; i < nGrps; i++) {
            maybe[i] = vset_create(domain, -1, NULL);
        }
    }

    LACE_ME;
    struct reach_s *root = reach_prepare(0, nGrps);

    int level = 0;
    while (!vset_is_empty(current_level)) {
        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(current_level, visited, level);
        level++;

        if (dlk_detect) vset_copy(root->deadlocks, current_level);

        if (inhibit_matrix != NULL) {
            // class_enabled holds all states in the current level with transitions in class c
            // only use current_level, so clear class_enabled...
            for (int c=0; c<inhibit_class_count; c++) vset_clear(class_enabled[c]);

            // for every class, compute successors, add to next_level
            for (int c=0; c<inhibit_class_count; c++) {
                // set container to current level minus enabled transitions from all inhibiting classes
                vset_copy(root->container, current_level);
                for (int i=0; i<c; i++) if (dm_is_set(inhibit_matrix,i,c)) vset_minus(root->container, class_enabled[i]);
                // evaluate all guards
                learn_guards(root->container, guard_count);
                // set ancestors to container
                vset_copy(root->ancestors, root->container);
                // carry over root->deadlocks from previous iteration
                // set class and call next function
                root->class = c;
                reach_bfs_next(root, reach_groups, maybe);
                reach_stop(root);
                if (!no_soundness_check && PINS_USE_GUARDS) {
                    // For the current level the spec is sound.
                    // This means that every maybe is actually false.
                    // We thus remove all maybe's
                    for (int g = 0; g < nGuards; g++) {
                        vset_minus(label_true[g], label_false[g]);
                    }
                }
                // update counters
                *next_count += root->next_count;
                *eg_count += root->eg_count;
                // add enabled transitions to class_enabled
                vset_copy(class_enabled[c], root->ancestors);
                vset_clear(root->ancestors);
                // remove visited states
                vset_minus(root->container, visited);
                // add new states to next_level
                vset_union(next_level, root->container);
                vset_clear(root->container);
            }
        } else {
            // set container to current level
            vset_copy(root->container, current_level);
            // evaluate all guards
            learn_guards(root->container, guard_count);
            // set ancestors to container
            if (root->ancestors != NULL) vset_copy(root->ancestors, current_level);
            // call next function
            reach_bfs_next(root, reach_groups, maybe);
            reach_stop(root);
            if (!no_soundness_check && PINS_USE_GUARDS) {
                // For the current level the spec is sound.
                // This means that every maybe is actually false.
                // We thus remove all maybe's
                for (int g = 0; g < nGuards; g++) {
                    vset_minus(label_true[g], label_false[g]);
                }
            }
            // update counters
            *next_count += root->next_count;
            *eg_count += root->eg_count;
            // set next_level to new states (root->container - visited states)
            vset_copy(next_level, root->container);
            vset_clear(root->container);
            vset_minus(next_level, visited);
        }

        if (sat_strategy == NO_SAT) check_invariants(next_level, level);

        // set current_level to next_level
        vset_copy(current_level, next_level);
        vset_clear(next_level);

        if (dlk_detect) {
            deadlock_check(root->deadlocks, reach_groups);
            vset_clear(root->deadlocks);
        }

        vset_union(visited, current_level);
        vset_reorder(domain);
    }

    reach_destroy(root);
    vset_destroy(current_level);
    vset_destroy(next_level);

    if (!no_soundness_check) {
        for(int i = 0; i < nGrps; i++) {
            vset_destroy(maybe[i]);
        }
    }
}

void
reach_bfs(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
          long *eg_count, long *next_count, long *guard_count)
{
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);
    //if (save_sat_levels) vset_minus(current_level, visited_old); // ???

    vset_t maybe[nGrps];
    if (!no_soundness_check) {
        for (int i = 0; i < nGrps; i++) {
            maybe[i] = vset_create(domain, -1, NULL);
        }
    }

    LACE_ME;
    struct reach_s *root = reach_prepare(0, nGrps);

    int level = 0;
    while (!vset_equal(visited, old_vis)) {
        vset_copy(old_vis, visited);

        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(NULL, visited, level);
        level++;

        if (dlk_detect) vset_copy(root->deadlocks, visited);

        if (inhibit_matrix != NULL) {
            // for every class, compute successors, add to next_level
            for (int c=0; c<inhibit_class_count; c++) {
                // set container to current level minus enabled transitions from all inhibiting classes
                vset_copy(root->container, visited);
                for (int i=0; i<c; i++) if (dm_is_set(inhibit_matrix,i,c)) vset_minus(root->container, class_enabled[i]);
                // evaluate all guards
                learn_guards(root->container, guard_count);
                // set ancestors to container
                vset_copy(root->ancestors, root->container);
                // carry over root->deadlocks from previous iteration
                // set class and call next function
                root->class = c;
                reach_bfs_next(root, reach_groups, maybe);
                reach_stop(root);
                if (!no_soundness_check && PINS_USE_GUARDS) {
                    // For the current level the spec is sound.
                    // This means that every maybe is actually false.
                    // We thus remove all maybe's
                    for (int g = 0; g < nGuards; g++) {
                        vset_minus(label_true[g], label_false[g]);
                    }
                }
                // update counters
                *next_count += root->next_count;
                *eg_count += root->eg_count;
                // add enabled transitions to class_enabled
                vset_union(class_enabled[c], root->ancestors);
                vset_clear(root->ancestors);
                // remove visited states
                vset_minus(root->container, visited);
                // add new states to next_level
                vset_union(next_level, root->container);
                vset_clear(root->container);
            }
            vset_union(visited, next_level);
            vset_clear(next_level);
        } else {
            // set container to current level
            vset_copy(root->container, visited);
            // evaluate all guards
            learn_guards(root->container, guard_count);
            // set ancestors to container
            if (root->ancestors != NULL) vset_copy(root->ancestors, visited);
            // call next function
            reach_bfs_next(root, reach_groups, maybe);
            reach_stop(root);
            if (!no_soundness_check && PINS_USE_GUARDS) {
                // For the current level the spec is sound.
                // This means that every maybe is actually false.
                // We thus remove all maybe's
                for (int g = 0; g < nGuards; g++) {
                    vset_minus(label_true[g], label_false[g]);
                }
            }
            // update counters
            *next_count += root->next_count;
            *eg_count += root->eg_count;
            // add successors to visited set
            vset_union(visited, root->container);
        }

        if (sat_strategy == NO_SAT) check_invariants(visited, level);

        if (dlk_detect) {
            deadlock_check(root->deadlocks, reach_groups);
            vset_clear(root->deadlocks);
        }

        vset_reorder(domain);
    }

    reach_destroy(root);
    vset_destroy(old_vis);
    vset_destroy(next_level);

    if (!no_soundness_check) {
        for(int i = 0; i < nGrps; i++) {
            vset_destroy(maybe[i]);
        }
    }

    return;
    (void)visited_old;
}
