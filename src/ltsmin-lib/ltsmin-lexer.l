%option prefix="ltsmin_"
%option header-file="ltsmin-lexer.h"
%option outfile="ltsmin-lexer.c"
%option nodefault noyywrap reentrant noyyalloc noyyrealloc noyyfree batch noinput
%option extra-type="ltsmin_parse_env_t"
%{
#include <hre/config.h>
#include <hre/user.h>
#include <hre-io/user.h>
#include <util-lib/chunk_support.h>
#include <ltsmin-lib/ltsmin-parse-env.h>
#include <ltsmin-lib/ltsmin-grammar.h>

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

white_space [ \t]+
ident       [_]*([a-zA-Z]|\\[[:print:]])([a-zA-Z0-9_']|\\[[:print:]])*
number      [0-9]+
string      \"(([[:print:]]{-}[\\\"])|"\\\\"|"\\\"")*\"
chunk       #([0-9a-fA-F][0-9a-fA-F])*#
operator    [~!@<>=\-+/?&\\|*\[\]]+
endofline   (\n|\r|\n\r|\r\n)

%%

{white_space}  {yyextra->linepos+=strlen(yytext);} //white space, but not end of line.

{endofline} {
    yyextra->lineno++;
    yyextra->linepos=0;
    if(yyextra->linebased) {
        Parse(yyextra->parser,TOKEN_END_OF_LINE,0,yyextra);
    }
}

"%".*   {} //comment

"(" {
    yyextra->linepos+=strlen(yytext);
    Parse(yyextra->parser,TOKEN_LPAR,0,yyextra);
}

")" {
    yyextra->linepos+=strlen(yytext);
    Parse(yyextra->parser,TOKEN_RPAR,0,yyextra);
}

"." {
    yyextra->linepos+=strlen(yytext);
    Parse(yyextra->parser,TOKEN_DOT,0,yyextra);
}

":" {
    yyextra->linepos+=strlen(yytext);
    Parse(yyextra->parser,TOKEN_COLON,0,yyextra);
}

{string}|{chunk} {
    int len=strlen(yytext);
    yyextra->linepos+=strlen(yytext);
    char decode[len];
    chunk c={.data=decode,.len=len};
    string2chunk(yytext,&c);
    Parse(yyextra->parser,TOKEN_VALUE,SIputC(yyextra->values,c.data,c.len),yyextra);
}

{ident} {
    int idx;
    yyextra->linepos+=strlen(yytext);
    if ((idx=SIlookup(yyextra->keywords,yytext))!=SI_INDEX_FAILED) {
        Parse(yyextra->parser,idx,idx,yyextra);
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

        if ((idx=SIlookup(yyextra->constant_ops,decoded))!=SI_INDEX_FAILED) {
            Parse(yyextra->parser,TOKEN_CONSTANT,idx,yyextra);
        } else if ((idx=SIlookup(yyextra->unary_ops,decoded))!=SI_INDEX_FAILED) {
            Parse(yyextra->parser,yyextra->unary_info[idx].pattern,idx,yyextra);
        } else if((idx=SIlookup(yyextra->binary_ops,decoded))!=SI_INDEX_FAILED){
            Parse(yyextra->parser,yyextra->binary_info[idx].pattern,idx,yyextra);
        } else if ((idx=SIlookup(yyextra->state_vars,decoded))!=SI_INDEX_FAILED) {
            Parse(yyextra->parser,TOKEN_STATE_VAR,idx,yyextra);
        } else if ((idx=SIlookup(yyextra->edge_vars,decoded))!=SI_INDEX_FAILED) {
            Parse(yyextra->parser,TOKEN_EDGE_VAR,idx,yyextra);
        } else {
            Parse(yyextra->parser,TOKEN_IDENT,SIput(yyextra->idents,decoded),yyextra);
        }
    }
}

{number} {
    yyextra->linepos+=strlen(yytext);
    Parse(yyextra->parser,TOKEN_NUMBER,atoi(yytext),yyextra);
}

<<EOF>> {
    yyextra->linepos+=strlen(yytext);
    Parse(yyextra->parser,0,0,yyextra);
    return 0;
}

{operator} {
    int idx;
    char yybuf[strlen(yytext)+1];
    int matched = 0;
    // find longest match
    // note: this is quite expensive like this, improve?
    for(int i=strlen(yytext); i>0 && !matched; i--) {
        memset(yybuf, 0, i+1);
        memcpy(yybuf, yytext, i);

        if ((idx=SIlookup(yyextra->keywords,yybuf))!=SI_INDEX_FAILED) {
            yyless(i);
            yyextra->linepos+=i;
            Parse(yyextra->parser,idx,0,yyextra);
            matched = 1;
        } else if((idx=SIlookup(yyextra->unary_ops,yybuf))!=SI_INDEX_FAILED){
            yyless(i);
            yyextra->linepos+=i;
            Parse(yyextra->parser,yyextra->unary_info[idx].pattern,idx,yyextra);
            matched = 1;
        } else if((idx=SIlookup(yyextra->binary_ops,yybuf))!=SI_INDEX_FAILED){
            yyless(i);
            yyextra->linepos+=i;
            Parse(yyextra->parser,yyextra->binary_info[idx].pattern,idx,yyextra);
            matched = 1;
        }
    }
    if (!matched)
        Abort("Unknown operator %s at line %d, pos %d",yytext,yyextra->lineno+1,yyextra->linepos+1);
}

. {
    yyextra->linepos++;
    HREmessage(lerror,"Unexpected character %s (%x) at line %d, pos %d",
          yytext,yytext[0],yyextra->lineno+1,yyextra->linepos+1);
    Parse(yyextra->parser,TOKEN_ERROR,0,yyextra);
    /* Because the same lexer is used for multiple languages, we give the
       parser the opportunity to provide a sensible error message. */
    (void)yyunput;
}

%%


