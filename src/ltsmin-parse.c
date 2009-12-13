#include <config.h>
#include <runtime.h>
#include <ltsmin-syntax.h>
#include <ltsmin-grammar.h>
#include <lts-type.h>
#include <ltsmin-mu.h>

static  struct poptOption options[] = {
    POPT_TABLEEND
};


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
    mu_parse_file(ltstype,file_name);
    return 0;
}
