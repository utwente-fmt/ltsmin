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

static inline vset_t
empty ()
{
    return vset_create (domain, -1, NULL);
}

static inline void
grow_levels(int new_levels)
{
    if (global_level == max_levels) {
        max_levels += new_levels;
        levels = RTrealloc(levels, max_levels * sizeof(vset_t));

        for(int i = global_level; i < max_levels; i++)
            levels[i] = vset_create(domain, -1, NULL);
    }
}

void
save_level(vset_t visited)
{
    grow_levels(1024);
    vset_copy(levels[global_level], visited);
    global_level++;
}

static int
seen_actions_test (int idx)
{
    int size = BVLLget_size(seen_actions);
    if (idx >= size - 1) {
        if (BVLLtry_set_sat_bit(seen_actions, size-1, 0)) {
            Warning(info, "Warning: Action cache full. Caching currently limited to %d labels.", size-1);
        }
        return 1;
    }
    return BVLLtry_set_sat_bit(seen_actions, idx, 0);
}

typedef struct trace_action {
    int *dst;
    int *cpy;
    char *action;
} trace_action_t;

typedef struct group_add_info {
    vrel_t rel; // target relation
    vset_t set; // source set
    int group; // which transition group
    int *src; // state vector
    int trace_count; // number of actions to trace after next-state call
    struct trace_action *trace_action;
} group_add_info_t;

static void
group_add(void *context, transition_info_t *ti, int *dst, int *cpy)
{
    group_add_info_t *ctx = (group_add_info_t*)context;

    int act_index = 0;
    if (ti->labels != NULL && act_label != -1) {
        // add with action label
        act_index = ti->labels[act_label];
        vrel_add_act(ctx->rel, ctx->src, dst, cpy, act_index);
    } else {
        // add without action label
        vrel_add_cpy(ctx->rel, ctx->src, dst, cpy);
    }

    if (act_detect && (no_exit || ErrorActions == 0)) {
        // note: in theory, it might be possible that ti->labels == NULL,
        // even though we are using action detection and act_label != -1,
        // which was checked earlier in init_action_detection().
        // this indicates an incorrect implementation of the pins model
        if (ti->labels == NULL) {
            Abort("ti->labels is null");
        }
        if (seen_actions_test(act_index)) { // is this the first time we encounter this action?
            char *action=pins_chunk_get (model,action_typeno,act_index).data;

            if (strncmp(act_detect,action,strlen(act_detect))==0)  {
                Warning(info, "found action: %s", action);

                if (trc_output) {

                    size_t vec_bytes = sizeof(int[w_projs[ctx->group].len]);

                    ctx->trace_action = (struct trace_action*) RTrealloc(ctx->trace_action, sizeof(struct trace_action) * (ctx->trace_count+1));
                    ctx->trace_action[ctx->trace_count].dst = (int*) RTmalloc(vec_bytes);
                    if (cpy != NULL) {
                        ctx->trace_action[ctx->trace_count].cpy = (int*) RTmalloc(vec_bytes);
                    } else {
                        ctx->trace_action[ctx->trace_count].cpy = NULL;
                    }

                    // set the required values in order to find the trace after the next-state call
                    memcpy(ctx->trace_action[ctx->trace_count].dst, dst, vec_bytes);
                    if (cpy != NULL) memcpy(ctx->trace_action[ctx->trace_count].cpy, cpy, vec_bytes);
                    ctx->trace_action[ctx->trace_count].action = action;

                    ctx->trace_count++;
                }

                add_fetch(&ErrorActions, 1);
            }
        }
    }
}

static void
explore_cb(vrel_t rel, void *context, int *src)
{
    group_add_info_t ctx;
    ctx.group = ((group_add_info_t*)context)->group;
    ctx.set = ((group_add_info_t*)context)->set;
    ctx.rel = rel;
    ctx.src = src;
    ctx.trace_count = 0;
    ctx.trace_action = NULL;
    (*transitions_short)(model, ctx.group, src, group_add, &ctx);

    if (ctx.trace_count > 0) {
        int long_src[N];
        for (int i = 0; i < ctx.trace_count; i++) {
            vset_example_match(ctx.set,long_src,r_projs[ctx.group].len, r_projs[ctx.group].proj,src);
            find_action(long_src,ctx.trace_action[i].dst,ctx.trace_action[i].cpy,ctx.group,ctx.trace_action[i].action);
            RTfree(ctx.trace_action[i].dst);
            if (ctx.trace_action[i].cpy != NULL) RTfree(ctx.trace_action[i].cpy);
        }

        RTfree(ctx.trace_action);
    }
}

#ifdef HAVE_SYLVAN
#define do_expand_group_next(g, s) CALL(do_expand_group_next, (g), (s))
VOID_TASK_2(do_expand_group_next, int, group, vset_t, set)
#else
static void do_expand_group_next(int group, vset_t set)
#endif
{
    group_add_info_t ctx;
    ctx.group = group;
    ctx.set = set;
    vset_project_minus(group_tmp[group], set, group_explored[group]);
    vset_union(group_explored[group], group_tmp[group]);

    if (log_active(infoLong)) {
        double elem_count;
        vset_count(group_tmp[group], NULL, &elem_count);

        if (elem_count >= 10000.0 * REL_PERF) {
            Print(infoLong, "expanding group %d for %.*g states.", group, DBL_DIG, elem_count);
        }
    }

    if (USE_PARALLELISM) {
        vrel_update(group_next[group], group_tmp[group], explore_cb, &ctx);
    } else {
        vrel_update_seq(group_next[group], group_tmp[group], explore_cb, &ctx);
    }

    vset_clear(group_tmp[group]);
}

void
expand_group_next (int group, vset_t set)
{
    LACE_ME;
    do_expand_group_next (group, set);
}

void
expand_group_next_projected(vrel_t rel, vset_t set, void *context)
{
    struct expand_info *expand_ctx = (struct expand_info*)context;
    (*expand_ctx->eg_count)++;

    vset_t group_explored = expand_ctx->group_explored;
    vset_zip(group_explored, set);

    group_add_info_t group_ctx;
    int group = expand_ctx->group;
    group_ctx.group = group;
    group_ctx.set = NULL;
    if (USE_PARALLELISM) {
        vrel_update(rel, set, explore_cb, &group_ctx);
    } else {
        vrel_update_seq(rel, set, explore_cb, &group_ctx);
    }
}

void
learn_guards_reduce(vset_t true_states, int t, long *guard_count,
                    vset_t *guard_maybe, vset_t false_states, vset_t maybe_states, vset_t tmp)
{
    LACE_ME;
    if (PINS_USE_GUARDS) {
        guard_t* guards = GBgetGuard(model, t);
        for (int g = 0; g < guards->count && !vset_is_empty(true_states); g++) {
            if (guard_count != NULL) (*guard_count)++;
            eval_label(guards->guard[g], true_states);

            if (!no_soundness_check) {

                // compute guard_maybe (= guard_true \cap guard_false)
                vset_copy(guard_maybe[guards->guard[g]], label_true[guards->guard[g]]);
                vset_intersect(guard_maybe[guards->guard[g]], label_false[guards->guard[g]]);

                if (!MAYBE_AND_FALSE_IS_FALSE) {
                    // If we have Promela, Java etc. then if we encounter a maybe guard then this is an error.
                    // Because every guard is evaluated in order.
                    if (!vset_is_empty(guard_maybe[guards->guard[g]])) {
                        Warning(info, "Condition in group %d does not evaluate to true or false", t);
                        HREabort(LTSMIN_EXIT_UNSOUND);
                    }
                } else {
                    // If we have mCRL2 etc., then we need to store all (real) false states and maybe states
                    // and see if after evaluating all guards there are still maybe states left.
                    vset_join(tmp, true_states, label_false[guards->guard[g]]);
                    vset_union(false_states, tmp);
                    vset_join(tmp, true_states, guard_maybe[guards->guard[g]]);
                    vset_minus(false_states,tmp);
                    vset_union(maybe_states, tmp);
                }
                vset_clear(guard_maybe[guards->guard[g]]);
            }
            vset_join(true_states, true_states, label_true[guards->guard[g]]);
        }

        if (!no_soundness_check && MAYBE_AND_FALSE_IS_FALSE) {
            vset_copy(tmp, maybe_states);
            vset_minus(tmp, false_states);
            if (!vset_is_empty(tmp)) {
                Warning(info, "Condition in group %d does not evaluate to true or false", t);
                HREabort(LTSMIN_EXIT_UNSOUND);
            }
            vset_clear(tmp);
            vset_clear(maybe_states);
            vset_clear(false_states);
        }

        if (!no_soundness_check) {
            for (int g = 0; g < guards->count; g++) {
                vset_minus(label_true[guards->guard[g]], label_false[guards->guard[g]]);
            }
        }
    }
}

struct label_add_info
{
    int label; // label number being evaluated
    int result; // desired result of the label
};

static void
eval_cb (vset_t set, void *context, int *src)
{
    // evaluate the label
    int result = GBgetStateLabelShort(model, ((struct label_add_info*)context)->label, src);

    // add to the correct set dependening on the result
    int dresult = ((struct label_add_info*)context)->result;
    if (
            dresult == result ||  // we have true or false (just add)
            (dresult == 0 && result == 2) ||  // always add maybe to false
            (dresult == 1 && result == 2 && !no_soundness_check)) { // if we want to do soundness
            vset_add(set, src);                                     // check then also add maybe to true.
                                                                    // maybe = false \cap true
    }
}

#ifdef HAVE_SYLVAN
#define do_eval_label(l, s) CALL(do_eval_label, (l), (s))
VOID_TASK_2(do_eval_label, int, label, vset_t, set)
#else
static void do_eval_label(int label, vset_t set)
#endif
{
    // get the short vectors we need to evaluate
    // minus what we have already evaluated
    vset_project_minus(label_tmp[label], set, label_false[label]);
    vset_minus(label_tmp[label], label_true[label]);

    // count when verbose
    if (log_active(infoLong)) {
        double elem_count;
        vset_count(label_tmp[label], NULL, &elem_count);
        if (elem_count >= 10000.0 * REL_PERF) {
            Print(infoLong, "expanding label %d for %.*g states.", label, DBL_DIG, elem_count);
        }
    }

    // we evaluate labels twice, because we can not yet add to two different sets.
    struct label_add_info ctx_false;

    ctx_false.label = label;
    ctx_false.result = 0;

    // evaluate labels and add to label_false[guard] when false
    if (USE_PARALLELISM) {
        vset_update(label_false[label], label_tmp[label], eval_cb, &ctx_false);
    } else {
        vset_update_seq(label_false[label], label_tmp[label], eval_cb, &ctx_false);
    }

    struct label_add_info ctx_true;

    ctx_true.label = label;
    ctx_true.result = 1;

    // evaluate labels and add to label_true[label] when true
    if (USE_PARALLELISM) {
        vset_update(label_true[label], label_tmp[label], eval_cb, &ctx_true);
    } else {
        vset_update_seq(label_true[label], label_tmp[label], eval_cb, &ctx_true);
    }

    vset_clear(label_tmp[label]);
}

void
eval_label (int label, vset_t set)
{
    LACE_ME;
    do_eval_label (label, set);
}

void
reduce(int group, vset_t set)
{
    if (PINS_USE_GUARDS) {
        guard_t *guards = GBgetGuard(model, group);
        for (int g = 0; g < guards->count && !vset_is_empty(set); g++) {
            vset_join(set, set, label_true[guards->guard[g]]);
        }
    }
}

void
learn_guards(vset_t states, long *guard_count) {
    if (PINS_USE_GUARDS) {
        for (int g = 0; g < nGuards; g++) {
            if (guard_count != NULL) (*guard_count)++;
            LACE_ME;
            do_eval_label(g, states);
        }
    }
}

#ifdef HAVE_SYLVAN
void
learn_guards_par(vset_t states, long *guard_count)
{
    LACE_ME;
    if (PINS_USE_GUARDS) {
        for (int g = 0; g < nGuards; g++) {
            if (guard_count != NULL) (*guard_count)++;
            SPAWN(do_eval_label, g, states);
        }
    }
    if (PINS_USE_GUARDS) {
        for (int g = 0; g < nGuards; g++) SYNC(do_eval_label);
    }
}
#endif

void
learn_labels(vset_t states)
{
    for (int i = 0; i < sLbls; i++) {
        LACE_ME;
        if (bitvector_is_set(&state_label_used, i)) do_eval_label(i, states);
    }
}

#ifdef HAVE_SYLVAN
void
learn_labels_par(vset_t states)
{
    LACE_ME;
    for (int i = 0; i < sLbls; i++) {
        if (bitvector_is_set(&state_label_used, i)) SPAWN(do_eval_label, i, states);
    }
    for (int i = 0; i < sLbls; i++) {
        if (bitvector_is_set(&state_label_used, i)) SYNC(do_eval_label);
    }
}
#endif

void
add_step (bool backward, vset_t addto, vset_t from, vset_t universe)
{
    vset_t          temp = empty ();
    vset_t          temp2 = empty ();
    for (int i = 0; i < nGrps; i++) {
        if (backward) {
            vset_prev (temp, from, group_next[i], universe);
            //reduce (i, temp); //TODO
        } else {
            vset_copy (temp2, from);
            //reduce (i, temp2); //TODO
            vset_next (temp, temp2, group_next[i]);
            vset_intersect (temp, universe);
        }
        vset_union (addto, temp);
        vset_clear (temp);
    }
    vset_destroy (temp);
    vset_destroy (temp2);
}


/**
 * Tree structure to evaluate the condition of a transition group.
 * If we disable the soundness check of guard-splitting then if we
 * have MAYBE_AND_FALSE_IS_FALSE (like mCRL(2) and SCOOP) then
 * (maybe && false == false) or (false && maybe == false) is not checked.
 * If we have !MAYBE_AND_FALSE_IS_FALSE (like Java, Promela and DVE) then only
 * (maybe && false == false) is not checked.
 * For guard-splitting ternary logic is used; i.e. (false,true,maybe) = (0,1,2) = (0,1,?).
 * Truth table for MAYBE_AND_FALSE_IS_FALSE:
 *      0 1 ?
 *      -----
 *  0 | 0 0 0
 *  1 | 0 1 ?
 *  ? | 0 ? ?
 * Truth table for !MAYBE_AND_FALSE_IS_FALSE:
 *      0 1 ?
 *      -----
 *  0 | 0 0 0
 *  1 | 0 1 ?
 *  ? | ? ? ?
 *
 *  Note that if a guard evaluates to maybe then we add it to both guard_false and guard_true, i.e. F \cap T != \emptyset.
 *  Soundness check: vset_is_empty(root(reach_red_s)->true_container \cap root(reach_red_s)->false_container) holds.
 *  Algorithm to carry all maybe states to the root:
 *  \bigcap X = Y \cap Z = (Fy,Ty) \cap (Fz,Tz):
 *   - T = (Ty \cap Tz) U M
 *   - F = Fy U Fz U M
 *   - M = MAYBE_AND_FALSE_IS_FALSE  => ((Fy \cap Ty) \ Fz) U ((Fz \cap Tz) \ Fy) &&
 *         !MAYBE_AND_FALSE_IS_FALSE => (Fy \cap Ty) U ((Fz \cap Tz) \ Fy)
 */

reach_red_t*
reach_red_prepare(size_t left, size_t right, int group)
{
    reach_red_t *result = (reach_red_t *)RTmalloc(sizeof(reach_red_t));
    if (right - left == 1) {
        result->index = left;
        result->left = NULL;
        result->right = NULL;
    } else {
        result->index = -1;
        result->left = reach_red_prepare(left, (left+right)/2, group);
        result->right = reach_red_prepare((left+right)/2, right, group);
    }
    result->group = group;
    result->true_container = vset_create(domain, -1, NULL);
    if (!no_soundness_check) {
        result->false_container = vset_create(domain, -1, NULL);
        result->left_maybe = vset_create(domain, -1, NULL);
        result->right_maybe = vset_create(domain, -1, NULL);
    }

    return result;
}

void
reach_red_destroy(reach_red_t *s)
{
    if (s->index == -1) {
        reach_red_destroy(s->left);
        reach_red_destroy(s->right);
    }
    vset_destroy(s->true_container);
    if (!no_soundness_check) {
        vset_destroy(s->false_container);
        vset_destroy(s->left_maybe);
        vset_destroy(s->right_maybe);
    }
    RTfree(s);
}

reach_t*
reach_prepare(size_t left, size_t right)
{
    reach_t *result = (reach_t *)RTmalloc(sizeof(reach_t));
    if (right - left == 1) {
        result->index = left;
        result->left = NULL;
        result->right = NULL;
        if (PINS_USE_GUARDS) result->red = reach_red_prepare(0, GBgetGuard(model, left)->count, left);
        else result->red = NULL;
    } else {
        result->index = -1;
        result->left = reach_prepare(left, (left+right)/2);
        result->right = reach_prepare((left+right)/2, right);
        result->red = NULL;
    }
    result->container = vset_create(domain, -1, NULL);
    result->ancestors = NULL;
    result->deadlocks = NULL;
    result->unsound_group = -1;
    if (inhibit_matrix != NULL || dlk_detect) {
        result->ancestors = vset_create(domain, -1, NULL);
    }
    if (dlk_detect) {
        result->deadlocks = vset_create(domain, -1, NULL);
    }
    return result;
}

void
reach_destroy(reach_t *s)
{
    if (s->index == -1) {
        reach_destroy(s->left);
        reach_destroy(s->right);
    }

    vset_destroy(s->container);
    if (s->ancestors != NULL) vset_destroy(s->ancestors);
    if (s->deadlocks != NULL) vset_destroy(s->deadlocks);

    if (s->red != NULL) reach_red_destroy(s->red);

    RTfree(s);
}
