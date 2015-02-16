/*
 * \file spg-attr.c
 */
#include <limits.h>
#if HAVE_PROFILER
#include <gperftools/profiler.h>
#endif
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <hre/config.h>
#include <hre/user.h>
#include <bignum/bignum.h>
#include <spg-lib/spg-solve.h>

/**
 * \brief Computes the attractor set for G, U.
 * The resulting set is stored in U.
 */
void
spg_attractor(const int player, const parity_game* g, recursive_result* result,
              vset_t u, spg_attr_options* options, int depth)
{
    int indent = 2 * depth;
    RTstopTimer(options->timer);
    Print(info, "[%7.3f] " "%*s" "attractor: player=%d", RTrealTime(options->timer), indent, "", player);
    RTstartTimer(options->timer);

    SPG_OUTPUT_DOT(options->dot,u,"spg_set_%05zu_u.dot",options->dot_count++);
    vset_t v_level = vset_create(g->domain, -1, NULL);
    vset_copy(v_level, u);
    int l = 0;

    // Compute fixpoint
    while (!vset_is_empty(v_level)) {
        if (options->compute_strategy) {
            update_strategy_levels(result, player, v_level);
        }
        SPG_ATTR_REPORT_LEVEL(indent, options,player,u,v_level,l);

        // prev_attr = V \intersect prev(attr^k)
        vset_t prev_attr = vset_create(g->domain, -1, NULL);
        vset_t tmp = vset_create(g->domain, -1, NULL);
        for(int group=0; group<g->num_groups; group++) {
            vset_clear(tmp);
            vset_prev(tmp, v_level, g->e[group], g->v);
            vset_union(prev_attr, tmp);
        }
        vset_clear(tmp);

        // Compute V_player \intersects prev_attr
        vset_clear(v_level);
        vset_copy(v_level, prev_attr);
        vset_intersect(v_level, g->v_player[player]);

        // B = V \intersect next(prev_attr)
        vset_t b = vset_create(g->domain, -1, NULL);
        for(int group=0; group<g->num_groups; group++) {
            vset_clear(tmp);
            vset_next(tmp, prev_attr, g->e[group]);
            vset_intersect(tmp, g->v);
            vset_union(b, tmp);
        }
        vset_clear(tmp);

        // B = B - U
        vset_minus(b, u);

        // prev_b = V \intersect prev(B)
        vset_t prev_b = vset_create(g->domain, -1, NULL);
        for(int group=0; group<g->num_groups; group++) {
            vset_clear(tmp);
            vset_prev(tmp, b, g->e[group], g->v);
            vset_union(prev_b, tmp);
        }
        vset_destroy(tmp);
        vset_destroy(b);

        // Compute V_other_player \intersects (prev_attr - prev_b)
        vset_t attr_other_player = vset_create(g->domain, -1, NULL);
        vset_copy(attr_other_player, prev_attr);
        vset_destroy(prev_attr);
        vset_minus(attr_other_player, prev_b);
        vset_destroy(prev_b);
        vset_intersect(attr_other_player, g->v_player[1-player]);

        vset_union(v_level, attr_other_player);
        vset_destroy(attr_other_player);

        // copy result:
        // U := U \union v_level
        // v_level := v_level - U
        vset_zip(u, v_level);

        SPG_OUTPUT_DOT(options->dot,u,"spg_set_%05zu_u_level_%d.dot",options->dot_count++,l);
        l++;
    }
    RTstopTimer(options->timer);
    Print(infoLong, "[%7.3f] " "%*s" "attr_%d: %d levels.", RTrealTime(options->timer), indent, "", player, l);
    RTstartTimer(options->timer);
    vset_destroy(v_level);
    (void)options;
}


#ifdef HAVE_SYLVAN

struct reach_par_s
{
    vset_t container;
    struct reach_par_s *left;
    struct reach_par_s *right;
    int index;
};

static struct reach_par_s*
attr_par_prepare(const parity_game* g, size_t left, size_t right)
{
    struct reach_par_s *result = (struct reach_par_s *)RTmalloc(sizeof(struct reach_par_s));
    if (right - left == 1) {
        result->index = left;
        result->left = NULL;
        result->right = NULL;
    } else {
        result->index = -1;
        result->left = attr_par_prepare(g, left, (left+right)/2);
        result->right = attr_par_prepare(g, (left+right)/2, right);
    }
    result->container = vset_create(g->domain, -1, NULL);
    return result;
}

void attr_par_destroy(struct reach_par_s* dummy)
{
    if (dummy->index >= 0) {
        vset_destroy(dummy->container);
    }
    else
    {
        attr_par_destroy(dummy->left);
        attr_par_destroy(dummy->right);
    }
}

VOID_TASK_3(attr_par_prev, vset_t, states, struct reach_par_s *, dummy, const parity_game*, g)
{
    if (dummy->index >= 0) {
        //fprintf(stderr, "begin next[%d] on worker %d\n", dummy->index, LACE_WORKER_ID);
        vset_clear(dummy->container);
        vset_prev(dummy->container, states, g->e[dummy->index], g->v);
        //fprintf(stderr, "end next[%d] on worker %d\n", dummy->index, LACE_WORKER_ID);
    } else {
        SPAWN(attr_par_prev, states, dummy->left, g);
        SPAWN(attr_par_prev, states, dummy->right, g);
        SYNC(attr_par_prev);
        SYNC(attr_par_prev);
        //fprintf(stderr, "begin union on worker %d\n", LACE_WORKER_ID);
        vset_copy(dummy->container, dummy->left->container);
        vset_union(dummy->container, dummy->right->container);
        vset_clear(dummy->left->container);
        vset_clear(dummy->right->container);
        //fprintf(stderr, "end union on worker %d\n", LACE_WORKER_ID);
    }
}

VOID_TASK_3(attr_par_next, vset_t, states, struct reach_par_s *, dummy, const parity_game*, g)
{
    if (dummy->index >= 0) {
        //fprintf(stderr, "begin next[%d] on worker %d\n", dummy->index, LACE_WORKER_ID);
        vset_next(dummy->container, states, g->e[dummy->index]);
        vset_intersect(dummy->container, g->v);
        //fprintf(stderr, "end next[%d] on worker %d\n", dummy->index, LACE_WORKER_ID);
    } else {
        SPAWN(attr_par_next, states, dummy->left, g);
        SPAWN(attr_par_next, states, dummy->right, g);
        SYNC(attr_par_next);
        SYNC(attr_par_next);
        //fprintf(stderr, "begin union on worker %d\n", LACE_WORKER_ID);
        vset_copy(dummy->container, dummy->left->container);
        vset_union(dummy->container, dummy->right->container);
        vset_clear(dummy->left->container);
        vset_clear(dummy->right->container);
        //fprintf(stderr, "end union on worker %d\n", LACE_WORKER_ID);
    }
}

VOID_TASK_2(task_intersect, vset_t, dst, vset_t, src)
{
    vset_intersect(dst, src);
}

VOID_TASK_2(task_union, vset_t, dst, vset_t, src)
{
    //printf("task_intersect: dst = %p\n", dst);
    vset_union(dst, src);
}

/**
 * \brief Computes the attractor set for G, U. (parallel version)
 * The resulting set is stored in U.
 */
void
spg_attractor_par(const int player, const parity_game* g, recursive_result* result,
                  vset_t u, spg_attr_options* options, int depth)
{
    int indent = 2*depth;
    RTstopTimer(options->timer);
    Print(info, "[%7.3f] attractor_par: player=%d", RTrealTime(options->timer), player);
    RTstartTimer(options->timer);
    SPG_OUTPUT_DOT(options->dot,u,"spg_set_%05zu_u.dot",options->dot_count++);
    vset_t v_level = vset_create(g->domain, -1, NULL);
    vset_copy(v_level, u);
    struct reach_par_s *root = attr_par_prepare(g, 0, g->num_groups);
    int l = 0;

    LACE_ME;

    // Compute fixpoint
    while (!vset_is_empty(v_level)) {
        if (options->compute_strategy) {
            update_strategy_levels(result, player, v_level);
        }
        SPG_ATTR_REPORT_LEVEL(indent,options,player,u,v_level,l);

        // prev_attr = V \intersect prev(attr^k)
        vset_t prev_attr = vset_create(g->domain, -1, NULL);
        CALL(attr_par_prev, v_level, root, g);
        vset_copy(prev_attr, root->container);
        vset_clear(root->container);

        // Compute V_player \intersects prev_attr
        vset_clear(v_level);
        vset_copy(v_level, prev_attr);
        SPAWN(task_intersect, v_level, g->v_player[player]);

        // B = V \intersect next(prev_attr)
        vset_t b = vset_create(g->domain, -1, NULL);
        CALL(attr_par_next, prev_attr, root, g);
        vset_copy(b, root->container);
        vset_clear(root->container);

        // B = B - U
        vset_minus(b, u);

        // prev_b = V \intersect prev(B)
        vset_t prev_b = vset_create(g->domain, -1, NULL);
        CALL(attr_par_prev, b, root, g);
        vset_copy(prev_b, root->container);
        vset_clear(root->container);
        vset_destroy(b);

        SYNC(task_intersect);

        // Compute V_other_player \intersects (prev_attr - prev_b)
        vset_t attr_other_player = vset_create(g->domain, -1, NULL);
        vset_copy(attr_other_player, prev_attr);
        vset_destroy(prev_attr);
        vset_minus(attr_other_player, prev_b);
        vset_destroy(prev_b);
        vset_intersect(attr_other_player, g->v_player[1-player]);

        //SYNC(task_intersect);

        vset_union(v_level, attr_other_player);
        vset_destroy(attr_other_player);

        // copy result:
        // U := U \union v_level
        // v_level := v_level - U
        vset_zip(u, v_level);

        SPG_OUTPUT_DOT(options->dot,u,"spg_set_%05zu_u_level_%d.dot",options->dot_count++,l);
        l++;
    }
    attr_par_destroy(root);
    vset_destroy(v_level);
    RTstopTimer(options->timer);
    Print(infoLong, "[%7.3f] " "%*s" "attr_%d: %d levels.", RTrealTime(options->timer), indent, "", player, l);
    RTstartTimer(options->timer);
    (void)options;
}


struct reach_par2_s
{
    vset_t container;
    vset_t prev_b;
    vset_t b;
    struct reach_par2_s *left;
    struct reach_par2_s *right;
    int index;
    struct reach_par_s *nested;
};

static struct reach_par2_s*
attr_par2_prepare(const parity_game* g, size_t left, size_t right, size_t l, size_t r)
{
    struct reach_par2_s *result = (struct reach_par2_s *)RTmalloc(sizeof(struct reach_par2_s));
    if (right - left == 1) {
        result->index = left;
        result->left = NULL;
        result->right = NULL;
        result->nested = attr_par_prepare(g, l, r);
        result->b = vset_create(g->domain, -1, NULL);
    } else {
        result->index = -1;
        result->left = attr_par2_prepare(g, left, (left+right)/2, l, r);
        result->right = attr_par2_prepare(g, (left+right)/2, right, l, r);
        result->nested = NULL;
    }
    result->container = vset_create(g->domain, -1, NULL);
    result->prev_b = vset_create(g->domain, -1, NULL);
    return result;
}

void attr_par2_destroy(struct reach_par2_s* dummy)
{
    vset_destroy(dummy->prev_b);
    vset_destroy(dummy->container);
    if (dummy->index >= 0) {
        vset_destroy(dummy->b);
        attr_par_destroy(dummy->nested);
    }
    else
    {
        attr_par2_destroy(dummy->left);
        attr_par2_destroy(dummy->right);
    }
}

VOID_TASK_4(attr_par_step, vset_t, states, vset_t, u, struct reach_par2_s *, dummy, const parity_game*, g)
{
    if (dummy->index >= 0) {
        //fprintf(stderr, "begin next[%d] on worker %d\n", dummy->index, LACE_WORKER_ID);
        vset_clear(dummy->container);
        vset_prev(dummy->container, states, g->e[dummy->index], g->v);

        // B = V \intersect next(prev_attr)
        CALL(attr_par_next, dummy->container, dummy->nested, g);
        vset_copy(dummy->b, dummy->nested->container);
        vset_clear(dummy->nested->container);

        // B = B - U
        vset_minus(dummy->b, u);

        // prev_b = V \intersect prev(B)
        CALL(attr_par_prev, dummy->b, dummy->nested, g);
        vset_copy(dummy->prev_b, dummy->nested->container);
        vset_clear(dummy->nested->container);
        vset_destroy(dummy->b);
        //fprintf(stderr, "end next[%d] on worker %d\n", dummy->index, LACE_WORKER_ID);
    } else {
        SPAWN(attr_par_step, states, u, dummy->left, g);
        SPAWN(attr_par_step, states, u, dummy->right, g);
        SYNC(attr_par_step);
        SYNC(attr_par_step);
        //fprintf(stderr, "begin union on worker %d\n", LACE_WORKER_ID);
        vset_copy(dummy->container, dummy->left->container);
        vset_copy(dummy->prev_b, dummy->left->prev_b);
        SPAWN(task_union, dummy->container, dummy->right->container);
        SPAWN(task_union, dummy->prev_b, dummy->right->prev_b);
        SYNC(task_union);
        SYNC(task_union);
        vset_clear(dummy->left->prev_b);
        vset_clear(dummy->right->prev_b);
        vset_clear(dummy->left->container);
        vset_clear(dummy->right->container);
        //fprintf(stderr, "end union on worker %d\n", LACE_WORKER_ID);
    }
}

/**
 * \brief Computes the attractor set for G, U.
 * Parallel version. Does backward and then forward steps together for each transition group.
 * The resulting set is stored in U.
 */
void
spg_attractor_par2(const int player, const parity_game* g, recursive_result* result,
                   vset_t u, spg_attr_options* options, int depth)
{
    int indent = 2*depth;
    RTstopTimer(options->timer);
    Print(info, "[%7.3f] attractor_par2: player=%d", RTrealTime(options->timer), player);
    RTstartTimer(options->timer);
    SPG_OUTPUT_DOT(options->dot,u,"spg_set_%05zu_u.dot",options->dot_count++);
    vset_t v_level = vset_create(g->domain, -1, NULL);
    vset_copy(v_level, u);
    int l = 0;

    LACE_ME;

    // Compute fixpoint
    while (!vset_is_empty(v_level)) {
        if (options->compute_strategy) {
            update_strategy_levels(result, player, v_level);
        }
        SPG_ATTR_REPORT_LEVEL(indent,options,player,u,v_level,l);

        // prev_attr = V \intersect prev(attr^k)
        vset_t prev_attr = vset_create(g->domain, -1, NULL);
        struct reach_par2_s *root = attr_par2_prepare(g, 0, g->num_groups, 0, g->num_groups);
        CALL(attr_par_step, v_level, u, root, g);
        vset_copy(prev_attr, root->container);
        vset_clear(root->container);
        // B = V \intersect next(prev_attr)
        // B = B - U
        // prev_b = V \intersect prev(B)
        vset_t prev_b = vset_create(g->domain, -1, NULL);
        vset_copy(prev_b, root->prev_b);
        vset_clear(root->prev_b);

        // Compute V_player \intersects prev_attr
        vset_clear(v_level);
        vset_copy(v_level, prev_attr);
        CALL(task_intersect, v_level, g->v_player[player]);
        //SYNC(task_intersect);

        // Compute V_other_player \intersects (prev_attr - prev_b)
        vset_t attr_other_player = vset_create(g->domain, -1, NULL);
        vset_copy(attr_other_player, prev_attr);
        vset_destroy(prev_attr);
        vset_minus(attr_other_player, prev_b);
        vset_destroy(prev_b);
        vset_intersect(attr_other_player, g->v_player[1-player]);

        //SYNC(task_intersect);

        vset_union(v_level, attr_other_player);
        vset_destroy(attr_other_player);

        // copy result:
        // U := U \union v_level
        // v_level := v_level - U
        vset_zip(u, v_level);

        SPG_OUTPUT_DOT(options->dot,u,"spg_set_%05zu_u_level_%d.dot",options->dot_count++,l);
        l++;
    }
    RTstopTimer(options->timer);
    Print(infoLong, "[%7.3f] " "%*s" "attr_%d: %d levels.", RTrealTime(options->timer), indent, "", player, l);
    RTstartTimer(options->timer);
    vset_destroy(v_level);
    (void)options;
}

#endif


/**
 * \brief Computes the attractor set for G, U.
 * The resulting set is stored in U.
 * FIXME: review, refactor, rewrite [properly implement chaining/saturation]
 */
void
spg_attractor_chaining(const int player, const parity_game* g, recursive_result* result,
                       vset_t u, spg_attr_options* options, int depth)
{
    int indent = 2*depth;
    RTstopTimer(options->timer);
    Print(info, "[%5.3f] " "%*s" "attractor_chain: player=%d", RTrealTime(options->timer), indent, "", player);
    RTstartTimer(options->timer);
    //vdom_t domain = g->domain;
    vset_t v_level = vset_create(g->domain, -1, NULL);
    vset_t v_previous_level = vset_create(g->domain, -1, NULL);
    vset_t v_group = vset_create(g->domain, -1, NULL);
    vset_copy(v_level, u);
    int l = 0;

    long peak_group_count = 0;
    long u_count;
    long v_level_count;
    long v_group_count;

    // Compute fixpoint
    while (!vset_is_empty(v_level)) {
        if (log_active(infoLong))
        {
            vset_count(u, &u_count, NULL);
            vset_count(v_level, &v_level_count, NULL);
            RTstopTimer(options->timer);
            Print(infoLong, "[%7.3f] attr_%d^%d: u has %ld nodes, v_level has %ld nodes, v_group has %ld nodes max.",
                  RTrealTime(options->timer), player, l, u_count, v_level_count, peak_group_count);
            RTstartTimer(options->timer);
            peak_group_count = 0;
        }

        vset_copy(v_previous_level, v_level);
        vset_clear(v_level);
        for(int group=0; group<g->num_groups; group++) {
            vset_copy(v_group, v_previous_level);
            int k = 0;
            while ((options->saturation || k < 1) && !vset_is_empty(v_group)) {
                if (log_active(infoLong))
                {
                    vset_count(u, &u_count, NULL);
                    vset_count(v_level, &v_level_count, NULL);
                    vset_count(v_group, &v_group_count, NULL);
                    Print(infoLong, "  %d: u has %ld nodes, v_level has %ld nodes, v_group has %ld nodes.", k, u_count, v_level_count, v_group_count);
                    if (v_group_count > peak_group_count) {
                        peak_group_count = v_group_count;
                    }
                }

                // prev_attr = V \intersect prev(attr^k)
                vset_t prev_attr = vset_create(g->domain, -1, NULL);
                vset_clear(prev_attr);
                vset_prev(prev_attr, v_group, g->e[group], g->v);
                vset_copy(v_group, prev_attr);
                vset_intersect(v_group, g->v_player[player]);

                vset_t prev_attr_other_player = prev_attr;
                vset_intersect(prev_attr_other_player, g->v_player[1-player]);
                // B = next(V \intersect prev_attr)
                vset_t b = vset_create(g->domain, -1, NULL);
                vset_t tmp = vset_create(g->domain, -1, NULL);
                for(int group=0; group<g->num_groups; group++) {
                    vset_clear(tmp);
                    vset_next(tmp, prev_attr_other_player, g->e[group]);
                    vset_intersect(tmp, g->v);
                    vset_union(b, tmp);
                }

                // B = B - U
                vset_minus(b, u);

                // prev_b = V \intersect prev(B)
                vset_t prev_b = vset_create(g->domain, -1, NULL);
                for(int group=0; group<g->num_groups; group++) {
                    vset_clear(tmp);
                    vset_prev(tmp, b, g->e[group], g->v);
                    vset_union(prev_b, tmp);
                }
                vset_destroy(tmp);
                vset_destroy(b);

                // Compute V_other_player \intersects (prev_attr - prev_b)
                vset_minus(prev_attr_other_player, prev_b);
                vset_destroy(prev_b);
                vset_union(v_group, prev_attr_other_player);
                vset_minus(v_group, u);
                vset_destroy(prev_attr);

                // copy group result
                vset_union(v_level, v_group);
                vset_union(v_previous_level, v_group);
                vset_union(u, v_group);
                k++;
            }
            Print(infoLong, "  group  %d: %d iterations.", group, k);
        }
        l++;
    }
    RTstopTimer(options->timer);
    Print(infoLong, "[%7.3f] attr_%d: %d levels.", RTrealTime(options->timer), player, l);
    RTstartTimer(options->timer);
    vset_destroy(v_group);
    vset_destroy(v_level);
    vset_destroy(v_previous_level);
    (void) result;
}
