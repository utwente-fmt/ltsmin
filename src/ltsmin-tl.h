#ifndef LTSMIN_TL_H
#define LTSMIN_TL_H

/* Definitions for a simple predicate language & temporal logics */

#include <greybox.h>
#include <lts-type.h>
#include <ltsmin-grammar.h>
#include <ltsmin-syntax.h>

/* Predicate language */
typedef enum {
    PRED_SVAR  = SVAR,
    PRED_EVAR  = EVAR,
    PRED_NUM   = INT,
    PRED_CHUNK = CHUNK,
    PRED_VAR   = VAR,
    PRED_EQ    = S_EQ,
    PRED_TRUE  = S_TRUE,
    PRED_FALSE = S_FALSE,
    PRED_NOT   = S_NOT,
    PRED_OR    = S_OR,
    PRED_AND   = S_AND,
    PRED_EQUIV = S_EQUIV,
    PRED_IMPLY = S_IMPLY
} Pred;

extern ltsmin_expr_t pred_parse_file(model_t model,const char *file);

/* linear temporal logic */
typedef enum {
    LTL_SVAR  = SVAR,
    LTL_EVAR  = EVAR,
    LTL_NUM   = INT,
    LTL_CHUNK = CHUNK,
    LTL_VAR   = VAR,
    LTL_TRUE  = PRED_TRUE,
    LTL_FALSE = PRED_FALSE,
    LTL_NOT   = PRED_NOT,
    LTL_EQ    = PRED_EQ,
    LTL_OR    = PRED_OR,
    LTL_AND   = PRED_AND,
    LTL_EQUIV = PRED_EQUIV,
    LTL_IMPLY = PRED_IMPLY,

    LTL_FUTURE= TOKEN_USER,
    LTL_GLOBALLY,
    LTL_RELEASE,
    LTL_WEAK_UNTIL,
    LTL_STRONG_RELEASE,
    LTL_NEXT,
    LTL_UNTIL
} LTL;

extern ltsmin_expr_t ltl_parse_file(model_t model,const char *file);

/* Computation Tree logic */

typedef enum {
    CTL_SVAR  = SVAR,
    CTL_EVAR  = EVAR,
    CTL_NUM   = INT,
    CTL_CHUNK = CHUNK,
    CTL_VAR   = VAR,
    CTL_TRUE  = PRED_TRUE,
    CTL_FALSE = PRED_FALSE,
    CTL_NOT   = PRED_NOT,
    CTL_EQ    = PRED_EQ,
    CTL_OR    = PRED_OR,
    CTL_AND   = PRED_AND,
    CTL_EQUIV = PRED_EQUIV,
    CTL_IMPLY = PRED_IMPLY,

    CTL_NEXT  = TOKEN_USER,
    CTL_UNTIL,
    CTL_FUTURE,
    CTL_GLOBALLY,
    CTL_EXIST,
    CTL_ALL
} CTL;

extern ltsmin_expr_t ctl_parse_file(model_t model,const char *file);


/* mu-alculus */
typedef enum {
    MU_SVAR                 = SVAR,
    MU_EVAR                 = EVAR,
    MU_NUM                  = INT,
    MU_CHUNK                = CHUNK,
    MU_VAR                  = VAR,
    MU_AND                  = PRED_AND,
    MU_OR                   = PRED_OR,
    MU_EQ                   = PRED_EQ,
    MU_TRUE                 = PRED_TRUE,
    MU_FALSE                = PRED_FALSE,
    MU_NOT                  = PRED_NOT,

    MU_EDGE_EXIST           = EDGE_EXIST,
    MU_EDGE_ALL             = EDGE_ALL,
    MU_EDGE_EXIST_LEFT      = TOKEN_EDGE_EXIST_LEFT,
    MU_EDGE_EXIST_RIGHT     = TOKEN_EDGE_EXIST_RIGHT,
    MU_EDGE_ALL_LEFT        = TOKEN_EDGE_ALL_LEFT,
    MU_EDGE_ALL_RIGHT       = TOKEN_EDGE_ALL_RIGHT,
    MU_MU                   = TOKEN_MU_SYM,
    MU_NU                   = TOKEN_NU_SYM,
    MU_NEXT                 = TOKEN_USER,
    MU_EXIST,
    MU_ALL
} MU;

extern ltsmin_expr_t mu_parse_file(model_t model,const char *file);

/* Conversion */
extern ltsmin_expr_t ltl_to_ctl_star(ltsmin_expr_t);
extern ltsmin_expr_t ltl_normalize(ltsmin_expr_t);
extern ltsmin_expr_t ctl_to_ctl_star(ltsmin_expr_t);
extern ltsmin_expr_t ctl_normalize(ltsmin_expr_t);
extern ltsmin_expr_t ctl_star_to_pnf(ltsmin_expr_t);
extern ltsmin_expr_t ctl_star_to_mu(ltsmin_expr_t);
extern char* ltsmin_expr_print_ltl(ltsmin_expr_t, char*);
extern char* ltsmin_expr_print_ctl(ltsmin_expr_t, char*);
extern char* ltsmin_expr_print_mu(ltsmin_expr_t, char*);
extern int ltsmin_expr_lookup_values(ltsmin_expr_t ltl,ltsmin_parse_env_t env,model_t model);

/* semantics */
extern void mark_predicate(ltsmin_expr_t e, matrix_t *m); /* mark touched variables */
extern void mark_visible(ltsmin_expr_t e, matrix_t *m, int* group_visibility);  /* mark touched groups */
static inline int /* evaluate predicate on state */
eval_predicate(ltsmin_expr_t e, transition_info_t* ti, int* state)
{
    switch (e->token) {
        case PRED_TRUE:
            return 1;
        case PRED_FALSE:
            return 0;
        case PRED_NUM:
            return e->idx;
        case PRED_SVAR:
            return state[e->idx];
        case PRED_EQ:
            return (eval_predicate(e->arg1, ti, state) == eval_predicate(e->arg2, ti, state));
        case PRED_VAR:
            if (-1 == e->num)
                Abort("unbound variable in predicate expression");
            return e->num;
        default: {
            char buf[1024];
            ltsmin_expr_print_ltl(e, buf);
            Abort("unhandled predicate expression: %s", buf);
        }
    }
    return 0;
    (void)ti;
}

/* ctl* to mu conversion
 *
 * The ctl* to mu conversion is the algorithm of Mad Dams:
 * Translating CTL* into the model mu-calculus,
 * LFCS report ECS-LFCS-90-123
 */

/*
 * The tableaux holds two tables, expressions and nodes. These are very simple
 * hash tables. The both table serve to identify companions (equal expressions).
 * The naming is as follows. A tableaux structure also holds the syntax tree. This
 * structure holds the actual tree that is build up.
 * A syntax tree looks like this
 *             node
 * label ---------------
 *       left      right
 *
 * Where left/right are of the type syntax tree or NULL. A node is either an expression
 * or a set of expressions quantified over all paths (A), or over at least one path (E).
 * The node holds all expressions in an expression list, which is a list ordered on the
 * hash value of the expressions.
 */

typedef struct tableaux_table_item
{
    uint32_t hash;
    void*    data;
} tableaux_table_item_t;

typedef struct tableaux_table
{
    int size;
    int count;
    int (*fn_eq)(void*,void*);
    tableaux_table_item_t *table;
} tableaux_table_t;

typedef enum {NODE_NONE, NODE_ALL, NODE_EXIST} tableaux_node_quantifier_t;

typedef struct tableaux_expr_list
{
    ltsmin_expr_t expr;
    struct tableaux_expr_list *next;
    // generating relation phi \in PHI
    struct tableaux_expr_list *generating_expr[2];
    int in_path;
} tableaux_expr_list_t;

typedef struct tableaux_node
{
    uint32_t hash;
    tableaux_node_quantifier_t quantifier;
    tableaux_expr_list_t *expr_list;
} tableaux_node_t;

typedef enum {TABLEAUX_TERMINAL, TABLEAUX_PRETERMINAL, TABLEAUX_IDENTITY, TABLEAUX_NOT, TABLEAUX_AND, TABLEAUX_OR,
              TABLEAUX_ALL_NEXT, TABLEAUX_EXIST_NEXT} syntax_tree_label_t;

typedef enum {ST_LEFT, ST_RIGHT} syntax_tree_branch_t;

typedef struct syntax_tree
{
    struct syntax_tree* parent;    // for PHI relation
    tableaux_node_t*                        node;
    syntax_tree_label_t  label;    // -----------------
    struct syntax_tree* branch[2]; // ST_LEFT  ST_RIGHT

    // information to find mu-/nu-paths
    struct syntax_tree* companion; // do i need this?
    int is_companion;
    int path_var[2];   // 1: there is a mu-/nu-path, 0: there is no mu-/nu-path
    // environment S(PHI) -> phi relation
    tableaux_expr_list_t *S_phi;

} syntax_tree_t;

typedef struct tableaux
{
    tableaux_table_t expressions;
    tableaux_table_t nodes;
    syntax_tree_t root;
    int used_var;
} tableaux_t;

typedef enum {MU_PATH, NU_PATH} tableaux_path_t;

tableaux_t             *tableaux_create();
void                    tableaux_destroy(tableaux_t*);
void                    tableaux_table_grow(tableaux_table_t *t);
void                    tableaux_table_add(tableaux_table_t*, uint32_t, void*);
void                   *tableaux_table_lookup(tableaux_table_t*, uint32_t, void*);
#endif
