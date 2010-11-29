#ifndef LTSMIN_BUCHI_H
#define LTSMIN_BUCHI_H

#include <ltsmin-syntax.h>

typedef struct ltsmin_buchi_transition {
//    ltsmin_expr_t condition;
    int* pos; // * copy sym_table to point to ltsmin_expressions? */
    int* neg;
    int to_state;
} ltsmin_buchi_transition_t;

typedef struct ltsmin_buchi_state {
    int accept;
    int transition_count;
    ltsmin_buchi_transition_t transitions[];
} ltsmin_buchi_state_t;

typedef struct ltsmin_buchi {
    int predicate_count;
    ltsmin_expr_t* predicates;
    int state_count;
    ltsmin_buchi_state_t* states[]; /* 0 to n*/
} ltsmin_buchi_t;

#endif
