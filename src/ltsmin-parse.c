#include <config.h>

#include <ltl2ba.h>
#undef Debug
#include <runtime.h>
#include <ltsmin-syntax.h>
#include <ltsmin-grammar.h>
#include <lts-type.h>
#include <ltsmin-tl.h>

typedef enum {PARSE_LTL, PARSE_CTL, PARSE_CTL_S, PARSE_MU} parse_mode_t;
static parse_mode_t parse_mode=PARSE_MU;

static  struct poptOption options[] = {
    {"ltl", 0, POPT_ARG_VAL, &parse_mode, PARSE_LTL, "parse and verify an ltl formula", NULL },
    {"ctl", 0, POPT_ARG_VAL, &parse_mode, PARSE_CTL, "parse and verify a ctl formula", NULL },
    {"ctl*", 0, POPT_ARG_VAL, &parse_mode, PARSE_CTL_S, "parse and verify a ctl* formula", NULL },
    {"mu", 0, POPT_ARG_VAL, &parse_mode, PARSE_MU, "parse and verify mu calculus", NULL },
    POPT_TABLEEND
};

// XXX move to include file
void ltsmin_ltl2ba(ltsmin_expr_t);

int main(int argc, char *argv[]){
    char* file_name;
    RTinitPopt(&argc,&argv,options,1,1,&file_name,NULL,"<input>",
                "test the LTSmin expression parser\n\nOptions");
    lts_type_t ltstype=lts_type_create();
    lts_type_set_state_length(ltstype,1);
    lts_type_set_state_name(ltstype,0,"x");
    lts_type_set_state_type(ltstype,0,"state");
    lts_type_set_state_label_count(ltstype,1);
    lts_type_set_state_label_name(ltstype,0,"p");
    lts_type_set_state_label_type(ltstype,0,"boolean");
    lts_type_set_edge_label_count(ltstype,1);
    lts_type_set_edge_label_name(ltstype,0,"action");
    lts_type_set_edge_label_type(ltstype,0,"action");
    switch(parse_mode) {
        case PARSE_MU: {
            mu_parse_file(ltstype, file_name);
            } break;
        case PARSE_LTL: {
            ltsmin_expr_t ltl = ltl_parse_file(ltstype, file_name);
            ltsmin_ltl2ba(ltl);
            /*
            print_expr(ltl);
            ltsmin_expr_t ctl = ltl_to_ctl_star(ltl);
            print_expr(ctl);
            ltsmin_expr_t mu = ctl_star_to_mu(ctl);
            print_expr(mu);
            */
            } break;
        case PARSE_CTL: {
            ltsmin_expr_t ctl = ctl_parse_file(ltstype, file_name);
            (void)ctl;
            } break;
        case PARSE_CTL_S: {
            ltsmin_expr_t ctl = ctl_parse_file(ltstype, file_name);
            ltsmin_expr_t mu = ctl_star_to_mu(ctl);
            (void)mu;
            } break;
    }
    return 0;
}
