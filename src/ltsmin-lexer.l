%option prefix="ltsmin_"
%option header-file="ltsmin-lexer.h"
%option outfile="ltsmin-lexer.c"
%option nodefault noyywrap reentrant noyyalloc noyyrealloc noyyfree batch noinput
%option extra-type="ltsmin_parse_env_t"
%{
#include <ltsmin-parse-env.h>
#include <ltsmin-grammar.h>
#include <runtime.h>
#include <stream.h>
#include <chunk_support.h>

void * yyalloc (size_t bytes, void * yyscanner){
    (void)yyscanner;
    return malloc(bytes);
}
void * yyrealloc (void * ptr, size_t bytes, void * yyscanner){
    (void)yyscanner;
    return realloc(ptr,bytes);
}

void   yyfree (void * ptr, void * yyscanner){
    (void)yyscanner;
    free(ptr);
}


#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#define YY_INPUT(buf,result,max_size) \
    { result = stream_read_max(yyextra->input, buf , max_size ) ; }

#define YY_FATAL_ERROR(msg) Fatal(1,error,"LTSmin lexer: %s",msg);

%}

white_space [ \t]+
ident       ([a-zA-Z]|\\[[:print:]])([a-zA-Z0-9_']|\\[[:print:]])*
number      [0-9]+
string      \"(([[:print:]]{-}[\\\"])|"\\\\"|"\\\"")*\"
chunk       #([0-9a-fA-F][0-9a-fA-F])*#
operator    [~!@<>=\-+/?&\\|*]+

%%

{white_space}  {} //white space, but not end of line.

"\n" {
    yyextra->lineno++;
    if(yyextra->linebased) {
        Parse(yyextra->parser,TOKEN_END_OF_LINE,0,yyextra);
    }
}

"%".*   {} //comment

"(" {
    Parse(yyextra->parser,TOKEN_LPAR,0,yyextra);
}

")" {
    Parse(yyextra->parser,TOKEN_RPAR,0,yyextra);
}

"." {
    Parse(yyextra->parser,TOKEN_DOT,0,yyextra);
}


":" {
    Parse(yyextra->parser,TOKEN_COLON,0,yyextra);
}

"_" {
    Parse(yyextra->parser,TOKEN_DONTCARE,0,yyextra);
}

{string}|{chunk} {
    int len=strlen(yytext);
    char decode[len];
    chunk c={.data=decode,.len=len};
    string2chunk(yytext,&c);
    Parse(yyextra->parser,TOKEN_VALUE,SIputC(yyextra->values,c.data,c.len),yyextra);
}

{ident} {
    int idx;
    if ((idx=SIlookup(yyextra->keywords,yytext))!=SI_INDEX_FAILED) {
        Parse(yyextra->parser,idx,0,yyextra);
    } else {
        int N=strlen(yytext);
        char decoded[N+1];
        int j=0;
        for(int i=0;i<N;i++){
            if(yytext[i]=='\\'){
                decoded[j]=yytext[i+1];
                i++;
            } else {
                decoded[j]=yytext[i];
            }
            j++;
        }
        decoded[j]=0;
        if ((idx=SIlookup(yyextra->state_vars,decoded))!=SI_INDEX_FAILED) {
            Parse(yyextra->parser,TOKEN_STATE_VAR,idx,yyextra);
        } else if ((idx=SIlookup(yyextra->edge_vars,decoded))!=SI_INDEX_FAILED) {
            Parse(yyextra->parser,TOKEN_EDGE_VAR,idx,yyextra);
        } else {
            Parse(yyextra->parser,TOKEN_IDENT,SIput(yyextra->idents,decoded),yyextra);
        }
    }
}

{number} {
        Parse(yyextra->parser,TOKEN_NUMBER,atoi(yytext),yyextra);
}

<<EOF>> {
    Parse(yyextra->parser,0,0,yyextra);
    return 0;
}

{operator} {
    int idx;
    if ((idx=SIlookup(yyextra->keywords,yytext))!=SI_INDEX_FAILED) {
        Parse(yyextra->parser,idx,0,yyextra);
    } else if((idx=SIlookup(yyextra->unary_ops,yytext))!=SI_INDEX_FAILED){
        Parse(yyextra->parser,yyextra->unary_info[idx].token,idx,yyextra);
    } else if((idx=SIlookup(yyextra->binary_ops,yytext))!=SI_INDEX_FAILED){
        Parse(yyextra->parser,yyextra->binary_info[idx].token,idx,yyextra);
    } else {
        Fatal(1,error,"Unknown operator %s",yytext);
    }
}

. {
    Fatal(1,error,"Unexpected character %s (%x)",yytext,yytext[0]);
    (void)yyunput;
}

%%


