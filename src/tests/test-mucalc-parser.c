#include <hre/config.h>

#include <assert.h>

#include <hre/runtime.h>
#include <hre-io/user.h>
#include <ltsmin-lib/mucalc-grammar.h>
#include <ltsmin-lib/mucalc-parse-env.h>
#include <ltsmin-lib/mucalc-syntax.h>
#include <ltsmin-lib/mucalc-lexer.h>

static  struct poptOption options[] = {
    POPT_TABLEEND
};

void parse_file(const char *file, mucalc_parse_env_t env)
{
    FILE *in=fopen( file, "r" );
    stream_t stream = NULL;
    size_t used;
    if (in) {
        Print(infoLong, "Opening stream.");
        stream = stream_input(in);
    } else {
        Print(infoLong, "Read into memory.");
        stream = stream_read_mem((void*)file, strlen(file), &used);
    }
    yyscan_t scanner;
    env->input=stream;
    env->parser=mucalc_parse_alloc(RTmalloc);
    mucalc_parse(env->parser, TOKEN_EXPR, 0, env);
    mucalc_lex_init_extra(env, &scanner);
    Print(infoLong, "Start lexer.");
    mucalc_lex(scanner);
    Print(infoLong, "Lexer done.");
    mucalc_lex_destroy(scanner);
    stream_close(&env->input);
    mucalc_parse_free(env->parser, RTfree);
}

void mucalc_parser_test(const char *file)
{
    Print(infoLong, "Mu-calculus parser test for file %s.", file);
    mucalc_parse_env_t env = mucalc_parse_env_create();
    parse_file(file, env);
    mucalc_parse_env_destroy(env);
}

int main(int argc, char *argv[]){
    char *files[1];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Test for the Mu-calculus parser. Tests for <formula>.\n");
    HREinitStart(&argc,&argv,1,1,files,"<formula>");

    mucalc_parser_test(files[0]);

    HREexit(0);
}
