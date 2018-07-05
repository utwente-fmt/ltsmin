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

void parse_stream(stream_t stream, mucalc_parse_env_t env)
{
    yyscan_t scanner;
    env->input=stream;
    env->parser=mucalc_parse_alloc(RTmalloc);
    mucalc_parse(env->parser, TOKEN_EXPR, 0, env);
    mucalc_lex_init_extra(env, &scanner);
    mucalc_lex(scanner);
    mucalc_check_formula(env, env->formula_tree);
    mucalc_lex_destroy(scanner);
    stream_close(&env->input);
    mucalc_parse_free(env->parser, RTfree);
}

void parse_string(const char *formula, mucalc_parse_env_t env)
{
    stream_t stream = NULL;
    size_t used;
    stream = stream_read_mem((void*)formula, strlen(formula), &used);
    parse_stream(stream, env);
}

void parse_file(const char *file, mucalc_parse_env_t env)
{
    FILE *in=fopen( file, "r" );
    if (in) {
        Print(infoLong, "Opening stream.");
        stream_t stream = stream_input(in);
        parse_stream(stream, env);
    } else {
        parse_string(file, env);
    }
}

void mucalc_parser_test_file(const char *file)
{
    Print(infoLong, "Mu-calculus parser test for file %s.", file);
    mucalc_parse_env_t env = mucalc_parse_env_create();
    parse_file(file, env);
    if (log_active(infoLong))
    {
        Print(infoLong, "Parser is done.")
        char buf[256];
        mucalc_print_formula(buf, sizeof(buf), env, env->formula_tree);
        Print(infoLong, "output: %s", buf);
    }
    mucalc_parse_env_destroy(env);
}

void mucalc_parser_test_string(const char *input, const char *expected)
{
    Print(infoLong, "input:  %s", input);
    mucalc_parse_env_t env = mucalc_parse_env_create();
    parse_string(input, env);
    char output[256];
    mucalc_print_formula(output, sizeof(output), env, env->formula_tree);
    mucalc_parse_env_destroy(env);
    Print(infoLong, "output: %s", output);
    if (expected == NULL)
    {
        assert(strlen(input)==strlen(output) && strncmp(input, output, strlen(input))==0);
    }
    else
    {
        assert(strlen(expected)==strlen(output) && strncmp(expected, output, strlen(expected))==0);
    }
    Print(infoLong, "succes.");
}

void test_formulas()
{
    Print(infoLong, "Mu-calculus parser test suite.");
    mucalc_parser_test_string("mu X . <>X", NULL);
    mucalc_parser_test_string("nu Y . []Y", NULL);
    mucalc_parser_test_string("mu X . [\"move\"]X", NULL);
    mucalc_parser_test_string("mu X . <\"a(5)\">X", NULL);
    mucalc_parser_test_string("mu X . <\"a(\\\"c\\\")\">X", NULL);
    //mucalc_parser_test_string("mu X . <>Y", NULL); // SHOULD FAIL
    mucalc_parser_test_string("({a=3} || {a=4})", NULL);
    mucalc_parser_test_string("mu X . (({a=3} || {a=4}) && [\"b\"]X)", NULL);
    mucalc_parser_test_string("nu Z . ([!\"a\"]false && [\"a\"]Z)", NULL);
    mucalc_parser_test_string("nu Z . ([\"a\"]!{x=2} && [\"b\"]Z)", NULL);
    mucalc_parser_test_string("nu Z . (<\"a\">!false && [\"a\"]Z)",     "nu Z . (<\"a\">true && [\"a\"]Z)");
    //mucalc_parser_test_string("nu Z . (<\"a\">false && [\"a\"]!Z)", NULL); // SHOULD FAIL
    //mucalc_parser_test_string("mu Z . (!([\"a\"]Z))", NULL); // SHOULD FAIL
}

int main(int argc, char *argv[]){
    char *files[1];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Test for the Mu-calculus parser. "
            "Tests for <formula> if formula given. "
            "Runs a test suite otherwise.\n");
    HREinitStart(&argc,&argv,0,1,files,"<formula>");

    if (files[0] != NULL)
    {
        mucalc_parser_test_file(files[0]);
    }
    else
    {
        test_formulas();
    }

    HREexit(0);
}
