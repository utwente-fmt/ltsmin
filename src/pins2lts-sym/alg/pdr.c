#include <hre/config.h>

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

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
#include <pins2lts-sym/alg/pdr.h>
#include <pins2lts-sym/aux/options.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/atomics.h>
#include <mc-lib/bitvector-ll.h>
#include <util-lib/bitset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>


static vset_t
get_eq_vset (int state_idx, int state_match)
{
  vset_t result = vset_create (domain, -1, NULL);
  vset_t one = vset_create (domain, -1, NULL); //TODO: vset_create_top
  int proj[1] = {state_idx};
  int match[1] = {state_match};
  vset_copy_match (result, one, 1, proj, match);
  vset_destroy (one);
  return result;
}

static vset_t
get_property_vset (ltsmin_expr_t e, ltsmin_parse_env_t env)
{

    switch (e->token) {
        case PRED_TRUE:  return vset_create (domain, -1, NULL); //TODO: vset_create_top
        case PRED_FALSE: return vset_create (domain, -1, NULL);
        case PRED_SVAR:
            HREassert (e->idx < 0, "Only state slots supported in expression.");
            return NULL;
        case PRED_AND: {
            vset_t s1 = get_property_vset (e->arg1, env);
            vset_t s2 = get_property_vset (e->arg2, env);
            vset_intersect (s1, s2);
            vset_destroy (s2);
            return s1;
        } break;
        case PRED_OR: {
            vset_t s1 = get_property_vset (e->arg1, env);
            vset_t s2 = get_property_vset (e->arg2, env);
            vset_union (s1, s2);
            vset_destroy (s2);
            return s1;
        } break;
        case PRED_NOT: {
            vset_t s1 = vset_create (domain, -1, NULL); //TODO: vset_create_top
            vset_t s2 = get_property_vset (e->arg1, env);
            vset_minus (s1, s2);
            vset_destroy (s2);
            return s1;
        } break;
        case PRED_EQ: {
            if (e->arg1->token == PRED_SVAR) {
                HREassert (e->arg2->token == PRED_NUM,"Unhandled predicate expression: %s", LTSminPrintExpr(e, env));
                return get_eq_vset (e->arg1->idx, e->arg2->idx);
            } else {
                HREassert (e->arg2->token == PRED_SVAR, "Unhandled predicate expression: %s", LTSminPrintExpr(e, env));
                HREassert (e->arg1->token == PRED_NUM);
                return get_eq_vset (e->arg2->idx, e->arg1->idx);
            }
        } break;
        case PRED_NEQ:
        case PRED_LT:
        case PRED_LEQ:
        case PRED_GT:
        case PRED_GEQ:
        default:
            LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
    }
}

struct group_add_info2 {
    int    group;
    int   *src;
    int   *explored;
    vset_t set;
    vrel_t rel;
};

static void
group_add2 (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    struct group_add_info2 *ctx = (struct group_add_info2*)context;

    int act_index = 0;
    if (ti->labels != NULL && act_label != -1) act_index = ti->labels[act_label];
    vrel_add_act (ctx->rel, ctx->src, dst, cpy, act_index);
}

static void
explore_cb2 (void *context, int *src)
{
    struct group_add_info2 *ctx = (struct group_add_info2*)context;

    ctx->src = src;
    GBgetTransitionsShort(model, ctx->group, src, group_add2, context);
    (*ctx->explored)++;

    if ((*ctx->explored) % 10000 == 0) {
        Warning(infoLong, "explored %d short vectors for group %d",
                    *ctx->explored, ctx->group);
    }
}

static inline void
expand_group_next2 (int group, vset_t set)
{
    struct group_add_info2 ctx;
    int explored = 0;
    ctx.group = group;
    ctx.set = set;
    ctx.rel = group_next[group];
    ctx.explored = &explored;
    vset_project(group_tmp[group], set);
    vset_zip(group_explored[group], group_tmp[group]);
    vset_enum(group_tmp[group], explore_cb2, &ctx);
    vset_clear(group_tmp[group]);
}


static void
reach_bfs2 (vset_t visited)
{
    int level = 0;
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);

    while (!vset_equal(visited, old_vis)) {
        vset_copy(old_vis, visited);
        //stats_and_progress_report(NULL, visited, level);
        level++;
        for (int i = 0; i < nGrps; i++) {
            expand_group_next2 (i, visited);
        }
        for (int i = 0; i < nGrps; i++) {
            vset_next (temp, old_vis, group_next[i]);
            vset_union (visited, temp);
        }
        vset_clear (temp);
        vset_reorder (domain);
    }

    vset_destroy (old_vis);
    vset_destroy (temp);
    long int n;
    double e;
    vset_count(visited, &n, &e);
    Warning (info, "Levels: %d  %f", level, e);
}

void
run_pdr (vset_t I, vset_t V)
{
    Warning (info, "PDRized [0]");
    vset_t P = get_property_vset (inv_expr[0], inv_parse_env[0]);
    reach_bfs2 (V);
    (void) P;
    (void) I;
}
