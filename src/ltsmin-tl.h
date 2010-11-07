#ifndef LTSMIN_TL_H
#define LTSMIN_TL_H

/* Definitions for temporal logics */

#include <ltsmin-syntax.h>
#include <lts-type.h>
#include <ltsmin-grammar.h>

/* linear temporal logic */
typedef enum {
    LTL_SVAR  = SVAR,
    LTL_EVAR  = EVAR,
    LTL_NUM   = INT,
    LTL_CHUNK = CHUNK,
    LTL_VAR   = VAR,
    LTL_EQ    = TOKEN_USER,
    LTL_TRUE,
    LTL_OR,
    LTL_NOT,
    LTL_NEXT,
    LTL_UNTIL,

    /* sugar */
    LTL_FALSE,
    LTL_AND,
    LTL_EQUIV,
    LTL_IMPLY,
    LTL_FUTURE,
    LTL_GLOBALLY,
    LTL_RELEASE,
    LTL_WEAK_UNTIL,
    LTL_STRONG_RELEASE
} LTL;

extern ltsmin_expr_t ltl_parse_file(lts_type_t ltstype,const char *file);

/* Computation Tree logic */

typedef enum {
    CTL_SVAR  = SVAR,
    CTL_EVAR  = EVAR,
    CTL_NUM   = INT,
    CTL_CHUNK = CHUNK,
    CTL_VAR   = VAR,
    CTL_EQ    = TOKEN_USER,
    CTL_TRUE,
    CTL_OR,
    CTL_NOT,
    CTL_NEXT,
    CTL_UNTIL,

    CTL_FALSE,
    CTL_AND,
    CTL_EQUIV,
    CTL_IMPLY,
    CTL_FUTURE,
    CTL_GLOBALLY,
    CTL_EXIST,
    CTL_ALL
} CTL;

extern ltsmin_expr_t ctl_parse_file(lts_type_t ltstype,const char *file);


/* mu-alculus */
typedef enum {
    MU_SVAR                 = SVAR,
    MU_EVAR                 = EVAR,
    MU_NUM                  = INT,
    MU_CHUNK                = CHUNK,
    MU_VAR                  = VAR,
    MU_EDGE_EXIST           = EDGE_EXIST,
    MU_EDGE_ALL             = EDGE_ALL,
    MU_EDGE_EXIST_LEFT      = TOKEN_EDGE_EXIST_LEFT,
    MU_EDGE_EXIST_RIGHT     = TOKEN_EDGE_EXIST_RIGHT,
    MU_EDGE_ALL_LEFT        = TOKEN_EDGE_ALL_LEFT,
    MU_EDGE_ALL_RIGHT       = TOKEN_EDGE_ALL_RIGHT,
    MU_MU                   = TOKEN_MU_SYM,
    MU_NU                   = TOKEN_NU_SYM,
    MU_AND                  = TOKEN_USER,
    MU_OR,
    MU_EQ,
    MU_TRUE,
    MU_FALSE,
    MU_NOT,
    MU_NEXT,
    MU_EXIST,
    MU_ALL
} MU;

extern ltsmin_expr_t mu_parse_file(lts_type_t ltstype,const char *file);

/* Conversion */
extern ltsmin_expr_t ltl_to_ctl_star(ltsmin_expr_t);
extern ltsmin_expr_t ltl_normalize(ltsmin_expr_t);
extern ltsmin_expr_t ctl_to_ctl_star(ltsmin_expr_t);
extern ltsmin_expr_t ctl_normalize(ltsmin_expr_t);
extern ltsmin_expr_t ctl_star_to_pnf(ltsmin_expr_t);
extern ltsmin_expr_t ctl_star_to_mu(ltsmin_expr_t);

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
