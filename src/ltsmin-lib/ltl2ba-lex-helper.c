


#include <ltsmin-lib/ltl2ba-lex-helper.h>

#define LTL_LPAR ((void*)0x01)
#define LTL_RPAR ((void*)0x02)

ltsmin_expr_t
ltsmin_expr_lookup(ltsmin_expr_t e, char *text, ltsmin_expr_list_t **le_list)
{
    // lookup expression
    ltsmin_expr_list_t **pp_le_list = le_list;
    while(*pp_le_list != NULL) {
        if (e) {
            // compare on expression
            if ((*pp_le_list)->expr->hash == e->hash) {
                if (LTSminExprEq((*pp_le_list)->expr, e)) {
                    return (*pp_le_list)->expr;
                }
            }
        } else {
            // compare on text
            if (strcmp((*pp_le_list)->text, text) == 0)
                return (*pp_le_list)->expr;
        }
        pp_le_list = (ltsmin_expr_list_t**)&((*pp_le_list)->next);
    }
    // alloc room for this predicate expression
    *pp_le_list = (ltsmin_expr_list_t*) RTmalloc(sizeof(ltsmin_expr_list_t));
    (*pp_le_list)->text = strdup(text);
    (*pp_le_list)->expr = e;
    Debug ("LTL Symbol table: record expression %p as '%s'", e, text);
    (*pp_le_list)->next = NULL;
    return e;
}

void
add_lin_expr(ltsmin_expr_t e, ltsmin_lin_expr_t **le)
{
    // fill le_expr
    if ((*le)->count >= (*le)->size) {
        (*le)->size *= 2;
        *le = RTrealloc(*le, sizeof(ltsmin_lin_expr_t) + (*le)->size * sizeof(ltsmin_expr_t));
    }
    (*le)->lin_expr[(*le)->count++] = e;
}

void
linearize_ltsmin_expr(ltsmin_expr_t e, ltsmin_lin_expr_t **le)
{
    // quick fix
    switch (e->token) {
        case LTL_EQ:
        case LTL_NEQ:
        case LTL_LT:
        case LTL_LEQ:
        case LTL_GT:
        case LTL_GEQ:
        case LTL_MULT:
        case LTL_DIV:
        case LTL_REM:
        case LTL_ADD:
        case LTL_SUB: {
            add_lin_expr(e, le);
            return;
        }
    }

    // add left part of binary op first
    if (e->node_type == BINARY_OP) {
        add_lin_expr(LTL_LPAR, le);
        linearize_ltsmin_expr(e->arg1, le);
    }
    // add expr
    add_lin_expr(e, le);

    // linearization order
    if (e->node_type == UNARY_OP) {
        add_lin_expr(LTL_LPAR, le);
        linearize_ltsmin_expr(e->arg1, le);
        add_lin_expr(LTL_RPAR, le);
    } else if (e->node_type == BINARY_OP) {
        linearize_ltsmin_expr(e->arg2, le);
        add_lin_expr(LTL_RPAR, le);
    }
}
