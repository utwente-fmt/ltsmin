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
#include <ltsmin-lib/ltsmin-syntax.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-group.h>
#include <pins-lib/property-semantics.h>
#include <pins2lts-sym/alg/mu.h>
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

vset_t
mu_compute(ltsmin_expr_t mu_expr, ltsmin_parse_env_t env, vset_t visited,
           vset_t* mu_var, array_manager_t mu_var_man)
{
    vset_t result = NULL;
    switch(mu_expr->token) {
    case MU_TRUE:
        result = vset_create(domain, -1, NULL);
        vset_copy(result, visited);
        return result;
    case MU_FALSE:
        return vset_create(domain, -1, NULL);
    case MU_OR: { // OR
        result = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
        vset_t mc = mu_compute(mu_expr->arg2, env, visited, mu_var, mu_var_man);
        vset_union(result, mc);
        vset_destroy(mc);
    } break;
    case MU_AND: { // AND
        result = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
        vset_t mc = mu_compute(mu_expr->arg2, env, visited, mu_var, mu_var_man);
        vset_intersect(result, mc);
        vset_destroy(mc);
    } break;
    case MU_NOT: { // NEGATION
        result = vset_create(domain, -1, NULL);
        vset_copy(result, visited);
        vset_t mc = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
        vset_minus(result, mc);
        vset_destroy(mc);
    } break;
    case MU_EXIST: { // E
        if (mu_expr->arg1->token == MU_NEXT) {
            result = vset_create(domain, -1, NULL);
            vset_t g = mu_compute(mu_expr->arg1->arg1, env, visited, mu_var, mu_var_man);

            add_step (true, result, g, visited);
        } else {
            Abort("invalid operator following MU_EXIST, expecting MU_NEXT");
        }
    } break;
    case MU_SVAR: {
        if (mu_expr->idx < N) { // state variable
            Abort("Unhandled MU_SVAR");
        } else { // state label
            result = vset_create(domain, -1, NULL);
            vset_join(result, visited, label_true[mu_expr->idx - N]);
        }
    } break;
    case MU_VAR:
        ensure_access(mu_var_man, mu_expr->idx);
        result = vset_create(domain, -1, NULL);
        vset_copy(result, mu_var[mu_expr->idx]);
        break;
    case MU_ALL:
        if (mu_expr->arg1->token == MU_NEXT) {
            // implemented as AX phi = ! EX ! phi

            result = vset_create(domain, -1, NULL);
            vset_copy(result, visited);

            // compute ! phi
            vset_t notphi = vset_create(domain, -1, NULL);
            vset_copy(notphi, visited);
            vset_t phi = mu_compute(mu_expr->arg1->arg1, env, visited, mu_var, mu_var_man);
            vset_minus(notphi, phi);
            vset_destroy(phi);

            // EX !phi
            vset_t prev = vset_create(domain, -1, NULL);
            add_step (true, prev, notphi, visited);

            // and negate result again
            vset_minus(result, prev);
            vset_destroy(prev);
            vset_destroy(notphi);
        } else {
            Abort("invalid operator following MU_ALL, expecting MU_NEXT");
        }
        break;
    case MU_MU:
        {
            ensure_access(mu_var_man, mu_expr->idx);
            // backup old var reference
            vset_t old = mu_var[mu_expr->idx];
            result = mu_var[mu_expr->idx] = vset_create(domain, -1, NULL);
            vset_t tmp = vset_create(domain, -1, NULL);
            do {
                vset_copy(mu_var[mu_expr->idx], tmp);
                vset_clear(tmp);
                tmp = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
                if (log_active(infoLong) || peak_nodes) {
                    long n1, n2;
                    long double e1, e2;
                    const int d1 = vset_count_fn (mu_var[mu_expr->idx], &n1, &e1);
                    const int d2 = vset_count_fn (tmp, &n2, &e2);
                    Warning(infoLong, "MU %s: %.*Lg (%ld) -> %.*Lg (%ld)",
                        SIget(env->idents,mu_expr->idx), d1, e1, n1, d2, e2, n2);

                    if (n1 > max_mu_count) max_mu_count = n1;
                    if (n2 > max_mu_count) max_mu_count = n1;
                }
            } while (!vset_equal(mu_var[mu_expr->idx], tmp));

            vset_destroy(tmp);
            // new var reference
            mu_var[mu_expr->idx] = old;
        }
        break;
    case MU_NU:
        {
            ensure_access(mu_var_man, mu_expr->idx);
            // backup old var reference
            vset_t old = mu_var[mu_expr->idx];
            result = mu_var[mu_expr->idx] = vset_create(domain, -1, NULL);
            vset_t tmp = vset_create(domain, -1, NULL);
            vset_copy(tmp, visited);
            do {
                vset_copy(mu_var[mu_expr->idx], tmp);
                vset_clear(tmp);
                tmp = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
                if (log_active(infoLong)) {
                    long n1, n2;
                    double e1, e2;
                    vset_count(mu_var[mu_expr->idx], &n1, &e1);
                    vset_count(tmp, &n2, &e2);
                    Warning(infoLong, "NU %s: %.0lf -> %.0lf",
                        SIget(env->idents,mu_expr->idx), e1, e2);
                }
            } while (!vset_equal(mu_var[mu_expr->idx], tmp));
            vset_destroy(tmp);
            // new var reference
            mu_var[mu_expr->idx] = old;
        }
        break;
    case MU_EQ:
    case MU_NEQ:
    case MU_LT:
    case MU_LEQ:
    case MU_GT:
    case MU_GEQ:
    case MU_EN: {
        result = vset_create(domain, -1, NULL);

        bitvector_t deps;
        bitvector_create(&deps, N);

        set_pins_semantics(model, mu_expr, env, &deps, NULL);
        rel_expr_info_t ctx;

        int vec[N];
        GBsetInitialState(model, vec);
        ctx.vec = vec;
        ctx.len = bitvector_n_high(&deps);
        int d[ctx.len];
        bitvector_high_bits(&deps, d);
        bitvector_free(&deps);
        ctx.deps = d;

        ctx.e = mu_expr;
        ctx.env = env;

        vset_t tmp = vset_create(domain, ctx.len, d);
        vset_project(tmp, visited);

        // count when verbose
        if (log_active(infoLong)) {
            double elem_count;
            vset_count(tmp, NULL, &elem_count);
            if (elem_count >= 10000.0 * REL_PERF) {
                const char* p = LTSminPrintExpr(mu_expr, env);
                Print(infoLong, "evaluating subformula %s for %.*g states.", p, DBL_DIG, elem_count);
            }
        }

        vset_t true_states = vset_create(domain, ctx.len, d);

        vset_update(true_states, tmp, rel_expr_cb, &ctx);

        vset_join(result, true_states, visited);
        vset_destroy(tmp);
        vset_destroy(true_states);
        break;
    }
    default:
        Abort("encountered unhandled mu operator");
    }
    return result;
}

/* Somewhat more clever mu-calculus algorithm. Vaguely inspired by Clarke/Grumberg/Peled.
 * Reuse previous value, unless a competing outermost fixpoint of contrary sign changed.
 * Static information is maintained in the sign- and deps- field of the mu-object.
 */
static vset_t
mu_rec(ltsmin_expr_t mu_expr, ltsmin_parse_env_t env, vset_t visited, mu_object_t muo, vset_t* mu_var) {

    vset_t result = NULL;
    switch(mu_expr->token) {
    case MU_TRUE:
        Warning(debug, "TRUE");

        result = vset_create(domain, -1, NULL);
        vset_copy(result, visited);
        return result;
    case MU_FALSE:
        Warning(debug, "FALSE");
        return vset_create(domain, -1, NULL);
    case MU_OR: { // OR
        Warning(debug, "OR");
        result = mu_rec(mu_expr->arg1, env, visited, muo, mu_var);
        vset_t mc = mu_rec(mu_expr->arg2, env, visited, muo, mu_var);
        vset_union(result, mc);
        Warning(debug, "OR OK");
        vset_destroy(mc);
    } break;
    case MU_AND: { // AND
        Warning(debug, "AND");
        result = mu_rec(mu_expr->arg1, env, visited, muo, mu_var);
        vset_t mc = mu_rec(mu_expr->arg2, env, visited, muo, mu_var);
        vset_intersect(result, mc);
        Warning(debug, "AND OK");
        vset_destroy(mc);
    } break;
    case MU_NOT: { // NEGATION
        Warning(debug, "NOT");
        result = vset_create(domain, -1, NULL);
        vset_copy(result, visited);
        vset_t mc = mu_rec(mu_expr->arg1, env, visited, muo, mu_var);
        vset_minus(result, mc);
        Warning(debug, "NOT OK");
        vset_destroy(mc);
    } break;
    case MU_EXIST: { // E
        Warning(debug, "EX");
        if (mu_expr->arg1->token == MU_NEXT) {
            vset_t temp = vset_create(domain, -1, NULL);
            result = vset_create(domain, -1, NULL);
            vset_t g = mu_rec(mu_expr->arg1->arg1, env, visited, muo, mu_var);

            for(int i=0;i<nGrps;i++){
                vset_prev(temp,g,group_next[i],visited);
                reduce(i, temp);
                vset_union(result,temp);
                vset_clear(temp);
            }
            vset_destroy(temp);
        } else {
            Abort("invalid operator following MU_EXIST, expecting MU_NEXT");
        }
        Warning(debug, "EX OK");
    } break;
    case MU_SVAR: {
        if (mu_expr->idx < N) { // state variable
            Abort("Unhandled MU_SVAR");
        } else { // state label
            result = vset_create(domain, -1, NULL);
            vset_join(result, visited, label_true[mu_expr->idx - N]);
        }
    } break;
    case MU_VAR:
        Warning(debug, "VAR %s", SIget(env->idents,mu_expr->idx));
        result = vset_create(domain, -1, NULL);
        vset_copy(result, mu_var[mu_expr->idx]);
        break;
    case MU_ALL:
        if (mu_expr->arg1->token == MU_NEXT) {
            // implemented as AX phi = ! EX ! phi

            Warning(debug, "AX");
            result = vset_create(domain, -1, NULL);
            vset_copy(result, visited);

            // compute ! phi
            vset_t notphi = vset_create(domain, -1, NULL);
            vset_copy(notphi, visited);
            vset_t phi = mu_rec(mu_expr->arg1->arg1, env, visited, muo, mu_var);
            vset_minus(notphi, phi);
            vset_destroy(phi);

            vset_t temp = vset_create(domain, -1, NULL);
            vset_t prev = vset_create(domain, -1, NULL);

            // EX !phi
            for(int i=0;i<nGrps;i++){
                vset_prev(temp,notphi,group_next[i],visited);
                reduce(i, temp);
                vset_union(prev,temp);
                vset_clear(temp);
            }
            vset_destroy(temp);

            // and negate result again
            vset_minus(result, prev);
            vset_destroy(prev);
            vset_destroy(notphi);
            Warning(debug, "AX OK");

        } else {
            Abort("invalid operator following MU_ALL, expecting MU_NEXT");
        }
        break;
    case MU_MU: case MU_NU:
        {   // continue at the value of last iteration
            int Z = mu_expr->idx;
            vset_t old = vset_create(domain, -1, NULL);
            result = vset_create(domain, -1, NULL);
            vset_copy(old,mu_var[Z]);
            do {
                vset_copy(result,mu_var[Z]);
                vset_copy(mu_var[Z],mu_rec(mu_expr->arg1, env, visited, muo, mu_var));
                if (log_active(infoLong) || peak_nodes) {
                    long n1, n2;
                    long double e1, e2;
                    const int d1 = vset_count_fn (result, &n1, &e1);
                    const int d2 = vset_count_fn (mu_var[Z], &n2, &e2);
                    Warning(infoLong, "%s %s: %.*Lg (%ld) -> %.*Lg (%ld)",
                        MU_NAME(muo->sign[Z]), SIget(env->idents,Z), d1, e1, n1, d2, e2, n2);

                    if (n1 > max_mu_count) max_mu_count = n1;
                    if (n2 > max_mu_count) max_mu_count = n1;
                }

                // reset dependent variables with opposite sign
                if (!vset_equal(result,mu_var[Z]))
                    for (int i=0;i<muo->nvars;i++) {
                        if (muo->deps[Z][i] && muo->sign[Z] != muo->sign[i]) {
                            Warning(debug, "%s resets %s",
                                SIget(env->idents,Z), SIget(env->idents,i));
                            if (muo->sign[i]==MU_MU) vset_clear(mu_var[i]);
                            if (muo->sign[i]==MU_NU) vset_copy(mu_var[i],visited);
                        }
                    }
            } while (!vset_equal(mu_var[Z], result));

            vset_destroy(old);
        }
        break;
    case MU_EQ:
    case MU_NEQ:
    case MU_LT:
    case MU_LEQ:
    case MU_GT:
    case MU_GEQ:
    case MU_EN: {
        Warning(debug, "EQ");
        result = vset_create(domain, -1, NULL);

        bitvector_t deps;
        bitvector_create(&deps, N);

        set_pins_semantics(model, mu_expr, env, &deps, NULL);
        rel_expr_info_t ctx;

        int vec[N];
        GBsetInitialState(model, vec);
        ctx.vec = vec;
        ctx.len = bitvector_n_high(&deps);
        int d[ctx.len];
        bitvector_high_bits(&deps, d);
        bitvector_free(&deps);
        ctx.deps = d;

        ctx.e = mu_expr;
        ctx.env = env;

        vset_t tmp = vset_create(domain, ctx.len, d);
        vset_project(tmp, visited);

        // count when verbose
        if (log_active(infoLong)) {
            double elem_count;
            vset_count(tmp, NULL, &elem_count);
            if (elem_count >= 10000.0 * REL_PERF) {
                const char* p = LTSminPrintExpr(mu_expr, env);
                Print(infoLong, "evaluating subformula %s for %.*g states.", p, DBL_DIG, elem_count);
            }
        }

        vset_t true_states = vset_create(domain, ctx.len, d);

        vset_update(true_states, tmp, rel_expr_cb, &ctx);

        vset_join(result, true_states, visited);
        vset_destroy(tmp);
        vset_destroy(true_states);
        break;
    }
    default:
        Abort("encountered unhandled mu operator");
    }
    return result;
}


vset_t
mu_compute_optimal(ltsmin_expr_t mu_expr, ltsmin_parse_env_t env, vset_t visited)
{
    int nvars = mu_optimize(&mu_expr,env);
    mu_object_t muo = mu_object(mu_expr,nvars);

    if (log_active(infoLong)) {
        const char s[] = "Normalizing mu-calculus formula: ";
        char buf[snprintf(NULL, 0, s) + 1];
        sprintf(buf, s);
        LTSminLogExpr(infoLong, buf, mu_expr, env);
    }

    // initialize mu/nu fixpoint variables at least/largest values
    vset_t* mu_var = (vset_t*)RTmalloc(sizeof(vset_t)*nvars);
    for (int i = 0 ; i < nvars ; i++) {
        if (muo->sign[i]==MU_MU) mu_var[i] = vset_create(domain,-1,NULL);
        else if (muo->sign[i]==MU_NU) {
            mu_var[i] = vset_create(domain,-1,NULL);
            vset_copy(mu_var[i],visited);
        } else Warning(info, "Gaps between fixpoint variables");
    }
    vset_t result = mu_rec(mu_expr,env,visited,muo,mu_var);
    // TODO: mu_object_destroy(muo);
    return result;
}


static array_manager_t* mu_var_mans = NULL;
static vset_t** mu_vars = NULL;

void
init_mu_calculus()
{
    int total = num_mu + num_ctl_star + num_ctl + num_ltl;
    if (total > 0) {
        mu_parse_env = (ltsmin_parse_env_t*) RTmalloc(sizeof(ltsmin_parse_env_t) * total);
        mu_exprs = (ltsmin_expr_t*) RTmalloc(sizeof(ltsmin_expr_t) * total);
        total = 0;
        for (int i = 0; i < num_mu; i++) {
            mu_parse_env[i] = LTSminParseEnvCreate();
            Warning(info, "parsing mu-calculus formula");
            mu_exprs[i] = mu_parse_file(mu_formulas[i], mu_parse_env[i], ltstype);
            if (log_active(infoLong)) {
                const char s[] = "Loaded and optimized mu-calculus formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[i], mu_parse_env[i]);
            }
        }
        total += num_mu;
        for (int i = 0; i < num_ctl_star; i++) {
            mu_parse_env[total + i] = LTSminParseEnvCreate();
            Warning(info, "parsing CTL* formula");
            ltsmin_expr_t ctl_star = ctl_parse_file(ctl_star_formulas[i], mu_parse_env[total + i], ltstype);
            Warning(info, "converting CTL* %s to mu-calculus", ctl_star_formulas[i]);
            mu_exprs[total + i] = ctl_star_to_mu(ctl_star);
            if (log_active(infoLong)) {
                const char s[] = "Converted CTL* to mu-calculus formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[total + i], mu_parse_env[total + i]);
            }
        }
        total += num_ctl_star;
        for (int i = 0; i < num_ctl; i++) {
            mu_parse_env[total + i] = LTSminParseEnvCreate();
            Warning(info, "parsing CTL formula");
            mu_exprs[total + i] = ctl_parse_file(ctl_formulas[i], mu_parse_env[total + i], ltstype);
            if (log_active(infoLong)) {
                const char s[] = "Loaded and optimized CTL formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[total + i], mu_parse_env[total + i]);
            }
            Warning(info, "converting CTL to mu-calculus...");
            mu_exprs[total + i] = ctl_to_mu(mu_exprs[total + i], mu_parse_env[total + i], ltstype);
            if (log_active(infoLong)) {
                const char s[] = "Converted CTL to mu-calculus formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[total + i], mu_parse_env[total + i]);
            }
        }
        total += num_ctl;
        for (int i = 0; i < num_ltl; i++) {
            mu_parse_env[total + i] = LTSminParseEnvCreate();
            Warning(info, "parsing LTL formula");
            ltsmin_expr_t ltl = ctl_parse_file(ltl_formulas[i], mu_parse_env[total + i], ltstype);
            Warning(info, "converting LTL %s to mu-calculus", ltl_formulas[i]);
            mu_exprs[total + i] = ltl_to_mu(ltl);
            if (log_active(infoLong)) {
                const char s[] = "Converted LTL to mu-calculus formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[total + i], mu_parse_env[total + i]);
            }
        }
        total += num_ltl;

        num_total = total;

        mu_var_mans = (array_manager_t*) RTmalloc(sizeof(array_manager_t) * num_total);
        mu_vars = (vset_t**) RTmalloc(sizeof(vset_t*) * num_total);

        for (int i = 0; i < num_total; i++) {
            // setup var manager
            mu_var_mans[i] = create_manager(65535);
            mu_vars[i] = NULL;
            ADD_ARRAY(mu_var_mans[i], mu_vars[i], vset_t);
        }
    }
}

#ifdef HAVE_SYLVAN
#define check_mu_go(v, i, s) CALL(check_mu_go, (v), (i), (s))
VOID_TASK_3(check_mu_go, vset_t, visited, int, i, int*, init)
#else
static void check_mu_go(vset_t visited, int i, int *init)
#endif
{
    vset_t x;
    if (mu_opt) {
        x = mu_compute_optimal(mu_exprs[i], mu_parse_env[i], visited);
    } else {
        x = mu_compute(mu_exprs[i], mu_parse_env[i], visited, mu_vars[i], mu_var_mans[i]);
    }
    if (x != NULL) {
        char* formula = NULL;
        // recall: mu-formulas, ctl-star formulas, ctl-formulas, ltl-formulas
        if (i < num_mu) {
            formula = mu_formulas[i];
        } else if (i < num_mu + num_ctl_star) {
            formula = ctl_star_formulas[i - num_mu];
        } else if (i < num_mu + num_ctl_star + num_ctl) {
            formula = ctl_formulas[i - num_mu - num_ctl_star];
        } else if (i < num_mu + num_ctl_star + num_ltl) {
            formula = ltl_formulas[i - num_mu - num_ctl_star - num_ctl];
        } else {
            Warning(lerror, "Number of formulas doesn't match (%d+%d+%d+%d)", num_mu, num_ctl_star, num_ctl, num_ltl);
        }

        if (log_active(infoLong)) {
            double e_count;
            vset_count(x, NULL, &e_count);
            Warning(infoLong, "Formula %s holds for %.*g states,", formula, DBL_DIG, e_count);
        }

        Warning(info, "Formula %s %s for the initial state", formula, vset_member(x, init) ? "holds" : "does not hold");
        vset_destroy(x);
    }
}

void
check_mu(vset_t visited, int* init)
{
    if (num_total > 0) {
        Print(infoLong, "Starting mu-calculus model checking.");
        learn_labels(visited);
        for (int i = 0; i < num_total; i++) {
            LACE_ME;
            check_mu_go(visited, i, init);
        }
    }
}

#ifdef HAVE_SYLVAN
void
check_mu_par(vset_t visited, int* init)
{
    LACE_ME;
    if (num_total > 0) {
        Print(infoLong, "Starting parallel mu-calculus model checking.");
        learn_labels_par(visited);
        for (int i = 0; i < num_total; i++) {
            SPAWN(check_mu_go, visited, i, init);
        }

        for (int i = 0; i < num_total; i++) SYNC(check_mu_go);
    }
}
#endif

