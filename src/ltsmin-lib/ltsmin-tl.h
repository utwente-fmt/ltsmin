#ifndef LTSMIN_TL_H
#define LTSMIN_TL_H

/* Definitions for a simple predicate language & temporal logics */

#include <dm/bitvector.h>
#include <ltsmin-lib/lts-type.h>
#include <ltsmin-lib/ltsmin-grammar.h>
#include <ltsmin-lib/ltsmin-syntax.h>

/* Predicate language */
typedef enum {
    PRED_SVAR  = SVAR,
    PRED_EVAR  = EVAR,
    PRED_NUM   = INT,
    PRED_CHUNK = CHUNK,
    PRED_VAR   = VAR,
    PRED_LT    = S_LT,
    PRED_LEQ   = S_LEQ,
    PRED_GT    = S_GT,
    PRED_GEQ   = S_GEQ,
    PRED_EQ    = S_EQ,
    PRED_NEQ   = S_NEQ,
    PRED_TRUE  = S_TRUE,
    PRED_FALSE = S_FALSE,
    PRED_MAYBE = S_MAYBE,
    PRED_NOT   = S_NOT,
    PRED_OR    = S_OR,
    PRED_AND   = S_AND,
    PRED_EQUIV = S_EQUIV,
    PRED_IMPLY = S_IMPLY,
    PRED_MULT  = S_MULT,
    PRED_DIV   = S_DIV,
    PRED_REM   = S_REM,
    PRED_ADD   = S_ADD,
    PRED_SUB   = S_SUB,
    PRED_EN    = S_EN,
} Pred;

extern ltsmin_expr_t pred_parse_file(const char *,ltsmin_parse_env_t,lts_type_t);

/* linear temporal logic */
typedef enum {
    LTL_SVAR  = SVAR,
    LTL_EVAR  = EVAR,
    LTL_NUM   = INT,
    LTL_CHUNK = CHUNK,
    LTL_VAR   = VAR,
    LTL_TRUE  = PRED_TRUE,
    LTL_FALSE = PRED_FALSE,
    LTL_MAYBE = PRED_MAYBE,
    LTL_LT    = PRED_LT,
    LTL_LEQ   = PRED_LEQ,
    LTL_GT    = PRED_GT,
    LTL_GEQ   = PRED_GEQ,
    LTL_NOT   = PRED_NOT,
    LTL_EQ    = PRED_EQ,
    LTL_NEQ   = PRED_NEQ,
    LTL_OR    = PRED_OR,
    LTL_AND   = PRED_AND,
    LTL_EQUIV = PRED_EQUIV,
    LTL_IMPLY = PRED_IMPLY,
    LTL_MULT  = PRED_MULT,
    LTL_DIV   = PRED_DIV,
    LTL_REM   = PRED_REM,
    LTL_ADD   = PRED_ADD,
    LTL_SUB   = PRED_SUB,
    LTL_EN    = PRED_EN,

    LTL_FUTURE= TOKEN_USER,
    LTL_GLOBALLY,
    LTL_RELEASE,
    LTL_WEAK_UNTIL,
    LTL_STRONG_RELEASE,
    LTL_NEXT,
    LTL_UNTIL
} LTL;

extern ltsmin_expr_t ltl_parse_file(const char *,ltsmin_parse_env_t,lts_type_t);

/* Computation Tree logic */

typedef enum {
    CTL_SVAR  = SVAR,
    CTL_EVAR  = EVAR,
    CTL_NUM   = INT,
    CTL_CHUNK = CHUNK,
    CTL_VAR   = VAR,
    CTL_TRUE  = PRED_TRUE,
    CTL_FALSE = PRED_FALSE,
    CTL_MAYBE = PRED_MAYBE,
    CTL_LT    = PRED_LT,
    CTL_LEQ   = PRED_LEQ,
    CTL_GT    = PRED_GT,
    CTL_GEQ   = PRED_GEQ,
    CTL_NOT   = PRED_NOT,
    CTL_EQ    = PRED_EQ,
    CTL_NEQ   = PRED_NEQ,
    CTL_OR    = PRED_OR,
    CTL_AND   = PRED_AND,
    CTL_EQUIV = PRED_EQUIV,
    CTL_IMPLY = PRED_IMPLY,
    CTL_MULT  = PRED_MULT,
    CTL_DIV   = PRED_DIV,
    CTL_REM   = PRED_REM,
    CTL_ADD   = PRED_ADD,
    CTL_SUB   = PRED_SUB,
    CTL_EN    = PRED_EN,

    CTL_NEXT  = TOKEN_USER,
    CTL_UNTIL,
    CTL_FUTURE,
    CTL_GLOBALLY,
    CTL_EXIST,
    CTL_ALL
} CTL;

extern ltsmin_expr_t ctl_parse_file(const char *,ltsmin_parse_env_t,lts_type_t);


/* mu-alculus */
typedef enum {
    MU_SVAR                 = SVAR,
    MU_EVAR                 = EVAR,
    MU_NUM                  = INT,
    MU_CHUNK                = CHUNK,
    MU_VAR                  = VAR,
    MU_LT                   = PRED_LT,
    MU_LEQ                  = PRED_LEQ,
    MU_GT                   = PRED_GT,
    MU_GEQ                  = PRED_GEQ,
    MU_AND                  = PRED_AND,
    MU_OR                   = PRED_OR,
    MU_EQ                   = PRED_EQ,
    MU_NEQ                  = PRED_NEQ,
    MU_TRUE                 = PRED_TRUE,
    MU_FALSE                = PRED_FALSE,
    MU_MAYBE                = PRED_MAYBE,
    MU_NOT                  = PRED_NOT,
    MU_MULT                 = PRED_MULT,
    MU_DIV                  = PRED_DIV,
    MU_REM                  = PRED_REM,
    MU_ADD                  = PRED_ADD,
    MU_SUB                  = PRED_SUB,
    MU_EN                   = PRED_EN,

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

extern const char  *PRED_NAME(Pred pred);
extern const char  *LTL_NAME(LTL ltl);
extern const char  *CTL_NAME(CTL ctl);
extern const char  *MU_NAME(MU mu);

extern stream_t read_formula(const char *file);

extern ltsmin_expr_t mu_parse_file(const char *,ltsmin_parse_env_t,lts_type_t);

/* Conversion */
extern ltsmin_expr_t ltl_to_ctl_star(ltsmin_expr_t);
extern ltsmin_expr_t ltl_normalize(ltsmin_expr_t);
extern ltsmin_expr_t ctl_to_ctl_star(ltsmin_expr_t);
extern ltsmin_expr_t ctl_normalize(ltsmin_expr_t);
extern ltsmin_expr_t ctl_star_to_pnf(ltsmin_expr_t);
extern ltsmin_expr_t ctl_star_to_mu(ltsmin_expr_t);
extern ltsmin_expr_t ctl_to_mu(ltsmin_expr_t, ltsmin_parse_env_t, lts_type_t);
extern ltsmin_expr_t ltl_to_mu(ltsmin_expr_t);
extern char* ltsmin_expr_print_ctl(ltsmin_expr_t, char*);
extern char* ltsmin_expr_print_mu(ltsmin_expr_t, char*);

// optimizes expression: negations inside, rename variables apart
// return the number of (different) variables
extern int mu_optimize(ltsmin_expr_t*, const ltsmin_parse_env_t);

typedef struct mu_object_s *mu_object_t;
struct mu_object_s {
    int nvars;     // nr. of mu-calculus variables
    int *sign;     // sign of variable n (MU_MU or MU_NU)
    int **deps;    // deps[i][j] is true if mu/nu Zi (... mu/nu Zj ( ... Zi ...) ...)
    int *stack;   // used to store local context in recursion
    int top;       // top of the stack
};

extern mu_object_t mu_object(ltsmin_expr_t in, int nvars);

/********** TABLEAUX FOR THE TRANSLATION FROM CTL-STAR TO MU_CALCULUS ********/

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
