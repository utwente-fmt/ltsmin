/***** ltl2ba : lex.c *****/

/* Written by Denis Oddoux, LIAFA, France                                 */
/* Copyright (c) 2001  Denis Oddoux                                       */
/* Modified by Paul Gastin, LSV, France                                   */
/* Copyright (c) 2007  Paul Gastin                                        */
/*                                                                        */
/* This program is free software; you can redistribute it and/or modify   */
/* it under the terms of the GNU General Public License as published by   */
/* the Free Software Foundation; either version 2 of the License, or      */
/* (at your option) any later version.                                    */
/*                                                                        */
/* This program is distributed in the hope that it will be useful,        */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of         */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          */
/* GNU General Public License for more details.                           */
/*                                                                        */
/* You should have received a copy of the GNU General Public License      */
/* along with this program; if not, write to the Free Software            */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA*/
/*                                                                        */
/* Based on the translation algorithm by Gastin and Oddoux,               */
/* presented at the 13th International Conference on Computer Aided       */
/* Verification, CAV 2001, Paris, France.                                 */
/* Proceedings - LNCS 2102, pp. 53-65                                     */
/*                                                                        */
/* Send bug-reports and/or questions to Paul Gastin                       */
/* http://www.lsv.ens-cachan.fr/~gastin                                   */
/*                                                                        */
/* Some of the code in this file was taken from the Spin software         */
/* Written by Gerard J. Holzmann, Bell Laboratories, U.S.A.               */
#include <hre/config.h>
#include <stdlib.h>
#include <ctype.h>

// Should be first, otherwise LTSmin print macros are in its the way
#include <ltl2ba.h>
#undef Debug
#include <hre/user.h>
#include <ltsmin-lib/ltl2ba-lex.h>
#include <ltsmin-lib/ltl2ba-lex-helper.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <ltsmin-lib/ltsmin-buchi.h>


static ltsmin_expr_list_t *le_list = NULL;
static ltsmin_lin_expr_t *le;
static int le_at;

static int  tl_lex(void);

extern YYSTYPE  tl_yylval;
char    yytext[2048];

#define Token(y)        tl_yylval = tl_nn(y,ZN,ZN); append_uform(yytext); \
    Debug ("LTL Lexer: passing token '%s' to LTL2BA", yytext); return y

#define LTL_LPAR ((void*)0x01)
#define LTL_RPAR ((void*)0x02)

static int
tl_lex(void)
{
    if (le_at >= le->count) { sprintf(yytext, ";"); Token(';'); }
    ltsmin_expr_t e = le->lin_expr[le_at++];
    if (e == LTL_LPAR) { sprintf(yytext,"("); Token('('); }
    if (e == LTL_RPAR) { sprintf(yytext,")"); Token(')'); }

    switch(e->token) {
        case LTL_TRUE:
            sprintf(yytext, "true");
            Token(TRUE);
        case LTL_FALSE:
            sprintf(yytext, "false");
            Token(FALSE);
        case LTL_AND:
            sprintf(yytext, "&&");
            Token(AND);
        case LTL_OR:
            sprintf(yytext, "||");
            Token(OR);
        case LTL_NOT:
            sprintf(yytext, "!");
            Token(NOT);
        case LTL_FUTURE:
            sprintf(yytext, "<>");
            Token(EVENTUALLY);
        case LTL_GLOBALLY:
            sprintf(yytext, "[]");
            Token(ALWAYS);
        case LTL_UNTIL:
            sprintf(yytext, "U");
            Token(U_OPER);
        case LTL_RELEASE:
            sprintf(yytext, "V");
            Token(V_OPER);
#ifdef NXT
        case LTL_NEXT:
            sprintf(yytext, "X");
            Token(NEXT);
#endif
        case LTL_EQUIV:
            sprintf(yytext, "<->");
            Token(EQUIV);
        case LTL_IMPLY:
            sprintf(yytext, "->");
            Token(IMPLIES);
        case LTL_EQ:
        case LTL_SVAR:
        case LTL_VAR:
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
            ltsmin_expr_print_ltl(e, yytext);
            /*ltsmin_expr_t ne = */ltsmin_expr_lookup(e, yytext, &le_list);

            tl_yylval = tl_nn(PREDICATE,ZN,ZN);
            tl_yylval->sym = tl_lookup(yytext);
            append_uform(yytext);
            }
            Debug ("LTL Lexer: passing token '%s' to LTL2BA", yytext);
            return PREDICATE;
        default:
            Abort("unhandled LTL_TOKEN: %s\n", LTL_NAME(e->token));
            break;
    }
    tl_yyerror("expected something...");
    return 0;
}

/* ltsmin extension for passing an ltsmin-expression to ltl2ba */
void
ltsmin_ltl2ba(ltsmin_expr_t e)
{
    ltl2ba_init();
#ifdef LTSMIN_DEBUG
    tl_verbose = log_active (infoLong);
#endif
    tl_yylex = tl_lex;
    set_uform("");
    const int le_size = 64;
    // linearized expression
    le = RTmalloc(sizeof(ltsmin_lin_expr_t) + le_size * sizeof(ltsmin_expr_t));
    le->size = le_size;
    le->count = 0;

    linearize_ltsmin_expr(e, &le);

    // print linearized expression for debugging:
    /*for(int i=0; i < le->count; i++) {
        if (le->lin_expr[i] == LTL_LPAR) {
            printf("par (\n");
        } else if (le->lin_expr[i] == LTL_RPAR) {
            printf("par )\n");
        } else {
            printf("token %d, idx %d\n", le->lin_expr[i]->token, le->lin_expr[i]->idx);
        }
    }*/

    le_at = 0;

    // now start parsing the expression
    tl_parse();
}

// XXX make proper interface in ltl2ba
extern int sym_size, n_sym, sym_id;
extern char **sym_table;

ltsmin_buchi_t*
ltsmin_buchi()
{
    ltsmin_buchi_t* res = NULL;
    int     accept = buchi_accept();
    BState *bstates = buchi_bstates();
    BTrans *t;
    BState *s;
    if(bstates->nxt == bstates) { /* empty automaton */
        return NULL;
    }
    if(bstates->nxt->nxt == bstates && bstates->nxt->id == 0) { /* true */
        return NULL;
    }

    // mapping map_id[final * 32 + s_id + 1] -> state id
    int map_id[32*32];
    int state_count = 0;
    for(s = bstates->prv; s != bstates; s = s->prv) {
        map_id[s->final * 32 + s->id + 1] = state_count++;
    }

    // allocate buchi struct
    res = RTmalloc(sizeof(ltsmin_buchi_t) + state_count * sizeof(ltsmin_buchi_state_t*));
    int n_symbols = sym_id;
    res->acceptance_set = 0;
    res->predicate_count = n_symbols;
    res->predicates = RTmalloc(n_symbols * sizeof(ltsmin_expr_t));
    for (int i=0; i < n_symbols; i++) {
        ltsmin_expr_t e = ltsmin_expr_lookup (NULL, sym_table[i], &le_list);
        Debug("LTL symbol table: lookup up predicate '%s': %p", sym_table[i], e);
        HREassert (e != NULL, "Lookup failed for expression: %s", sym_table[i]);
        res->predicates[i] = e;
    }
    res->state_count = state_count;
    int index = 0;
    state_count = 0;
    for(s = bstates->prv; s != bstates; s = s->prv) {
        ltsmin_buchi_state_t * bs = NULL;
        int transition_count = 0;
        // count transitions
        for(t = s->trans->nxt; t != s->trans; t = t->nxt)
            transition_count++;
        // allocate memory for transitions
        bs = RTmalloc(sizeof(ltsmin_buchi_state_t) + transition_count * sizeof(ltsmin_buchi_transition_t));
        bs->accept = (s->final == accept);
        bs->transition_count = transition_count;

        transition_count = 0;
        for(t = s->trans->nxt; t != s->trans; t = t->nxt) {
            bs->transitions[transition_count].acc_set = 0;
            bs->transitions[transition_count].pos = t->pos;
            bs->transitions[transition_count].neg = t->neg;
            bs->transitions[transition_count].to_state = map_id[t->to->final*32+t->to->id+1];
            bs->transitions[transition_count].index = index++;
            transition_count++;
        }

        res->states[state_count++] = bs;
    }
    res->trans_count = index;
    return res;
}
