#ifndef LTSMIN_BUCHI_H
#define LTSMIN_BUCHI_H

#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-parse-env.h>

typedef struct ltsmin_buchi_transition {
//    ltsmin_expr_t condition;
    int *pos; // * copy sym_table to point to ltsmin_expressions? */
    int *neg;
    int to_state;
    uint32_t acc_set; // HOA acceptance marks
    int index;  // A global unique dense trans index in the LTL buchi automaton
} ltsmin_buchi_transition_t;

typedef struct ltsmin_buchi_state {
    uint32_t accept; // Also used for HOA acceptance
    int transition_count;
    ltsmin_buchi_transition_t transitions[];
} ltsmin_buchi_state_t;

typedef struct rabin_pair {
    uint32_t fin_set; // bitmarking F_i for a generalized Rabin pair
    uint32_t inf_set; // bitmarking I_i^1..I_i^j for a generalized Rabin pair
} rabin_pair_t;

typedef struct rabin_pairs {
    int n_pairs; // number of pairs for the generalized Rabin automata
    rabin_pair_t pairs[]; // the generalized Rabin pairs
} rabin_t;

typedef struct ltsmin_buchi {
    uint32_t acceptance_set; // HOA acceptance (0 for standard BA)
    int predicate_count;
    ltsmin_expr_t *predicates;
    uint32_t edge_predicates; // bitset, marks edge var predicates true
    int state_count;
    int trans_count;
    ltsmin_parse_env_t env;
    rabin_t *rabin; // for generalized Rabin acceptance
    ltsmin_buchi_state_t* states[]; /* 0 to n*/
} ltsmin_buchi_t;

#endif
