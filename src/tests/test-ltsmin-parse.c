#include <hre/config.h>

#undef Debug
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-grammar.h>
#include <ltsmin-lib/lts-type.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <pins-lib/property-semantics.h>
#include <pins-lib/pins.h>

typedef enum {PARSE_LTL, PARSE_CTL, PARSE_CTL_S, PARSE_MU} parse_mode_t;
static parse_mode_t parse_mode=PARSE_MU;

static  struct poptOption options[] = {
    {"ltl", 0, POPT_ARG_VAL, &parse_mode, PARSE_LTL, "parse and verify an ltl formula", NULL },
    {"ctl", 0, POPT_ARG_VAL, &parse_mode, PARSE_CTL, "parse and verify a ctl formula", NULL },
    {"ctl-star", 0, POPT_ARG_VAL, &parse_mode, PARSE_CTL_S, "parse and verify a ctl* formula", NULL },
    {"mu", 0, POPT_ARG_VAL, &parse_mode, PARSE_MU, "parse and verify mu calculus", NULL },
    POPT_TABLEEND
};

int main(int argc, char *argv[]){
    char* file_name;
    HREinitBegin(argv[0]);
    HREaddOptions(options,"test the LTSmin expression parser\n\nOptions");
    HREinitStart(&argc,&argv,1,2,&file_name,"<input>");

    lts_type_t ltstype=lts_type_create();
    lts_type_set_state_length(ltstype,1);
    lts_type_set_state_name(ltstype,0,"x");
    lts_type_set_state_type(ltstype,0,"state");
    lts_type_set_state_label_count(ltstype,1);
    lts_type_set_state_label_name(ltstype,0,"p");
    lts_type_set_state_label_type(ltstype,0,"boolean");
    lts_type_set_edge_label_count(ltstype,1);
    lts_type_set_edge_label_name(ltstype,0,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    lts_type_set_edge_label_type(ltstype,0,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    switch(parse_mode) {
        case PARSE_MU: {
            ltsmin_parse_env_t env = LTSminParseEnvCreate();
            mu_parse_file(file_name, env, ltstype);
            } break;
        case PARSE_LTL: {
            ltsmin_parse_env_t env = LTSminParseEnvCreate();
            ltl_parse_file(file_name, env, ltstype);
            //ltsmin_expr_t notltl = LTSminExpr(UNARY_OP, LTL_NOT, 0, ltl, NULL);
            //ltsmin_ltl2ba(notltl);
            /*
            print_expr(ltl);
            ltsmin_expr_t ctl = ltl_to_ctl_star(ltl);
            print_expr(ctl);
            ltsmin_expr_t mu = ctl_star_to_mu(ctl);
            print_expr(mu);
            */
            } break;
        case PARSE_CTL: {
            ltsmin_parse_env_t env = LTSminParseEnvCreate();
            ltsmin_expr_t ctl = ctl_parse_file(file_name, env, ltstype);
            (void)ctl;
            } break;
        case PARSE_CTL_S: {
            ltsmin_parse_env_t env = LTSminParseEnvCreate();
            ltsmin_expr_t ctl = ctl_parse_file(file_name, env, ltstype);
            ltsmin_expr_t mu = ctl_star_to_mu(ctl);
            (void)mu;
            } break;
    }
    return 0;
}
