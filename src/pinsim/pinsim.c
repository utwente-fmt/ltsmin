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

// #include <hre/config.h>
#include <dm/dm.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <pins-lib/pg-types.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/property-semantics.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
// #include <util-lib/dynamic-array.h>
// #include <util-lib/bitset.h>
#include <hre/stringindex.h>

static model_t model;
static char *files[2];



char * getIOString(){


	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	printf("\nEnter string below - [ctrl + d] to quit\n");

	while((read = getline(&line, &len, stdin)) != -1){
		if(read > 0){
			printf("\n read %zd chars from stdin, allocated %zd bytes for line %s",read,len,line);
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
    // HREaddOptions(options,"Perform a symbolic reachability analysis of <model>\n"
    //                "The optional output of this analysis is an ETF "
    //                    "representation of the input\n\nOptions");
    lts_lib_setup(); // add options for LTS library

    HREinitStart(&argc,&argv,1,2,files,"<model> [<etf>]");

    // Warning(info, "opening %s", files[0]);
    model = GBcreateBase();

    // GBsetChunkMethods(model,HREgreyboxNewmap,HREglobal(),
    //                   HREgreyboxI2C,
    //                   HREgreyboxC2I,
    //                   HREgreyboxCAtI,
    //                   HREgreyboxCount);

    GBloadFile(model, files[0], &model);
	
	getIOString();

	return 0;

}

