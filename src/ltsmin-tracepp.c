#include <config.h>
#include <runtime.h>
#include <lts_io.h>
#include <trace.h>
#include <stdio.h>
#include <chunk_support.h>

#define  BUFLEN 4096

static int arg_all=0;
static int arg_table=0;
static char *arg_value="name";
static enum { FF_TXT, FF_CSV, FF_DOT, FF_HTML, FF_LATEX, FF_FSM, FF_TRACE, FF_AUT} file_format = FF_TXT;
static char *arg_sep=",";
static enum { IDX, NAME } output_value = NAME;

static si_map_entry output_values[] = {
    {"name",    NAME},
    {"idx",     IDX},
    {NULL, 0}
};

static si_map_entry file_formats[] = {
    {"txt",     FF_TXT},
    {"csv",     FF_CSV},
/*
    {"dot",     FF_DOT},
    {"html",    FF_HTML},
    {"latex",   FF_LATEX},
    {"FSM",     FF_FSM},
    {"trace",   FF_TRACE},
*/
    {"aut",     FF_AUT},
    {NULL, 0}
};

static void
output_popt (poptContext con, enum poptCallbackReason reason,
               const struct poptOption *opt, const char *arg, void *data)
{
    (void)con; (void)opt; (void)arg; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST: {
            int ov = linear_search (output_values, arg_value);
            if (ov < 0) {
                Warning (error, "unknown output value %s", arg_value);
                RTexitUsage (EXIT_FAILURE);
            }
            output_value = ov;
        }
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal (1, error, "unexpected call to output_popt");
}

static  struct poptOption options[] = {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION  , (void*)output_popt , 0 , NULL , NULL },
    { "csv-separator" , 's' , POPT_ARG_STRING , &arg_sep , 0 , "separator in csv output" , NULL },
    { "values" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &arg_value , 0 ,
      "output indices/names", "<idx|name>"},
    { "table" , 't' , POPT_ARG_VAL , &arg_table , 1 , "output txt format as table" , NULL },
    { "list" , 'l' , POPT_ARG_VAL , &arg_table , 0 , "output txt format as list", NULL },
    { "diff" , 'd' , POPT_ARG_VAL , &arg_all , 0 , "output state differences instead of all values" , NULL },
    { "all" , 'a' , POPT_ARG_VAL , &arg_all , 1 , "output all values instead of state differences" , NULL },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL ,NULL},
    POPT_TABLEEND
};

static void
trace_get_type_str(trc_t trace, int typeno, int type_idx, size_t dst_size, char* dst) {
    int notype = trc_get_type(trace, typeno, type_idx, dst_size, dst);
    if (notype || output_value == IDX) {
        snprintf(dst, dst_size, "%d", type_idx);
    }
}

void
output_text(trc_t trace, FILE* output_file) {
    lts_type_t ltstype = trc_get_ltstype(trace);
    int N = lts_type_get_state_length(ltstype);
    int eLbls = lts_type_get_edge_label_count(ltstype);
    int sLbls = lts_type_get_state_label_count(ltstype);
    uint32_t align = 0;  // maximal width of state header/state label header
    char tmp[BUFLEN];
    char tmp2[BUFLEN];

    // calculate width
    for(int j=0; j<N; ++j) {
        // state header
        char *name = lts_type_get_state_name(ltstype, j);
        char *type = lts_type_get_state_type(ltstype, j);
        snprintf(tmp, BUFLEN, "%s:%s[%d]", name == NULL ? "_" : name, type == NULL ? "_" : type, j);
        if (align < strlen(tmp)) align = strlen(tmp);
    }
    for(int j=0; j<sLbls; ++j) {
        char *name = lts_type_get_state_label_name(ltstype, j);
        char *type = lts_type_get_state_label_type(ltstype, j);
        snprintf(tmp, BUFLEN, "%s:%s[%d]", name == NULL ? "_" : name, type == NULL ? "_" : type, j);
        if (align < strlen(tmp)) align = strlen(tmp);
    }

    // output trace
    int len = trc_get_length(trace);
    for(int i=0; i<len; ++i) {
        int prev_state[N];
        int state[N];
        int prev_state_lbls[sLbls];
        int state_lbls[sLbls];
        int edge_lbls[eLbls];

        if (i == 0) {
            // ouput initial state
            fprintf(output_file, "Initial state\n");
            for(int j=0; j<N; ++j) prev_state[j] = -1;
            for(int j=0; j<sLbls; ++j) prev_state_lbls[j] = -1;
        } else {
            // last state
            for(int j=0; j<N; ++j) prev_state[j] = state[j];
            for(int j=0; j<sLbls; ++j) prev_state_lbls[j] = state_lbls[j];
        }

        // get state
        trc_get_state(trace, i, state);

        // output state
        for(int j=0; j<N; ++j) {
            if (arg_all || state[j] != prev_state[j]) {
                char *name = lts_type_get_state_name(ltstype, j);
                char *type = lts_type_get_state_type(ltstype, j);
                snprintf(tmp, BUFLEN, "%s:%s[%d]", name == NULL ? "_" : name, type == NULL ? "_" : type, j);

                int typeno = lts_type_get_state_typeno(ltstype, j);
                trace_get_type_str(trace, typeno, state[j], BUFLEN, tmp2);

                fprintf(output_file, "\t%*s = %s\n", align, tmp, tmp2);
            }
        }

        // output state labels
        if (trc_get_state_label(trace, 0, NULL)) {
            trc_get_state_label(trace, i, state_lbls);
            for(int j=0; j<sLbls; ++j) {
                if (arg_all || state_lbls[j] != prev_state_lbls[j]) {
                    char *name = lts_type_get_state_label_name(ltstype, j);
                    char *type = lts_type_get_state_label_type(ltstype, j);
                    snprintf(tmp, BUFLEN, "%s:%s[%d]", name == NULL ? "_" : name, type == NULL ? "_" : type, j);

                    int typeno = lts_type_get_state_label_typeno(ltstype, j);
                    trace_get_type_str(trace, typeno, state_lbls[j], BUFLEN, tmp2);

                    fprintf(output_file, "\t%*s = %s\n", align, tmp, tmp2);
                }
            }
        }

        // output edge labels
        if ((i+1)<len) {
            if (trc_get_state_label(trace, 0, NULL)) {
                trc_get_state_label(trace, i, edge_lbls);
                for(int j=0; j<eLbls; ++j) {
                    char *name = lts_type_get_edge_label_name(ltstype, j);
                    char *type = lts_type_get_edge_label_type(ltstype, j);
                    snprintf(tmp, BUFLEN, "%s:%s[%d]", name == NULL ? "_" : name, type == NULL ? "_" : type, j);

                    int typeno = lts_type_get_edge_label_typeno(ltstype, j);
                    trace_get_type_str(trace, typeno, edge_lbls[j], BUFLEN, tmp2);

                    fprintf(output_file, "%s%s = %s",j==0?"":", ", tmp, tmp2);
                }
                fprintf(output_file, "\n");
            } else {
                fprintf(output_file," --- no edge label ---\n");
            }

        }
    }


}

void
output_text_table(trc_t trace, FILE* output_file) {
    lts_type_t ltstype = trc_get_ltstype(trace);
    int N = lts_type_get_state_length(ltstype);
    int eLbls = lts_type_get_edge_label_count(ltstype);
    int sLbls = lts_type_get_state_label_count(ltstype);
    int width_s[N];      // width of state item column
    int width_el[eLbls]; // width of edge label column
    int width_sl[sLbls]; // width of state label column
    char tmp[BUFLEN];

    // calculate width
    for(int j=0; j<N; ++j) {
        // state header
        char *name = lts_type_get_state_name(ltstype, j);
        char *type = lts_type_get_state_type(ltstype, j);
        snprintf(tmp, BUFLEN, "%s:%s", name == NULL ? "_" : name, type == NULL ? "_" : type);
        width_s[j] = strlen(tmp);

    }
    for(int j=0; j<sLbls; ++j) {
        char *name = lts_type_get_state_label_name(ltstype, j);
        char *type = lts_type_get_state_label_type(ltstype, j);
        snprintf(tmp, BUFLEN, "%s:%s", name == NULL ? "_" : name, type == NULL ? "_" : type);
        width_sl[j] = strlen(tmp);
    }
    for(int j=0; j<eLbls; ++j) {
        char *name = lts_type_get_edge_label_name(ltstype, j);
        char *type = lts_type_get_edge_label_type(ltstype, j);
        snprintf(tmp, BUFLEN, "%s:%s", name == NULL ? "_" : name, type == NULL ? "_" : type);
        width_el[j] = strlen(tmp);
    }
    int len = trc_get_length(trace);
    int state_lbls[sLbls];
    int edge_lbls[eLbls];
    for(int i=0; i<len; ++i) {
        int state[N];
        trc_get_state(trace, i, state);

        for(int j=0; j<N; ++j) {
            int typeno = lts_type_get_state_typeno(ltstype, j);
            trace_get_type_str(trace, typeno, state[j], BUFLEN, tmp);
            int len = strlen(tmp);
            if (width_s[j] < len) width_s[j] = len;
        }
        if (trc_get_state_label(trace, i, state_lbls)) {
            for(int j=0; j<sLbls; ++j) {
                int typeno = lts_type_get_state_label_typeno(ltstype, j);
                trace_get_type_str(trace, typeno, state_lbls[j], BUFLEN, tmp);
                int len = strlen(tmp);
                if (width_sl[j] < len) width_sl[j] = len;
            }
        }
        if ((i+1) < len && trc_get_edge_label(trace, i, edge_lbls)) {
            for(int j=0; j<eLbls; ++j) {
                int typeno = lts_type_get_edge_label_typeno(ltstype, j);
                trace_get_type_str(trace, typeno, edge_lbls[j], BUFLEN, tmp);
                int len = strlen(tmp);
                if (width_el[j] < len) width_el[j] = len;
            }
        }
    }

    // print header
    fprintf(output_file, "      ");
    for(int j=0; j<N; ++j) {
        char *name = lts_type_get_state_name(ltstype, j);
        char *type = lts_type_get_state_type(ltstype, j);
        snprintf(tmp, BUFLEN, "%s:%s", name == NULL ? "_" : name, type == NULL ? "_" : type);

        fprintf(output_file, "%s%*s", j==0?"":" ", -width_s[j], tmp);
    }
    if (trc_get_state_label(trace, 0, NULL)) {
        fprintf(output_file, "   ");
        for(int j=0; j<sLbls; ++j) {
            char *name = lts_type_get_state_label_name(ltstype, j);
            char *type = lts_type_get_state_label_type(ltstype, j);
            snprintf(tmp, BUFLEN, "%s:%s", name == NULL ? "_" : name, type == NULL ? "_" : type);

            fprintf(output_file, "%s%*s", j==0?"":" ", -width_sl[j], tmp);
        }
    }
    if (trc_get_edge_label(trace, 0, NULL)) {
        fprintf(output_file, "   ");
        for(int j=0; j<eLbls; ++j) {
            char *name = lts_type_get_edge_label_name(ltstype, j);
            char *type = lts_type_get_edge_label_type(ltstype, j);
            snprintf(tmp, BUFLEN, "%s:%s", name == NULL ? "_" : name, type == NULL ? "_" : type);

            fprintf(output_file, "%s%*s", j==0?"":" ", -width_el[j], tmp);
        }
    }
    fprintf(output_file, "\n");


    // print the state / state labels / edge labels
    for(int i=0; i<len; ++i) {
        int prev_state[N];
        int state[N];
        int prev_state_lbls[sLbls];
        int state_lbls[sLbls];
        int prev_edge_lbls[eLbls];
        int edge_lbls[eLbls];
        for(int j=0; j<N; ++j) prev_state[j] = (i == 0 ? -1 : state[j]);
        trc_get_state(trace, i, state);
        fprintf(output_file, "%.3d: [",i);
        for(int j=0; j<N; ++j) {
            if (arg_all || state[j] != prev_state[j]) {
                int typeno = lts_type_get_state_typeno(ltstype, j);
                trace_get_type_str(trace, typeno, state[j], BUFLEN, tmp);
            } else {
                snprintf(tmp, BUFLEN, "...");
            }

            fprintf(output_file, "%s%*s", j==0?"":" ", width_s[j], tmp);
        }
        fprintf(output_file, "]");

        // print state labels
        if (trc_get_state_label(trace, 0, NULL)) {
            fprintf(output_file, " [");
            for(int j=0; j<sLbls; ++j) prev_state_lbls[j] = (i == 0 ? -1 : state_lbls[j]);
            trc_get_state_label(trace, i, state_lbls);
            for(int j=0; j<sLbls; ++j) {
                if (arg_all || state_lbls[j] != prev_state_lbls[j]) {
                    int typeno = lts_type_get_state_label_typeno(ltstype, j);
                    trace_get_type_str(trace, typeno, state_lbls[j], BUFLEN, tmp);
                } else {
                    snprintf(tmp, BUFLEN, "...");
                }

                fprintf(output_file, "%s%*s", j==0?"":" ", width_sl[j], tmp);
            }
            fprintf(output_file, "]");
        }

        // print edge labels
        if ((i+1)<len) {
            if (trc_get_edge_label(trace, 0, NULL)) {
                fprintf(output_file, " [");
                for(int j=0; j<eLbls; ++j) prev_edge_lbls[j] = (i == 0 ? -1 : edge_lbls[j]);
                trc_get_edge_label(trace, i, edge_lbls);
                for(int j=0; j<eLbls; ++j) {
                    if (arg_all || edge_lbls[j] != prev_edge_lbls[j]) {
                        int typeno = lts_type_get_edge_label_typeno(ltstype, j);
                        trace_get_type_str(trace, typeno, edge_lbls[j], BUFLEN, tmp);
                    } else {
                        snprintf(tmp, BUFLEN, "...");
                    }

                    fprintf(output_file, "%s%*s", j==0?"":" ", width_el[j], tmp);
                }
                fprintf(output_file, "]");
            }
        }
        fprintf(output_file, "\n");
    }
}

void
output_csv(trc_t trace, FILE* output_file) {
    lts_type_t ltstype = trc_get_ltstype(trace);
    int N = lts_type_get_state_length(ltstype);
    int eLbls = lts_type_get_edge_label_count(ltstype);
    int sLbls = lts_type_get_state_label_count(ltstype);

    // add header
    for(int j=0; j<N; ++j) {
        char *name = lts_type_get_state_name(ltstype, j);
        char *type = lts_type_get_state_type(ltstype, j);
        fprintf(output_file, "%s%s:%s", j==0 ? "" : arg_sep, name == NULL ? "_" : name, type == NULL ? "_" : type);
    }
    if (trc_get_state_label(trace,0,NULL)) {
        for(int j=0; j<sLbls; ++j) {
            char *name = lts_type_get_state_label_name(ltstype, j);
            char *type = lts_type_get_state_label_type(ltstype, j);
            fprintf(output_file, "%s%s:%s", arg_sep, name == NULL ? "_" : name, type == NULL ? "_" : type);
        }
    }
    if (trc_get_edge_label(trace,0,NULL)) {
        for(int j=0; j<eLbls; ++j) {
            char *name = lts_type_get_edge_label_name(ltstype, j);
            char *type = lts_type_get_edge_label_type(ltstype, j);
            fprintf(output_file, "%s%s:%s", arg_sep, name == NULL ? "_" : name, type == NULL ? "_" : type);
        }
    }
    fprintf(output_file, "\n");

    int edge_lbls[eLbls];
    int state_lbls[sLbls];
    int state[N];
    int len = trc_get_length(trace);
    // print the state / state labels / edge labels
    for(int i=0; i<len; ++i) {
        char tmp[BUFLEN];
        trc_get_state(trace, i, state);
        for(int j=0; j<N; ++j) {
            int typeno = lts_type_get_state_typeno(ltstype, j);
            trace_get_type_str(trace, typeno, state[j], BUFLEN, tmp);
            fprintf(output_file, "%s%s", j==0 ? "" : arg_sep, tmp);
        }

        // print state labels
        if (trc_get_edge_label(trace, i, edge_lbls)) {
            for(int j=0; j<sLbls; ++j) {
                int typeno = lts_type_get_state_label_typeno(ltstype, j);
                trace_get_type_str(trace, typeno, state_lbls[j], BUFLEN, tmp);
                fprintf(output_file, "%s%s", arg_sep, tmp);
            }
        }

        // printf edge labels
        if (trc_get_edge_label(trace, i, edge_lbls)) {
            for(int j=0; j<eLbls; ++j) {
                if ((i+1)<len) {
                    int typeno = lts_type_get_edge_label_typeno(ltstype, j);
                    trace_get_type_str(trace, typeno, edge_lbls[j], BUFLEN, tmp);
                    fprintf(output_file, "%s%s", arg_sep, tmp);
                } else {
                    fprintf(output_file, "%s", arg_sep);
                }
            }
        }
        fprintf(output_file, "\n");
    }
}

void
output_aut(trc_t trace, FILE* output_file) {
    lts_type_t ltstype = trc_get_ltstype(trace);
    int eLbls = lts_type_get_edge_label_count( ltstype );
    int edge_lbls[eLbls];

    // print header
    int len = trc_get_length(trace);
    fprintf(output_file, "des (%d, %d, %d)\n", 0, len - 1, len);

    // print edges
    char tmp[BUFLEN];
    char typestr[BUFLEN];
    for(int i=0; i < len - 1; ++i) {
        // initialize tmp
        tmp[0] = '\0';

        // todo: escape string when value contains quote (")?

        // get edge label
        int has_labels = trc_get_edge_label(trace, i, edge_lbls);
        if (has_labels) {
            for(int j=0; j<eLbls; ++j) {
                int typeno = lts_type_get_edge_label_typeno(ltstype, j);
                int notype = trc_get_type(trace, typeno, edge_lbls[j], BUFLEN, typestr);
                if (notype || output_value == IDX) {
                    snprintf(&tmp[strlen(tmp)], BUFLEN - strlen(tmp) - 1, "%s%d", j==0 ? "": " ", edge_lbls[j]);
                } else {
                    snprintf(&tmp[strlen(tmp)], BUFLEN - strlen(tmp) - 1, "%s%s", j==0 ? "": " ", typestr);
                }
            }
        } else {
            snprintf(tmp, BUFLEN, "?");
        }
        fprintf(output_file, "(%d, \"%s\", %d)\n", i, tmp, i + 1);
    }
}

int
main(int argc,char*argv[]){
	char           *files[2];
    RTinitPopt(&argc,&argv,options,1,2,files,NULL,"<input> [<output>]",
                "Pretty print trace files\n\n"
                "Supported output file extensions are:\n"
                "  txt: Textual output\n"
                "  aut: Aldebaran file format\n"
                "  csv: Comma separated values\n\n"
                "Options");
    // open file (--file argument or stdout in case of NULL)
    FILE* output_file = stdout;
    if (files[1]) {
        // determine extension
        char *extension = strrchr (files[1], '.');
        if (extension == NULL) {
            Fatal(1,error,"unknown file format extension for file '%s'", files[1]);
        }
        extension++;
        int ff = linear_search (file_formats, extension);
        if (ff < 0) {
            Fatal(1,error,"unknown file format '%s'", extension);
        }
        file_format = ff;

        // open file
        Warning(info,"Writing output to '%s'",files[1]);
        output_file = fopen(files[1],"w");
        if (output_file == NULL) {
            Fatal(1,error,"Could not open file '%s'\n", files[1]);
        }
    }

    trc_t trace=trc_read(files[0]);
    Warning(info,"length of trace is %d",trc_get_length(trace));

    switch (file_format) {
        case FF_TXT:
            if (arg_table) {
                output_text_table(trace, output_file);
            } else {
                output_text(trace, output_file);
            }
            break;
        case FF_CSV:
            output_csv(trace, output_file);
            break;
        case FF_AUT:
            output_aut(trace, output_file);
            break;
        default:
            Fatal(1,error,"File format not yet supported!");
    }

    // close output file
    if (files[1]) fclose(output_file);
}
