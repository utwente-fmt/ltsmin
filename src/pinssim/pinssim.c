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

#define RESET   "\033[0m"
#define BLACK   "\033[30m"      /* Black */
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
#define WHITE   "\033[37m"      /* White */
#define BOLDBLACK   "\033[1m\033[30m"      /* Bold Black */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDYELLOW  "\033[1m\033[33m"      /* Bold Yellow */
#define BOLDBLUE    "\033[1m\033[34m"      /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */

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
    bool explored;
    int treeDepth;
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
FILE * opf;
static char * files[2]; 
static int numCommands = 8;
static char * com[5];
static char * helpText[8] =
   {"\n COMMANDS:",
	"\t help                 \t show all options",
	"\t current              \t print info of current node",
	"\t state                \t print current state",
	"\t trans                \t print available transitions and subsequent states",
	"\t path (states)        \t print path of transitions (with states) from CURRENT to INITIAL",
	"\t take [TRANSNUMBER]   \t take transition with TRANSNUMBER and explore potential successor states",
	"\t goback               \t go back to parent state"};


static void
printHelp(){
	for (int i = 0; i < numCommands; i++) fprintf(stdout, "%s\n", helpText[i]);
}

static void 
printState(int * state){
	for (int j = 0; j < N; j++){
		 	fprintf(stdout, "%d,", state[j]);
	}
	fprintf(stdout,"\n");
}

static void 
printTransitions(trace_node * node, bool withSuccStates){

	//fprintf(stdout,"\n");
	if(node->numSuccessors > 0){ 
		fprintf(stdout, "Transitions available from here:\n");
		for (int i = 0; i < node->numSuccessors; i++){
			fprintf(stdout, "group %d - ", node->successors[i].grpNum);
			if (withSuccStates) printState(node->successors[i].state);
		}
		if (!withSuccStates) fprintf(stdout,"\n");
	}
	else fprintf(stdout, "No transitions available from here!\n Enter 'goback' to go back to the parent state.parent\n");
	

}

static void 
printNode(trace_node * node, bool withSuccStates){
	//fprintf(stdout, "\n");
	fprintf(stdout, "-------------- NODE -------------\n");
	if(node->grpNum >= 0){
		fprintf(stdout, "State of parent node:\n");
		printState(node->parent->state);
	}
	else{
		fprintf(stdout, "This is the " GREEN "INITIAL" RESET " state!\n");
	}
	fprintf(stdout, "State of this node:\n");
	printState(node->state);
	if(node->grpNum >= 0) fprintf(stdout, "Transition that lead here: %d\n", node->grpNum);
	printTransitions(node, withSuccStates);
	fprintf(stdout, "---------------------------------\n");
	//fprintf(stdout, "\n");
}

static void 
printPath(trace_node * start, bool withStates){

	trace_node * temp = start;

	fprintf(stdout, "Path from " RED " CURRENT" RESET " to " GREEN "INITIAL" RESET " state:\n\n");
	fprintf(stdout, RED "CURRENT" RESET);

	if(withStates) fprintf(stdout, "\n");
	while(temp->grpNum >= 0){
		if(withStates){
			printState(temp->state);
			fprintf(stdout, BOLDWHITE "  |\n" RESET);
			fprintf(stdout, "%d\n", temp->grpNum);
			fprintf(stdout, BOLDWHITE "  |\n" RESET);
		}
		else{
			fprintf(stdout, " %d ", temp->grpNum);
			fprintf(stdout, BOLDWHITE "->" RESET);
		}
		temp = temp->parent;
	}
	if (temp->grpNum < 0){
		if(withStates){
			printState(temp->state);
			fprintf(stdout, GREEN "INITIAL\n" RESET);
		}
		else fprintf(stdout, GREEN " INITIAL\n" RESET);
	}

}

static void 
freeNodeMem(trace_node * node){
	//free(node->parent);
	//free(node->state);
	if (node->numSuccessors > 0){
		for (int i = 0; i < node->numSuccessors; i++){
			freeNodeMem(&(node->successors[i]));
		}
		//free(node->successors);
	}
	free(node);
}

static void
group_add(void *context, transition_info_t *ti, int *dst, int *cpy){

	fprintf(stdout, "ENTERED group_add()\n");
	trace_node * node = (trace_node*)context;
	if(node->grpNum >= 0) printTransitions(node->parent,1);
	node->numSuccessors += 1;
	if (node->numSuccessors==1) node->successors = malloc(sizeof(trace_node)*node->numSuccessors);
	else  node->successors = realloc(node->successors,sizeof(trace_node)*node->numSuccessors);

	if(node->grpNum >= 0) printTransitions(node->parent,1);
	node->successors[node->numSuccessors-1].grpNum = ti->group;
	node->successors[node->numSuccessors-1].state = (int*)malloc(sizeof(int)*N);
	memcpy(node->successors[node->numSuccessors-1].state,dst,sizeof(int)*N);
	node->successors[node->numSuccessors-1].parent = (trace_node*)malloc(sizeof(trace_node));
	memcpy(node->successors[node->numSuccessors-1].parent,node,sizeof(trace_node));
	node->successors[node->numSuccessors-1].numSuccessors = 0;
	node->successors[node->numSuccessors-1].explored = 0;
	node->successors[node->numSuccessors-1].treeDepth = node->treeDepth+1;
    if(node->grpNum >= 0) printTransitions(node->parent,1);
	// TESTs
	//printNode(&node->successors[node->numSuccessors-1]);
	//fprintf(stdout, "group %d: ", ti->group);
	//printState(node->successors[node->numSuccessors-1].state);

}

static void
explore_states(trace_node * node){
	
	fprintf(stdout, "ENTERED explore_states()\n");
	if(node->explored == 0){
		fprintf(stdout, "Taking transition %d - exploring potential successor states.\n\n", node->grpNum);
		if(node->grpNum >= 0) printTransitions(node->parent,1);
		for (int i = 0; i < nGrps; i++){
			 GBgetTransitionsLong(model,i,node->state,group_add,node);
		}
		if(node->grpNum >= 0) printTransitions(node->parent,1);
	}
	else fprintf(stdout, CYAN "\n\t INFO:" RESET " Successors already explored!\n");
	printNode(node, 1);
}

static void 
proceed(int index){
	//current = (trace_node*)realloc(current, sizeof(current->successors[index]));
	//memcpy(current,&(current->successors[index]),sizeof(current->successors[index]));
	current = &(current->successors[index]);
	explore_states(current);
}

static void
goBack(){
	if(current->grpNum != -1){
		printTransitions(current->parent,1);
		trace_node * temp = current;
		current = current->parent;
		//freeNodeMem(temp);
		printTransitions(current,1);
		printNode(current, 1);
	}
	else fprintf(stdout, CYAN "\t INFO:" RESET " This is the INITIAL state! You can't go back!\n");
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
		com[n] = line;
		n++;
	}

	return n;
}

/*actOnInput()*/
void handleIO(char * input){

	int nCom= phraseComLine(" ",input);
	
	//TEST to control parsed input commands
	//fprintf(stdout, "Num of commands %d\n", nCom);
	//for(int i = 0; i < nCom; i++) fprintf(stdout, "|%s|\n", com[i]);

	if (strcmp(com[0],"help") == 0) printHelp();
	else if (strcmp(com[0],"current") == 0) printNode(current, 0);
	else if (strcmp(com[0],"state") == 0) printState(current->state);
	else if (strcmp(com[0],"trans") == 0) printTransitions(current, 1);
	else if (strcmp(com[0],"path") == 0){
		if (nCom >= 2){
			if(strcmp(com[1],"states") == 0) printPath(current, 1);
			else fprintf(stdout, RED "\t ERROR: " RESET " Entered argument for 'path' unknown\n\t See 'help' for description.\n");
		}
		else printPath(current, 0);
	} 
	else if (strcmp(com[0],"take") == 0){
		if (nCom >= 2){
			int transNum;
			sscanf(com[1],"%d",&transNum);
			
			int i = 0;
			int numSucc = current->numSuccessors;
			while (i < numSucc){
				if(current->successors[i].grpNum == transNum){
					printTransitions(current,1);
					proceed(i);
					printTransitions(current->parent,1);
					break;
				}
				i++;
			}
			if (i >= numSucc) fprintf(stdout, RED "\t ERROR: " RESET " this transition is not available from this state.\n\t Enter 'trans' to see all available transitions and there states.");
		}
		else fprintf(stdout, RED "\t ERROR: " RESET " explore needs as argument the number of the transition to take.\n");
	}
	else if (strcmp(com[0],"goback") == 0) goBack();
	else fprintf(stdout, RED "\t ERROR: " RESET " Command unknown - enter 'help' for available options.\n");

}

/*runIO()*/
void runIO(){

	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
	fprintf(stdout, CYAN "\nEnter command below - help for options -[ctrl + d] to quit\n" RESET);

	while((read = getline(&line, &len, stdin)) != -1){
		if(read > 0){
			//printf("\n-> read %zd chars from stdin, allocated %zd bytes for line %s",read,len,line);
			line[strlen(line)-1] = '\0';
			fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n\n");
			handleIO(line);
		}
		fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
		fprintf(stdout, CYAN "\nEnter command below - help for options -[ctrl + d] to quit\n" RESET);
	}
	//free (line);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *	Main - execution
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int main (int argc, char *argv[]){

	fprintf(stdout, CYAN "\n Start up PINSsim\n" RESET);
	fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
	
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
    fprintf(stdout,CYAN "INFO: " RESET " Loaded file: %s \n",files[0]);
  

    if (argc >= 2){
    	opf = fopen(files[1],"w");
    	if (opf != NULL){ 
    		GBprintDependencyMatrixCombined(opf, model);
    		fprintf(stdout, CYAN "INFO:" RESET " Saved dependency matrix to file: %s \n",files[1]);
    	}
    	//GBprintStateLabelMatrix(opf, model);
    }


    initial_DM = GBgetDMInfo(model);
    ltstype = GBgetLTStype(model);
    N = lts_type_get_state_length(ltstype);
    //eLbls = lts_type_get_edge_label_count(ltstype);
    nGrps = dm_nrows(GBgetDMInfo(model));

    fprintf(stdout,CYAN "INFO: " RESET " State vector length is %d; there are %d groups\n", N, nGrps);

    //dm_print(stderr, GBgetStateLabelInfo(model));

    src = (int*)alloca(sizeof(int)*N);
    GBgetInitialState(model, src);

    head = (trace_node*)malloc(sizeof(trace_node));
    head->grpNum = -1;
    head->state = (int*)malloc(sizeof(int)*N);
    head->state = src;
    head->parent = NULL;
    head->numSuccessors = 0;
    head->explored = 0;
    head->treeDepth = 0;

    fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
    fprintf(stdout, GREEN "INITIAL STATE:\n" RESET);
    explore_states(head);

    //current = (trace_node*)malloc(sizeof(*head));
    current = head;
    //memcpy(current,head,sizeof(*head));

	runIO();

	return 0;

}

