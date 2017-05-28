#ifndef ALG_MU_H
#define ALG_MU_H


#include <vset-lib/vector_set.h>


/* Naive textbook mu-calculus algorithm
 * Taken from:
 * Model Checking and the mu-calculus, E. Allen Emerson
 * DIMACS Series in Discrete Mathematics, 1997 - Citeseer
 */
extern vset_t mu_compute(ltsmin_expr_t mu_expr, ltsmin_parse_env_t env,
                         vset_t visited, vset_t* mu_var, array_manager_t mu_var_man);

extern vset_t mu_compute_optimal(ltsmin_expr_t mu_expr, ltsmin_parse_env_t env,
                                 vset_t visited);

extern void init_mu_calculus();

extern void check_mu(vset_t visited, int* init);

#ifdef HAVE_SYLVAN
extern void check_mu_par(vset_t visited, int* init);
#else
#define check_mu_par(v,i)
#endif


#define CHECK_MU(s, i) \
    if (mu_par) check_mu_par((s), (i)); \
    else check_mu((s), (i));

#endif //ALG_REACH_H
