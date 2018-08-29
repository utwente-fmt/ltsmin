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
#include <ltsmin-lib/ltsmin-syntax.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-group.h>
#include <pins-lib/property-semantics.h>
#include <pins2lts-sym/alg/aux.h>
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


static void
write_trace_state(lts_file_t trace_handle, int src_no, int *state)
{
  int labels[sLbls];

  Warning(debug, "dumping state %d", src_no);

  for (int i = 0; i < sLbls; i++) {
      labels[i] = GBgetStateLabelLong(model, i, state);
  }

  lts_write_state(trace_handle, 0, state, labels);
}

struct write_trace_step_s {
    lts_file_t    trace_handle;
    int           src_no;
    int           dst_no;
    int          *dst;
    int           found;
};

static void
write_trace_next(void *arg, transition_info_t *ti, int *dst, int *cpy)
{
    struct write_trace_step_s *ctx = (struct write_trace_step_s*)arg;

    if (ctx->found)
        return;

    for(int i = 0; i < N; i++) {
        if (ctx->dst[i] != dst[i])
            return;
    }

    ctx->found = 1;
    lts_write_edge(ctx->trace_handle, 0, &ctx->src_no, 0, dst, ti->labels);
    (void)cpy;
}

static void
write_trace_step(lts_file_t trace_handle, int src_no, int *src,
                 int dst_no, int *dst)
{
    struct write_trace_step_s ctx;

    Warning(debug, "finding edge for state %d", src_no);
    ctx.trace_handle = trace_handle;
    ctx.src_no = src_no;
    ctx.dst_no = dst_no;
    ctx.dst = dst;
    ctx.found = 0;

    for (int i = 0; i < nGrps && !ctx.found; i++) {
        GBgetTransitionsLong(model, i, src, write_trace_next, &ctx);
    }

    if (!ctx.found)
        Abort("no matching transition found");
}

static void
write_trace(lts_file_t trace_handle, int **states, int total_states)
{
    // output starting from initial state, which is in states[total_states-1]

    for(int i = total_states - 1; i > 0; i--) {
        int current_step = total_states - i - 1;

        write_trace_state(trace_handle, current_step, states[i]);
        write_trace_step(trace_handle, current_step, states[i],
                         current_step + 1, states[i - 1]);
    }

    write_trace_state(trace_handle, total_states - 1, states[0]);
}

static void
find_trace_to(int trace_end[][N], int end_count, int level, vset_t *levels,
              lts_file_t trace_handle)
{
    int    prev_level   = level - 2;
    vset_t src_set      = vset_create(domain, -1, NULL);
    vset_t dst_set      = vset_create(domain, -1, NULL);
    vset_t temp         = vset_create(domain, -1, NULL);

    int   max_states    = 1024 + end_count;
    int   current_state = end_count;
    int **states        = RTmalloc(sizeof(int*[max_states]));

    for (int i = 0; i < end_count; i++)
        states[i] = trace_end[i];

    for(int i = end_count; i < max_states; i++)
        states[i] = RTmalloc(sizeof(int[N]));

    int     max_int_level  = 32;
    vset_t *int_levels     = RTmalloc(sizeof(vset_t[max_int_level]));

    for(int i = 0; i < max_int_level; i++)
        int_levels[i] = vset_create(domain, -1, NULL);

    while (prev_level >= 0) {
        int int_level = 0;

        if (vset_member(levels[prev_level], states[current_state - 1])) {
            Warning(debug, "Skipping level %d in trace generation", prev_level);
            prev_level--;
            continue;
        }

        vset_add(int_levels[0], states[current_state - 1]);

        // search backwards from states[current_state - 1] to prev_level
        do {
            int_level++;

            // grow int_levels if needed
            if (int_level == max_int_level) {
                max_int_level += 32;
                int_levels = RTrealloc(int_levels, sizeof(vset_t[max_int_level]));

                for(int i = int_level; i < max_int_level; i++)
                    int_levels[i] = vset_create(domain, -1, NULL);
            }

            for (int i=0; i < nGrps; i++) {
                vset_prev(temp, int_levels[int_level - 1], group_next[i], levels[level-1]); // just use last level as universe // TODO FIXME
                reduce(i, temp);

                vset_union(int_levels[int_level], temp);
                vset_intersect(temp, levels[prev_level]);
                if (!vset_is_empty(temp)) break; // found a good ancestor! we can leave now!
                else vset_clear(temp);
            }

            // if there was no ancestor, abort (this should be impossible!)
            if (vset_is_empty(int_levels[int_level])) Abort("Error trying to trace action!");

            // do this until we find an actual state from levels[prev_level], i.e., temp is not empty
        } while (vset_is_empty(temp));

        // grow states if needed
        if (current_state + int_level >= max_states) {
            int old_max_states = max_states;

            max_states = current_state + int_level + 1024;
            states = RTrealloc(states,sizeof(int*[max_states]));

            for(int i = old_max_states; i < max_states; i++)
                states[i] = RTmalloc(sizeof(int[N]));
        }

        vset_example(temp, states[current_state + int_level - 1]);

        // find the states that give us a trace to states[current_state - 1]
        for(int i = int_level - 1; i > 0; i--) {
            vset_clear(src_set);
            vset_add(src_set, states[current_state + i]);

            for(int j = 0; j < nGrps; j++) {
                reduce(j, temp);
                vset_next_fn(temp, src_set, group_next[j]);
                vset_union(dst_set, temp);
            }

            vset_intersect(dst_set, int_levels[i]);
            vset_minus(dst_set, src_set);
            vset_example(dst_set, states[current_state + i - 1]);
            vset_clear(src_set);
            vset_clear(dst_set);
        }

        current_state += int_level;
        prev_level--;

        for(int i = 0; i <= int_level; i++)
            vset_clear(int_levels[i]);

        vset_clear(temp);
    }

    write_trace(trace_handle, states, current_state);
}

void
find_trace(int trace_end[][N], int end_count, int level, vset_t *levels, char* file_prefix)
{
    // Find initial state and open output file
    int             init_state[N];
    hre_context_t   n = HREctxCreate(0, 1, "blah", 0);
    lts_file_t      trace_output = lts_vset_template();
    lts_type_t      ltstype = GBgetLTStype(model);

    GBgetInitialState(model, init_state);
    lts_file_set_context(trace_output, n);

    char file_name[(5+strlen(trc_output)+strlen(file_prefix))*sizeof(char)];
    sprintf(file_name, "%s%s.%s", trc_output, file_prefix, trc_type);
    Warning(info,"writing to file: %s",file_name);
    trace_output = lts_file_create(file_name, ltstype, 1, trace_output);
    lts_write_init(trace_output, 0, (uint32_t*)init_state);
    int T=lts_type_get_type_count(ltstype);
    for(int i=0;i<T;i++){
        lts_file_set_table(trace_output,i,GBgetChunkMap(model,i));
    }

    // Generate trace
    rt_timer_t  timer = RTcreateTimer();
    RTstartTimer(timer);
    find_trace_to(trace_end, end_count, level, levels, trace_output);
    RTstopTimer(timer);
    RTprintTimer(info, timer, "constructing trace for '%s' took", file_prefix);

    // Close output file
    lts_file_close(trace_output);
}

void
find_action(int *src, int *dst, int *cpy, int group, char *action)
{
    int trace_end[2][N];

    for (int i = 0; i < N; i++) {
        trace_end[0][i] = src[i];
        trace_end[1][i] = src[i];
    }

    // Set dst of the last step of the trace to its proper value
    for (int i = 0; i < w_projs[group]->count; i++) {
        int w = ci_get (w_projs[group], i);
        if (cpy == NULL || cpy[i] == 0 || ci_binary_search(r_projs[group], w) != -1) {
            trace_end[0][w] = dst[i];
        }
    }

    find_trace (trace_end, 2, global_level, levels, action);
}

struct inv_info_s {
    vset_t container;
    void* work;
};

static void
inv_info_destroy(void* context)
{
    struct inv_info_s* info = (struct inv_info_s*) context;
    vset_destroy(info->container);
    RTfree(info);
}

struct inv_rel_s {
    vset_t tmp; // some workspace for learning
    vset_t true_states; // all short states that satisfy the expression
    vset_t false_states; // all short states that do not satisfy the expression
    vset_t shortcut; // only used when not evaluating every binary operand in parallel.
    int* vec; // space for long vector
    int len; // length of short vector
    int* deps; // dependencies for short vector
};

static void
inv_rel_destroy(void* context)
{
    struct inv_info_s* info = (struct inv_info_s*) context;
    struct inv_rel_s* rel = (struct inv_rel_s*) info->work;

    vset_destroy(rel->tmp);
    vset_destroy(rel->true_states);
    vset_destroy(rel->false_states);
    if (!inv_bin_par) vset_destroy(rel->shortcut);
    inv_info_destroy(info);
}

static void
inv_svar_destroy(void* context)
{
    struct inv_info_s* info = (struct inv_info_s*) context;

    if (info->work != NULL) vset_destroy((vset_t) info->work);
    inv_info_destroy(info);
}

static inline void
inv_cleanup()
{
    bitvector_clear(&state_label_used);

    int n_violated = 0;
    for (int i = 0; i < num_inv; i++) {
        if (!inv_violated[i]) {
            bitvector_union(&state_label_used, &inv_sl_deps[i]);
        } else n_violated++;
    }

    if (n_violated == num_inv) RTfree(inv_detect);

    if (PINS_USE_GUARDS) {
        for (int i = 0; i < nGuards; i++) {
            bitvector_set(&state_label_used, i);
        }
    }

    if (label_true != NULL) {
        for (int i = 0; i < sLbls; i++) {
            if (!bitvector_is_set(&state_label_used, i)) {
                if (label_true[i] != NULL) {
                    vset_destroy(label_false[i]);
                    vset_destroy(label_true[i]);
                    vset_destroy(label_tmp[i]);
                    label_false[i] = NULL;
                    label_true[i] = NULL;
                    label_tmp[i] = NULL;
                }
            }
        }
    }
}

void
init_action_detection()
{
    if (act_label == -1)
        Abort("No edge label '%s...' for action detection", LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    int count = 8; // GBchunkCount(model, action_typeno);
    // create vector with 2 values per bucket, i.e. one bit per bucket
    Print(infoLong, "Preparing action cache for %zu action labels.", (size_t)(1ULL << count)-1);
    seen_actions = BVLLcreate (2, count);
    Warning(info, "Detecting actions with prefix \"%s\"", act_detect);
}

void
init_invariant_detection()
{
    inv_proj = (ci_list **) RTmalloc(sizeof(ci_list *[num_inv]));
    inv_expr = (ltsmin_expr_t*) RTmalloc(sizeof(ltsmin_expr_t) * num_inv);
    inv_violated = (int*) RTmallocZero(sizeof(int) * num_inv);
    inv_parse_env = (ltsmin_parse_env_t*) RTmalloc(sizeof(ltsmin_parse_env_t) * num_inv);
    inv_deps = (bitvector_t*) RTmalloc(sizeof(bitvector_t) * num_inv);
    inv_sl_deps = (bitvector_t*) RTmalloc(sizeof(bitvector_t) * num_inv);

    for (int i = 0; i < num_inv; i++) {
        inv_parse_env[i] = LTSminParseEnvCreate();
        inv_expr[i] = pred_parse_file(inv_detect[i], inv_parse_env[i], ltstype);
        if (log_active(infoLong)) {
            const char s[] = "Loaded and optimized invariant #%d: ";
            char buf[snprintf(NULL, 0, s, i + 1) + 1];
            sprintf(buf, s, i + 1);
            LTSminLogExpr(infoLong, buf, inv_expr[i], inv_parse_env[i]);
        }
        bitvector_create (&inv_deps[i], N);
        bitvector_create (&inv_sl_deps[i], sLbls);
        set_groups_of_edge (model, inv_expr[i]);
        set_pins_semantics (model, inv_expr[i], inv_parse_env[i], &inv_deps[i], &inv_sl_deps[i]);
        size_t n = bitvector_n_high (&inv_deps[i]);
        inv_proj[i] = ci_create (n);
        bitvector_high_bits (&inv_deps[i], inv_proj[i]->data);
        inv_proj[i]->count = n;
    }

    inv_cleanup ();

    if (inv_par) label_locks = (int*) RTmallocZero(sizeof(int[sLbls]));
}

void
inv_info_prepare(ltsmin_expr_t e, ltsmin_parse_env_t env, int i)
{
    struct inv_info_s* c;
    switch(e->token) {
    case PRED_NOT:
        inv_info_prepare(e->arg1, env, i);
        c = RTmalloc(sizeof(struct inv_info_s));
        e->destroy_context = inv_info_destroy;
        break;
    case PRED_AND:
    case PRED_OR:
        inv_info_prepare(e->arg1, env, i);
        inv_info_prepare(e->arg2, env, i);
        c = RTmalloc(sizeof(struct inv_info_s));
        e->destroy_context = inv_info_destroy;
        break;
    case PRED_TRUE:
    case PRED_FALSE:
        c = RTmalloc(sizeof(struct inv_info_s));
        e->destroy_context = inv_info_destroy;
        break;
    case PRED_SVAR: {
        c = RTmalloc(sizeof(struct inv_info_s));
        e->destroy_context = inv_svar_destroy;
        if (e->idx < N) { // state variable
            // make sure the state variable is a Boolean
            HREassert(lts_type_get_format(
                ltstype,
                lts_type_get_state_typeno(ltstype, e->idx)) == LTStypeBool);
            /* create vset_t where this state variable is true. */
            int proj[1] = { e->idx };
            c->work = vset_create(domain, 1, proj);
            int t[1] = { 1 };
            vset_add(c->work, t);
        } else if (!inv_bin_par) { // state label
            /* create vset_t because we can not directly project
             * from the invariant domain to the state label domain.
             * However this vset_t is only necessary when we do not
             * evaluate invariants in parallel. In the parallel setting
             * the state labels will already be evaluated. In the sequential
             * setting the state labels must still be evaluated because the
             * binary operators '&&' and '||' use short-circuit evaluation. */
            c->work = vset_create(domain, -1, NULL);
        } else c->work = NULL;
        break;
    }
    case PRED_EN:
    case PRED_EQ:
    case PRED_NEQ:
    case PRED_LT:
    case PRED_LEQ:
    case PRED_GT:
    case PRED_GEQ: {
        bitvector_t deps;
        bitvector_create(&deps, N);
        set_groups_of_edge(model, e);
        set_pins_semantics(model, e, env, &deps, NULL);

        const int len = bitvector_n_high(&deps);

        c = RTmalloc(sizeof(struct inv_info_s)
                + sizeof(struct inv_rel_s)
                + sizeof(int[N])
                + sizeof(int[len]));

        struct inv_rel_s* rel = c->work = (struct inv_rel_s*) (c + 1);
        rel->vec = (int*) (rel + 1);
        rel->deps = (int*) (rel->vec + N);

        e->destroy_context = inv_rel_destroy;

        GBgetInitialState(model, rel->vec);

        rel->len = len;
        bitvector_high_bits(&deps, rel->deps);
        rel->tmp = vset_create(domain, rel->len, rel->deps);
        rel->true_states = vset_create(domain, rel->len, rel->deps);
        rel->false_states = vset_create(domain, rel->len, rel->deps);
        if (!inv_bin_par) rel->shortcut = vset_create(domain, -1, NULL);
        bitvector_free(&deps);
        break;
    }
    default:
        LTSminLogExpr (lerror, "Unhandled predicate expression: ", e, env);
        HREabort (LTSMIN_EXIT_FAILURE);
    }
    e->context = c;
    c->container = vset_create(domain, inv_proj[i]->count, inv_proj[i]->data);
}

void
rel_expr_cb(vset_t set, void *context, int *e)
{
    rel_expr_info_t* ctx = (rel_expr_info_t *) context;
    int vec[N];
    memcpy(vec, ctx->vec, sizeof(int[N]));
    for (int i = 0; i < ctx->len; i++) vec[ctx->deps[i]] = e[i];
    if (eval_state_predicate(model, ctx->e, vec, ctx->env)) vset_add(set, e);
}

#ifdef HAVE_SYLVAN
#define eval_predicate_set_par(e, env, s) CALL(eval_predicate_set_par, (e), (env), (s))
VOID_TASK_3(eval_predicate_set_par, ltsmin_expr_t, e, ltsmin_parse_env_t, env, vset_t, states)
{
    struct inv_info_s* c = (struct inv_info_s*) e->context;
    struct inv_info_s* left, *right;
    left = right = NULL;
    if (e->node_type == UNARY_OP || e->node_type == BINARY_OP) left = (struct inv_info_s*) e->arg1->context;
    if (e->node_type == BINARY_OP) right = (struct inv_info_s*) e->arg2->context;

    switch (e->token) {
        case PRED_TRUE: {
            // do nothing (c->container already contains everything)
        } break;
        case PRED_FALSE: {
            vset_clear(c->container);
        } break;
        case PRED_SVAR:
            if (e->idx < N) { // state variable
                vset_t svar = (vset_t) c->work;
                vset_join(c->container, c->container, svar);
            } else { // state label
                vset_join(c->container, c->container, label_true[e->idx - N]);
            }
            break;
        case PRED_NOT: {
            vset_copy(left->container, c->container);
            eval_predicate_set_par(e->arg1, env, states);
            vset_minus(c->container, left->container);
            vset_clear(left->container);
        } break;
        case PRED_AND: {
            vset_copy(left->container, c->container);
            SPAWN(eval_predicate_set_par, e->arg1, env, states);
            vset_copy(right->container, c->container);
            vset_clear(c->container);
            eval_predicate_set_par(e->arg2, env, states);
            SYNC(eval_predicate_set_par);
            vset_copy(c->container, left->container);
            vset_clear(left->container);
            vset_intersect(c->container, right->container);
            vset_clear(right->container);
        } break;
        case PRED_OR: {
            vset_copy(left->container, c->container);
            SPAWN(eval_predicate_set_par, e->arg1, env, states);
            vset_copy(right->container, c->container);
            vset_clear(c->container);
            eval_predicate_set_par(e->arg2, env, states);
            SYNC(eval_predicate_set_par);
            vset_copy(c->container, left->container);
            vset_clear(left->container);
            vset_union(c->container, right->container);
            vset_clear(right->container);
        } break;
        case PRED_EQ:
        case PRED_EN:
        case PRED_NEQ:
        case PRED_LT:
        case PRED_LEQ:
        case PRED_GT:
        case PRED_GEQ: {
            struct inv_rel_s* rel = (struct inv_rel_s*) c->work;

            vset_project_minus(rel->tmp, states, rel->true_states);
            vset_minus(rel->tmp, rel->false_states);

            struct rel_expr_info ctx;
            ctx.vec = rel->vec;

            ctx.len = rel->len;
            ctx.deps = rel->deps;

            ctx.e = e;
            ctx.env = env;

            // count when verbose
            if (log_active(infoLong)) {
                double elem_count;
                vset_count(rel->tmp, NULL, &elem_count);
                if (elem_count >= 10000.0 * REL_PERF) {
                    char* p = LTSminPrintExpr(e, env);
                    Print(infoLong, "evaluating subformula %s for %.*g states.", p, DBL_DIG, elem_count);
                    RTfree(p);
                }
            }

            vset_update(rel->true_states, rel->tmp, rel_expr_cb, &ctx);
            vset_minus(rel->tmp, rel->true_states);
            vset_union(rel->false_states, rel->tmp);
            vset_clear(rel->tmp);
            vset_join(c->container, c->container, rel->true_states);
            break;
        }
        default:
            LTSminLogExpr (lerror, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
    }
}
#endif

static void
eval_predicate_set(ltsmin_expr_t e, ltsmin_parse_env_t env, vset_t states)
{
    struct inv_info_s* c = (struct inv_info_s*) e->context;
    struct inv_info_s* left, *right;
    left = right = NULL;
    if (e->node_type == UNARY_OP || e->node_type == BINARY_OP) left = (struct inv_info_s*) e->arg1->context;
    if (e->node_type == BINARY_OP) right = (struct inv_info_s*) e->arg2->context;

    switch (e->token) {
        case PRED_TRUE: {
            // do nothing (c->container already contains everything)
        } break;
        case PRED_FALSE: {
            vset_clear(c->container);
        } break;
        case PRED_SVAR: {
            vset_t svar = (vset_t) c->work;

            if (e->idx >= N) { // state label
                /* following join is necessary because vset does not yet support
                 * set projection of a projected set. */
                vset_join(svar, c->container, states);
#ifdef HAVE_SYLVAN
                LACE_ME;
                if (inv_par) {
                    volatile int* ptr = &label_locks[e->idx - N];
                    while (!cas(ptr, 0, 1)) {
                        lace_steal_random();
                        ptr = &label_locks[e->idx - N];
                    }
                }
#endif
                eval_label(e->idx - N, svar);
                if (inv_par) label_locks[e->idx - N] = 0;
                vset_join(c->container, c->container, label_true[e->idx - N]);
                vset_clear(svar);
            } else { // state variable
                vset_join(c->container, c->container, svar);
            }
        } break;
        case PRED_NOT: {
            vset_copy(left->container, c->container);
            eval_predicate_set(e->arg1, env, states);
            vset_minus(c->container, left->container);
            vset_clear(left->container);
        } break;
        case PRED_AND: {
            vset_copy(left->container, c->container);
            vset_clear(c->container);
            eval_predicate_set(e->arg1, env, states);
            if (!vset_is_empty(left->container)) {
                vset_copy(right->container, left->container); // epic win for state labels
                eval_predicate_set(e->arg2, env, states);
                vset_copy(c->container, right->container);
                vset_intersect(c->container, left->container);
                vset_clear(right->container);
                vset_clear(left->container);
            }
        } break;
        case PRED_OR: {
            vset_copy(left->container, c->container);
            eval_predicate_set(e->arg1, env, states);
            if (!vset_equal(left->container, c->container)) {
                vset_copy(right->container, c->container);
                vset_minus(right->container, left->container); // epic win for state labels
                eval_predicate_set(e->arg2, env, states);
                vset_copy(c->container, left->container);
                vset_union(c->container, right->container);
                vset_clear(right->container);
            }
            vset_clear(left->container);
        } break;
        case PRED_EN:
        case PRED_EQ:
        case PRED_NEQ:
        case PRED_LT:
        case PRED_LEQ:
        case PRED_GT:
        case PRED_GEQ: {
            struct inv_rel_s* rel = (struct inv_rel_s*) c->work;

            // this join is necessary because we can not project an already projected vset.
            vset_join(rel->shortcut, states, c->container);

            vset_project_minus(rel->tmp, rel->shortcut, rel->true_states);
            vset_clear(rel->shortcut);
            vset_minus(rel->tmp, rel->false_states);

            struct rel_expr_info ctx;
            ctx.vec = rel->vec;

            ctx.len = rel->len;
            ctx.deps = rel->deps;

            ctx.e = e;
            ctx.env = env;

            // count when verbose
            if (log_active(infoLong)) {
                double elem_count;
                vset_count(rel->tmp, NULL, &elem_count);
                if (elem_count >= 10000.0 * REL_PERF) {
                    const char* p = LTSminPrintExpr(e, env);
                    Print(infoLong, "evaluating subformula %s for %.*g states.", p, DBL_DIG, elem_count);
                }
            }

            vset_update(rel->true_states, rel->tmp, rel_expr_cb, &ctx);
            vset_minus(rel->tmp, rel->true_states);
            vset_union(rel->false_states, rel->tmp);
            vset_clear(rel->tmp);
            vset_join(c->container, c->container, rel->true_states);
            break;
        }
        default:
            LTSminLogExpr (lerror, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
    }
}

static inline void
check_inv(vset_t states, const int level)
{
    if (num_inv_violated != num_inv && !vset_is_empty(states)) {
        int iv = 0;
        for (int i = 0; i < num_inv; i++) {
            if (!inv_violated[i]) {
                vset_project(inv_set[i], states);
                if (!vset_is_empty(inv_set[i])) {
                    vset_t container = ((struct inv_info_s*) inv_expr[i]->context)->container;
                    vset_copy(container, inv_set[i]);
                    eval_predicate_set(inv_expr[i], inv_parse_env[i], states);
                    if (!vset_equal(inv_set[i], container)) {
                        LTSminExprDestroy(inv_expr[i], 1);
                        LTSminParseEnvDestroy(inv_parse_env[i]);
                        vset_destroy(inv_set[i]);
                        Warning(info, " ");
                        Warning(info, "Invariant violation (%s) found at depth %d!", inv_detect[i], level);
                        Warning(info, " ");
                        inv_violated[i] = 1;
                        iv = 1;
                        num_inv_violated++;
                        if (num_inv_violated == num_inv) {
                            Warning(info, "all invariants violated");
                            if(!no_exit) {
                                RTstopTimer(reach_timer);
                                RTprintTimer(info, reach_timer, "invariant detection took");
                                Warning(info, "exiting now");
                                GBExit(model);
                                HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
                            }
                            Warning(info, "continuing...")
                        }
                    } else {
                        vset_clear(inv_set[i]);
                        vset_clear(container);
                    }
                }
            }
        }
        if (iv) inv_cleanup();
    }
}

#ifdef HAVE_SYLVAN
TASK_3(int, check_inv_par_go, vset_t, states, int, i, int, level)
{
    int res = 0;
    if (!inv_violated[i]) {
        vset_project(inv_set[i], states);
        if (!vset_is_empty(inv_set[i])) {
            vset_t container = ((struct inv_info_s*) inv_expr[i]->context)->container;
            vset_copy(container, inv_set[i]);
            if (inv_bin_par) eval_predicate_set_par(inv_expr[i], inv_parse_env[i], states);
            else eval_predicate_set(inv_expr[i], inv_parse_env[i], states);

            if (!vset_equal(inv_set[i], container)) {
                LTSminExprDestroy(inv_expr[i], 1);
                LTSminParseEnvDestroy(inv_parse_env[i]);
                vset_destroy(inv_set[i]);
                Warning(info, " ");
                Warning(info, "Invariant violation (%s) found at depth %d!", inv_detect[i], level);
                Warning(info, " ");
                RTfree(inv_detect[i]);
                inv_violated[i] = 1;
                res = 1;
                add_fetch(&num_inv_violated, 1);
            } else {
                vset_clear(container);
                vset_clear(inv_set[i]);
            }
        }
    }
    return res;
}

static inline void
check_inv_par(vset_t states, const int level)
{
    LACE_ME;
    if (num_inv_violated != num_inv && !vset_is_empty(states)) {
        if (inv_bin_par) learn_labels_par(states);
        int iv = 0;
        for (int i = 0; i < num_inv; i++) {
            SPAWN(check_inv_par_go, states, i, level);
        }
        for (int i = 0; i < num_inv; i++) {
            int res = SYNC(check_inv_par_go);
            iv = res || iv;
        }
        if (num_inv_violated == num_inv) {
            Warning(info, "all invariants violated");
            if(!no_exit) {
                Warning(info, "exiting now");
                RTstopTimer(reach_timer);
                RTprintTimer(info, reach_timer, "invariant detection took");
                GBExit(model);
                HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
            }
            Warning(info, "continuing...")
        }
        if (iv) inv_cleanup();
    }
}
#else
#define check_inv_par(s,l)
#endif

void
check_invariants(vset_t set, int level)
{
    if (inv_par) check_inv_par(set, level);
    else check_inv(set, level);
}

static void
valid_end_cb(void *context, int *src)
{
    int *state = (int *) context;
    if (!state[N] && !pins_state_is_valid_end(model, src)) {
        memcpy (state, src, sizeof(int[N]));
        state[N] = 1;
    }
}

void
deadlock_check(vset_t deadlocks, bitvector_t *reach_groups)
// checks for deadlocks, generate trace if requested, and unsets dlk_detect
{
    if (vset_is_empty(deadlocks))
        return;

    Warning(debug, "Potential deadlocks found");

    vset_t next_temp = vset_create(domain, -1, NULL);
    vset_t prev_temp = vset_create(domain, -1, NULL);
    vset_t new_reduced[nGrps];

    for(int i=0;i<nGrps;i++) {
        new_reduced[i]=vset_create(domain, -1, NULL);
    }

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
    for (int i = 0; i < nGrps; i++) {
        if (bitvector_is_set(reach_groups, i)) continue;
        vset_copy(new_reduced[i], deadlocks);
        learn_guards_reduce(new_reduced[i], i, NULL, guard_maybe, false_states, maybe_states, tmp);
        expand_group_next(i, new_reduced[i]);
        vset_next_fn(next_temp, new_reduced[i], group_next[i]);
        vset_prev(prev_temp, next_temp, group_next[i],new_reduced[i]);
        reduce(i, prev_temp);
        vset_minus(deadlocks, prev_temp);
    }

    vset_destroy(next_temp);
    vset_destroy(prev_temp);

    for(int i=0;i<nGrps;i++) {
        vset_destroy(new_reduced[i]);
    }
    if(!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            vset_destroy(guard_maybe[i]);
        }
        vset_destroy(tmp);
        vset_destroy(false_states);
        vset_destroy(maybe_states);
    }

    if (vset_is_empty(deadlocks))
        return;

    int dlk_state[1][N + 1];
    if (pins_get_valid_end_state_label_index(model) >= 0) {
        dlk_state[0][N] = 0; // Did not find an invalid end state yet
        vset_enum (deadlocks, valid_end_cb, dlk_state[0]);
        if (!dlk_state[0][N])
            return;
    } else {
        vset_example(deadlocks, dlk_state[0]);
    }

    Warning(info, "deadlock found");

    if (trc_output) {
        find_trace(dlk_state, 1, global_level, levels, "deadlock");
    }

    if (no_exit) {
        dlk_detect=0; // avoids checking for more deadlocks; as long as dlk_detect==1, no deadlocks have been found.
    } else {
        RTstopTimer(reach_timer);
        RTprintTimer(info, reach_timer, "deadlock detection took");
        Warning(info, "exiting now");
        GBExit(model);
        HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
    }
}

