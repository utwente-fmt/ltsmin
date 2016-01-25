#ifndef LTL2BA_LEX_HELPER_DEF
#define LTL2BA_LEX_HELPER_DEF


#include <hre/config.h>
#include <stdlib.h>
#include <ctype.h>

#include <hre/user.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <ltsmin-lib/ltsmin-buchi.h>

typedef struct ltsmin_expr_list_t {
    char* text;
    ltsmin_expr_t expr;
    struct ltsmin_expr_list* next;
} ltsmin_expr_list_t;


typedef struct ltsmin_lin_expr {
    int size;
    int count;
    ltsmin_expr_t lin_expr[];
} ltsmin_lin_expr_t;

ltsmin_expr_t ltsmin_expr_lookup(ltsmin_expr_t e, char* text, ltsmin_expr_list_t **le_list);
void linearize_ltsmin_expr(ltsmin_expr_t e, ltsmin_lin_expr_t **le);


#endif // LTL2BA_LEX_HELPER_DEF