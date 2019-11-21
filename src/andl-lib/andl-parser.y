/* The ANDL grammer, found in:
@TECHREPORT { SRH16,
    AUTHOR = { M Schwarick and C Rohr and M Heiner },
    TITLE = { {MARCIE Manual} },
    INSTITUTION = { Brandenburg University of Technology Cottbus, Department of Computer Science },
    YEAR = { 2016 },
    NUMBER = { 02-16 },
    MONTH = { December },
    URL = { https://opus4.kobv.de/opus4-btu/frontdoor/index/index/docId/4056 },
    PDF = { http://www-dssz.informatik.tu-cottbus.de/track/download.php?id=187 },
}
*/
%pure-parser
%locations
%defines
%lex-param { void *scanner }
%parse-param { void *scanner} { andl_context_t *andl_context }
%define parse.error verbose
%define api.prefix {andl_}
%code requires {
#include <hre/config.h>
#include <stdio.h>
#include <pins-lib/modules/pnml-pins.h>
#ifdef YYDEBUG
#undef YYDEBUG
#define YYDEBUG 1
#endif
}
%code{
#include <andl-lib/andl-lexer.h>
#include <hre/stringindex.h>
#include <hre/user.h>

    void yy_fatal_error(yyconst char* msg , yyscan_t yyscanner) {
        (void) msg; (void) yyscanner;
    }

    void yyerror(YYLTYPE *loc, void *scanner, andl_context_t *andl_context, const char* c) {
        Warning(info, "Parse error on line %d: %s", loc->first_line, c);
        andl_context->error = 1;
        (void) scanner;
    }
}

%union { 
    char *text;
    int number;
    arc_dir_t dir;
}

%token PN
%token <text> IDENT
%token LBRAC
%token RBRAC
%token LCURLY
%token RCURLY
%token COLON
%token CONSTANTS
%token PLACES
%token DISCRETE
%token SEMICOLON
%token ASSIGN
%token <number> NUMBER
%token <dir> PLUS
%token <dir> MIN
%token AMP
%token TRANSITIONS

%type <dir> op
%type <number> const_function

%%

pn
    :   PN LBRAC IDENT RBRAC LCURLY items RCURLY {
            andl_context->pn_context->name = $3;
        }
    ;

items
    :   item
    |   items item
    ;

item
    :   constants
    |   places
    |   discrete
    |   transitions
    ;

constants
    :   CONSTANTS COLON
    ;

places
    :   PLACES COLON
    ;

discrete
    :   DISCRETE COLON pdecs
    ;

transitions
    :   TRANSITIONS COLON {
            andl_context->pn_context->num_transitions = 0;
        } tdecs {
            ensure_access(andl_context->arc_man, andl_context->pn_context->num_arcs);
            arc_t * const arc = andl_context->pn_context->arcs + andl_context->pn_context->num_arcs;
            arc->type = ARC_LAST;
            arc->transition = -1;
            arc->place = -1;
        }
    ;

pdecs
    :   pdecs pdec
    |   /* empty */
    ;

pdec
    :   IDENT ASSIGN NUMBER SEMICOLON {
            int i = SIlookup(andl_context->pn_context->place_names, $1);
            if (i != SI_INDEX_FAILED) {
                andl_context->error = 1;
                Warning(info, "Duplicate place: %s", $1);                
            } else {
                i = SIput(andl_context->pn_context->place_names, $1);
                ensure_access(andl_context->init_man, i);
                andl_context->pn_context->init_state[i] = $3;
            }
            RTfree($1);
        }
    |   IDENT error SEMICOLON {
            Warning(info, "Something went wrong with place %s on line %d", $1, @1.first_line);
        }
    ;

tdecs
    :   tdecs tdec
    |   /* empty */
    ;

tdec
    :   IDENT COLON conditions COLON {     
            ensure_access(andl_context->trans_man, andl_context->pn_context->num_transitions);
            transition_t *const t = andl_context->pn_context->transitions +
                andl_context->pn_context->num_transitions;
            t->label = SIput(andl_context->pn_context->edge_labels, $1);
            t->in_arcs = 0;
            t->out_arcs = 0;
            RTfree($1);
        } arcs transition_function SEMICOLON { 
            andl_context->pn_context->num_transitions++;
        }
    |   IDENT error SEMICOLON {
            Warning(info, "Something went wrong with transition %s on line %d", $1, @1.first_line);
        }
    ;

conditions
    :   /* empty */
    ;

arcs
    :   arc
    |   arcs AMP arc
    |   /* empty */ 
    ;

arc
    :   LBRAC IDENT op const_function RBRAC {
            const int i = SIlookup(andl_context->pn_context->place_names, $2);
            if (i == SI_INDEX_FAILED) {
                andl_context->error = 1;
                Warning(info, "Place %s is not declared", $2);
            } else {
                ensure_access(andl_context->arc_man, andl_context->pn_context->num_arcs);

                arc_t * const arc = andl_context->pn_context->arcs + andl_context->pn_context->num_arcs++;

                arc->safe = 0;
                arc->place = i;
                arc->num = $4;
                arc->type = $3;
                arc->transition = andl_context->pn_context->num_transitions;

                if ($3 == ARC_IN) {
                    andl_context->pn_context->num_in_arcs++;
                    andl_context->pn_context->transitions[arc->transition].in_arcs++;
                } else if ($3 == ARC_OUT) {
                    andl_context->pn_context->transitions[arc->transition].out_arcs++;
                } else {
                    andl_context->error = 1;
                    Warning(info, "Unknown arc type");
                }
            }
            
            RTfree($2);
        }
    |   LBRAC error RBRAC {
            Warning(info, "Missing identifier on line %d", @1.first_line);
        }
    |   LBRAC IDENT error RBRAC {
            Warning(info, "Something went wrong with arc %s on line %d", $2, @1.first_line);
        }
    ;

op
    :   PLUS
    |   MIN
    |   ASSIGN {
            andl_context->error = 1;
            Warning(info, "Constant assign arcs not yet supported.");
        }
    ;

const_function
    :   NUMBER
    ;

transition_function
    :   /* empty */
    ;

%%
