#include "pinsim.h"

// From pins2lts-sym.c
// include "config.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <hre/config.h>
#include <dm/dm.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <pins-lib/pg-types.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/property-semantics.h>
#include <pins-lib/dlopen-api.h>
#include <pins-lib/dlopen-pins.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <spg-lib/spg-solve.h>
#include <vset-lib/vector_set.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/bitset.h>
#include <hre/stringindex.h>

static  struct poptOption options[] = {
//	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)reach_popt , 0 , NULL , NULL },
// #ifdef HAVE_SYLVAN
//     { "order" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &order , 0 , "set the exploration strategy to a specific order" , "<bfs-prev|bfs|chain-prev|chain|par-prev|par>" },
// #else
//     { "order" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &order , 0 , "set the exploration strategy to a specific order" , "<bfs-prev|bfs|chain-prev|chain>" },
// #endif
//     { "saturation" , 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &saturation , 0 , "select the saturation strategy" , "<none|sat-like|sat-loop|sat-fix|sat>" },
//     { "sat-granularity" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &sat_granularity , 0 , "set saturation granularity","<number>" },
//     { "save-sat-levels", 0, POPT_ARG_VAL, &save_sat_levels, 1, "save previous states seen at saturation levels", NULL },
//     { "guidance", 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &guidance, 0 , "select the guided search strategy" , "<unguided|directed>" },
//     { "deadlock" , 'd' , POPT_ARG_VAL , &dlk_detect , 1 , "detect deadlocks" , NULL },
//     { "action" , 0 , POPT_ARG_STRING , &act_detect , 0 , "detect action prefix" , "<action prefix>" },
//     { "invariant", 'i', POPT_ARG_STRING, &inv_detect, 1, "detect invariant violations", NULL },
//     { "no-exit", 'n', POPT_ARG_VAL, &no_exit, 1, "no exit on error, just count (for error counters use -v)", NULL },
//     { "trace" , 0 , POPT_ARG_STRING , &trc_output , 0 , "file to write trace to" , "<lts-file>.gcf" },
//     { "save-transitions", 0 , POPT_ARG_STRING, &transitions_save_filename, 0, "file to write transition relations to", "<outputfile>" },
//     { "load-transitions", 0 , POPT_ARG_STRING, &transitions_load_filename, 0, "file to read transition relations from", "<inputfile>" },
//     { "mu" , 0 , POPT_ARG_STRING , &mu_formula , 0 , "file with a mu formula" , "<mu-file>.mu" },
//     { "ctl-star" , 0 , POPT_ARG_STRING , &ctl_formula , 0 , "file with a ctl* formula" , "<ctl-file>.ctl" },
//     { "dot", 0, POPT_ARG_STRING, &dot_dir, 0, "directory to write dot representation of vector sets to", NULL },
//     { "pg-solve" , 0 , POPT_ARG_NONE , &pgsolve_flag, 0, "Solve the generated parity game (only for symbolic tool).","" },
//     { NULL, 0 , POPT_ARG_INCLUDE_TABLE, spg_solve_options , 0, "Symbolic parity game solver options", NULL},
// #if defined(LTSMIN_PBES)
//     { "pg-reduce" , 0 , POPT_ARG_NONE , &pgreduce_flag, 0, "Reduce the generated parity game on-the-fly (only for symbolic tool).","" },
// #endif
//     { "pg-write" , 0 , POPT_ARG_STRING , &pg_output, 0, "file to write symbolic parity game to","<pg-file>.spg" },
//     { "no-matrix" , 0 , POPT_ARG_VAL , &no_matrix , 1 , "do not print the dependency matrix when -v (verbose) is used" , NULL},
   	SPEC_POPT_OPTIONS,
  	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "PINS options",NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options",NULL},
    POPT_TABLEEND
};

static model_t model;
static char *files[2];

char * getIOString(){


	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	printf("\nEnter string below - [ctrl + d] to quit\n");

	while((read = getline(&line, &len, stdin)) != -1){
		if(read > 0){
			printf("\n-> read %zd chars from stdin, allocated %zd bytes for line %s",read,len,line);
			return line;
		}
		printf("\nEnter string below - [ctrl + d] to quit\n");
	}

	//free (line);
	return line;

}

int main (int argc, char *argv[]){
// From pins2lts-sym.c
	HREinitBegin(argv[0]);
    HREaddOptions(options,"Perform a symbolic reachability analysis of <model>\n"
                    "The optional output of this analysis is an ETF "
                        "representation of the input\n\nOptions");
    lts_lib_setup(); // add options for LTS library

    HREinitStart(&argc,&argv,1,2,files,"<model> [<etf>]");

    // Warning(info, "opening %s", files[0]);
    model = GBcreateBase();

    // GBsetChunkMethods(model,HREgreyboxNewmap,HREglobal(),
    //                   HREgreyboxI2C,
    //                   HREgreyboxC2I,
    //                   HREgreyboxCAtI,
    //                   HREgreyboxCount);

    //HREbarrier(HREglobal());
    const char * extension = strrchr (files[0], '.');
    printf("Parsed extension: %s \n",extension);

    //PINSpluginLoadLanguageModule(extension);
    GBloadFile(model, files[0], &model);
    printf("pinsim.c - main: loaded file\n");
    //HREbarrier(HREglobal());
	
	getIOString();

	// Ask for initial state - specify leave empty for default
	// 

	return 0;

}

