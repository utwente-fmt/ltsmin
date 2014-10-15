/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *pinssim.c - LTSmin
 *		Created by Tobias Uebbing on 20140917
 *		Based on LTSmin pins2lts-sym.c
 *		
 *
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// Includes from pins2lts-sym.c
// Which of these are really necessary should be discussed later on
#include "pinssim.h"

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

// poptOption structure for HREaddOptions()
// to be adapted!
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

typedef struct _trace_node{
	int grpNum;
	int * state;
	struct _trace_node * parent;
	int numSuccessors;
    struct _trace_node * successors;
} trace_node;

// Variables necessary for model checking
static lts_type_t ltstype;
static int N;
static int nGrps;
static model_t model;
static int * src;
static matrix_t * initial_DM = NULL;
static trace_node * head;
static trace_node * current;

// I/O variables
static char * files[2];
static char * com[5];
FILE * opf;


static void 
printState(int * state){
	for (int j = 0; j < N; j++){
		 	fprintf(stdout, "%d,", state[j]);
	}
	fprintf(stdout,"\n");
}

static void 
printTransitions(trace_node * node){

	fprintf(stdout,"\n");
	for (int i = 0; i < node->numSuccessors; i++){
		fprintf(stdout, "group %d: ", node->successors[i].grpNum);
		printState(node->successors[i].state);
	}
	if(node->numSuccessors <= 0) fprintf(stdout, "No transitions available from here!");
	fprintf(stdout,"\n");

}

static void 
printNode(trace_node * node){
	fprintf(stdout, "\n");
	fprintf(stdout, "----------------------------------------------------------------\n");
	fprintf(stdout, "Transition that lead here: %d\n", node->grpNum);
	fprintf(stdout, "State of this note:\n");
	printState(node->state);
	fprintf(stdout, "Transitions available from here:\n");
	printTransitions(node);
	if(node->grpNum >= 0){
		fprintf(stdout, "State of parent node:\n");
		printState(node->parent->state);
	}
	else{
		fprintf(stdout, "This is the INITIAL state!\n");
	}
	fprintf(stdout, "----------------------------------------------------------------\n");
	fprintf(stdout, "\n");
}

static void
group_add(void *context, transition_info_t *ti, int *dst, int *cpy){

	trace_node * node = (trace_node*)context;
	fprintf(stdout, "node->grpNum %d\n",node->grpNum);
	node->numSuccessors += 1;
	if (node->numSuccessors==1) node->successors = malloc(sizeof(trace_node)*node->numSuccessors);
	else  node->successors = realloc(node->successors,sizeof(trace_node)*node->numSuccessors);

	node->successors[node->numSuccessors-1].grpNum = ti->group;
	node->successors[node->numSuccessors-1].state = (int*)malloc(sizeof(int)*N);
	memcpy(node->successors[node->numSuccessors-1].state,dst,sizeof(int)*N);
	node->successors[node->numSuccessors-1].parent = (trace_node*)malloc(sizeof(trace_node));
	memcpy(node->successors[node->numSuccessors-1].parent,node,sizeof(trace_node));
	node->successors[node->numSuccessors-1].numSuccessors = 0;

	// trace_node * newNode = (trace_node*)malloc(sizeof(trace_node)+sizeof(int)*N);
	// newNode->grpNum = ti->group;
	// newNode->state = (int*)alloca(sizeof(int)*N);
	// memcpy(newNode->state,dst,sizeof(int)*N);
	// newNode->parent = (trace_node*)alloca(sizeof(trace_node));
	// memcpy(newNode->parent,node,sizeof(trace_node));
	// newNode->numSuccessors = 0;

	// node->successors[node->numSuccessors-1] = *newNode;
	//printNode(&node->successors[node->numSuccessors-1]);
	fprintf(stdout, "group %d: ", ti->group);
	printState(node->successors[node->numSuccessors-1].state);

}

static void
explore_states(trace_node * node){
	fprintf(stdout, "\nTaking transition %d - exploring potential successor states.\n\n", node->grpNum);
	for (int i = 0; i < nGrps; i++){
		 GBgetTransitionsLong(model,i,node->state,group_add,node);
	}
	if(node->numSuccessors > 0){
		fprintf(stdout, "\nFound %d successor states by following transitions:\n\n", node->numSuccessors);
		printTransitions(node);
	}
	else{
		fprintf(stdout, "\nThis states has no successor states\n\n");
	} 
}

static void 
proceed(int index){
	current = (trace_node*)realloc(current, sizeof(current->successors[index]));
	memcpy(current,&(current->successors[index]),sizeof(current->successors[index]));
	explore_states(current);
}

static void
goBack(){
	if(current->grpNum != -1){
		// TO DO:
		// FREE MEMORY OF SUCCESSORS AND THE CURRENT NODE
		current = (trace_node*)realloc(current, sizeof(*(current->parent)));
		memcpy(current,current->parent,sizeof(*(current->parent)));
	}
	else fprintf(stdout, "INFO: This is the INITIAL state! You can't go back!\n");
}


int phraseComLine(char * separator, char * line){

	int n = 0;
	if(strstr(line,separator)){
		char * split = strtok(line,separator);
		com[n] = split;
		while (split!=NULL){
			split = strtok(NULL,separator);
			n++;
			com[n] = split;
		}
	}
	else{
		com[0] = line;
	}

	return n;
}

/*actOnInput()*/
void actOnInput(char * input){

	int nCom= phraseComLine(" ",input);
	fprintf(stdout, "Num of commands %d\n", nCom);

	for(int i = 0; i < nCom; i++) fprintf(stdout, "%s\n", com[i]);

	int transNum;
	sscanf(input,"%d",&transNum);

	int i = 0;
	for (i; i < current->numSuccessors; i++){
		if(current->successors[i].grpNum == transNum){
			//fprintf(stdout, "MATCH!");
			proceed(i);
			break;
		}
	}

	// if(nCom>0){

	// 	char * split = strtok(input,"");
	// 	while (split!=NULL){
	// 		fprintf(stdout, "%s\n",split);
	// 		split = strtok(NULL," ");
	// 		fprintf(stdout, "%s\n",split);
	// 	}

	// 	// if(strcmp(com[0],"explore")){

	// 	// 	fprintf(stdout, "Explore!");
	// 	// }

	// }

	

}

/*handleIO()*/
bool handleIO(){


	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	printf("\nEnter string below - [ctrl + d] to quit\n");

	while((read = getline(&line, &len, stdin)) != -1){
		if(read > 0){
			printf("\n-> read %zd chars from stdin, allocated %zd bytes for line %s",read,len,line);
			actOnInput(line);
		}
		printf("\nEnter string below - [ctrl + d] to quit\n");
	}

	//free (line);
	return 1;

}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *	Main - execution
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int main (int argc, char *argv[]){

	// Initate model and HRE
	HREinitBegin(argv[0]);
    HREaddOptions(options,"representation of the input\n\nOptions");
    lts_lib_setup(); // add options for LTS library

    HREinitStart(&argc,&argv,1,2,files,"<model> [<etf>]");

    // Warning(info, "opening %s", files[0]);
    model = GBcreateBase();

    GBsetChunkMethods(model,HREgreyboxNewmap,HREglobal(),
                      HREgreyboxI2C,
                      HREgreyboxC2I,
                      HREgreyboxCAtI,
                      HREgreyboxCount);

    // const char * extension = strrchr (files[0], '.');
    // printf("Parsed extension: %s \n",extension);

    GBloadFile(model, files[0], &model);
    fprintf(stdout,"pinssim.c - main: loaded file %s \n",files[0]);
  

    if (argc >= 2){
    	opf = fopen(files[1],"w");
    	if (opf != NULL){ 
    		GBprintDependencyMatrixCombined(opf, model);
    		fprintf(stdout,"pinssim.c - main: saved dependency matrix to file %s \n",files[1]);
    	}
    	//GBprintStateLabelMatrix(opf, model);
    }


    initial_DM = GBgetDMInfo(model);
    ltstype = GBgetLTStype(model);
    N = lts_type_get_state_length(ltstype);
    //eLbls = lts_type_get_edge_label_count(ltstype);
    nGrps = dm_nrows(GBgetDMInfo(model));

    fprintf(stdout,"state vector length is %d; there are %d groups\n", N, nGrps);

    //dm_print(stderr, GBgetStateLabelInfo(model));

    src = (int*)alloca(sizeof(int)*N);
    GBgetInitialState(model, src);

    head = (trace_node*)alloca(sizeof(trace_node));
    head->grpNum = -1;
    head->state = (int*)alloca(sizeof(int)*N);
    head->state = src;
    head->parent = NULL;
    head->numSuccessors = 0;

    explore_states(head);

    current = (trace_node*)malloc(sizeof(*head));
    memcpy(current,head,sizeof(*head));

    // printState(src);
    // printState(head->state);
    // printState(current->state);

    printTransitions(head);
    printTransitions(current);

    printNode(head);

    printTransitions(head);

	handleIO();



	// Ask for initial state - specify leave empty for default
	// 

	return 0;

}

