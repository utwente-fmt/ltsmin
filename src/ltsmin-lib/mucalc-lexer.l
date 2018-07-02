/* scanner for mu-calculus */
 
%option prefix="mucalc_"
%option header-file="mucalc-lexer.h"
%option outfile="mucalc-lexer.c"
%option nodefault noyywrap reentrant noyyalloc noyyrealloc noyyfree batch noinput
%option extra-type="mucalc_parse_env_t"
%{
#include <hre/config.h>

#include <assert.h>

#include <hre/user.h>
#include <hre-io/user.h>
#include <ltsmin-lib/mucalc-parse-env.h>
#include <ltsmin-lib/mucalc-grammar.h>

void * yyalloc (size_t bytes, void * yyscanner){
    (void)yyscanner;
    return RTmalloc(bytes);
}
void * yyrealloc (void * ptr, size_t bytes, void * yyscanner){
    (void)yyscanner;
    return RTrealloc(ptr,bytes);
}

void   yyfree (void * ptr, void * yyscanner){
    (void)yyscanner;
    RTfree(ptr);
}


#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#define YY_INPUT(buf,result,max_size) \
    { result = stream_read_max(yyextra->input, buf , max_size ) ; }

%}
 
ID          [A-Za-z_][A-Za-z0-9_]*
STRING      \"(([[:print:]]{-}[\\\"])|"\\\\"|"\\\"")*\"
NUMBER      [0-9]+
WHITESPACE  [ \t]+
EOL         (\n|\r|\n\r|\r\n)

%%

"(" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_LPAR,0,yyextra);
}

")" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_RPAR,0,yyextra);
}

"mu" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_MU,0,yyextra);
}

"nu" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_NU,0,yyextra);
}

"." {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_DOT,0,yyextra);
}

"true" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_TRUE,0,yyextra);
}

"false" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_FALSE,0,yyextra);
}  

{ID}     {
    yyextra->linepos+=strlen(yytext);
    int value = SIput(yyextra->ids, yytext);
    //Print(infoLong, "lexer: ID: %s (%d)", yytext, value);
    mucalc_parse(yyextra->parser,TOKEN_ID,value,yyextra);
}

{STRING} {
    yyextra->linepos+=strlen(yytext);
    assert(strlen(yytext) >= 2);
    // trim the quote characters
    yytext[strlen(yytext) - 1] = '\0';
    char* str = yytext + sizeof(char);
    // unescape escaped strings
    char* unescaped = mucalc_unescape_string(str);
    int value = SIput(yyextra->strings, unescaped);
    //Print(infoLong, "lexer: STRING: %s %s (%d)", str, unescaped, value);
    mucalc_parse(yyextra->parser,TOKEN_STRING,value,yyextra);
}

{NUMBER} {
    yyextra->linepos+=strlen(yytext);
    int value = atoi(yytext);
    //Print(infoLong, "lexer: NUMBER: %d", value);
    mucalc_parse(yyextra->parser,TOKEN_NUMBER,value,yyextra);
}

"&&" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_AND,0,yyextra);
}

"||" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_OR,0,yyextra);
}

"!" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_NOT,0,yyextra);
}
 
"<" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_LMAY,0,yyextra);
}

">" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_RMAY,0,yyextra);
}

"[" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_LMUST,0,yyextra);
}

"]" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_RMUST,0,yyextra);
}

"{" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_LBRACKET,0,yyextra);
}

"}" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_RBRACKET,0,yyextra);
}

"=" {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,TOKEN_EQUALS,0,yyextra);
}

{WHITESPACE}  {yyextra->linepos+=strlen(yytext);} //white space, but not end of line.

{EOL} {
    yyextra->lineno++;
    yyextra->linepos=0;
}

"%".*   {} //comment
 
<<EOF>> {
    yyextra->linepos+=strlen(yytext);
    mucalc_parse(yyextra->parser,0,0,yyextra);
    return 0;
}

. {
    yyextra->linepos++;
    HREmessage(lerror,"Unexpected character %s (%x) at line %d, pos %d",
          yytext,yytext[0],yyextra->lineno+1,yyextra->linepos+1);
    yyextra->error = true;
    (void)yyunput;
}

%%

