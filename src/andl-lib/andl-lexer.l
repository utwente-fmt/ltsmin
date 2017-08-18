%option prefix="andl_"
%option outfile="andl-lexer.c"
%option header-file="andl-lexer.h"
%option reentrant bison-bridge bison-locations
%option noyywrap noyyalloc noyyfree noyyrealloc fast noinput nounput
%top{
#include <hre/config.h>
#include <andl-lib/libandl_la-andl-parser.h>
#define YYSTYPE ANDL_STYPE
#define YYLTYPE ANDL_LTYPE
}

%{
#define YY_USER_INIT {yylloc->first_column = 0; yylloc->first_line = 1;}

void *yyalloc(size_t bytes, void *yyscanner){
    (void) yyscanner;
    return RTmalloc(bytes);
}
void *yyrealloc(void *ptr, size_t bytes, void *yyscanner){
    (void) yyscanner;
    return RTrealloc(ptr,bytes);
}

void yyfree(void *ptr, void *yyscanner){
    (void) yyscanner;
    RTfree(ptr);
}

#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"

%}

white_space [ \t]+
ident       [_]*([a-zA-Z]|\\[[:print:]])([a-zA-Z0-9_']|\\[[:print:]])*
number      [0-9]+
endofline   (\n|\r|\n\r|\r\n)

%%

%{

%}

{white_space} { yylloc->first_column++; } //white space, but not end of line.

{endofline} { yylloc->first_line++; yylloc->first_column = 0;}

"pn" return PN;

"[" return LBRAC;
"]" return RBRAC;
"{" return LCURLY;
"}" return RCURLY;
":" return COLON;
"constants" return CONSTANTS;
"places" return PLACES;
"discrete" return DISCRETE;
"=" return ASSIGN;
{number}    {
                yylval->number = atoi(yytext);
                // test if the value fits in an int
                char buf[strlen(yytext) + 1];
                sprintf(buf, "%d", yylval->number);
                if (strcmp(buf, yytext) != 0) {
                    Abort("Make sure the inscription \"%s\" fits in a signed 32-bit integer", yytext);
                }

                return NUMBER;
            }
"+" { yylval->dir = ARC_OUT; return PLUS; }
"-" { yylval->dir = ARC_IN; return MIN; }
"&" return AMP;
"transitions" return TRANSITIONS;
";" return SEMICOLON;

{ident} {
            yylval->text = strdup(yytext);
            return IDENT;
        }

%%
