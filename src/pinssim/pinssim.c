/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * pinssim.c - LTSmin
 *		Created by   Tobias J. Uebbing on 20140917
 *		Modified by  -
 *		Based on     LTSmin pins2lts-sym.c & lts-tracepp.c
 *		Copyright    Formal Methods & Tools
 *				     EEMCS faculty - University of Twente - 2014
 *		Supervisor 	 Jeroen Meijer
 *		Description  
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *	 Includes & Definitions
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

//#include "pinssim.h"

// Includes from pins2lts-sym.c
// TODO:
// Which of these are really necessary should be discussed later on

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
// From lts.h:
#include <hre/runtime.h>
#include <lts-lib/lts.h>
#include <util-lib/chunk_support.h>
// -----------
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

// Colors for console output
#define RESET   	"\033[0m"
#define BLACK   	"\033[30m"      	   /* Black */
#define RED     	"\033[31m"     		   /* Red */
#define GREEN   	"\033[32m"      	   /* Green */
#define YELLOW  	"\033[33m"     		   /* Yellow */
#define BLUE    	"\033[34m"  		   /* Blue */
#define MAGENTA 	"\033[35m"      	   /* Magenta */
#define CYAN    	"\033[36m"     		   /* Cyan */
#define WHITE   	"\033[37m"    		   /* White */
#define BOLDBLACK   "\033[1m\033[30m"      /* Bold Black */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDYELLOW  "\033[1m\033[33m"      /* Bold Yellow */
#define BOLDBLUE    "\033[1m\033[34m"      /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */

#define BUFLEN 4096						   /* Buffer length for temporal buffer */

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 Structures & Variables
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
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
    bool isPath;
    int treeDepth;
} trace_node;

// Variables necessary for model checking
static lts_type_t ltstype;
static int N, nGrps,eLbls, sLbls, nGuards;
static int maxTreeDepth;
static model_t model;
static int * src;
static matrix_t * dm_r = NULL;
static matrix_t * dm_may_w = NULL;
static matrix_t * dm_must_w = NULL;
static matrix_t * stateLabel = NULL;
static trace_node * head;
static trace_node * current;

//Options
static bool loopDetection = false;
static bool clearMemOnGoBack = false;
static bool clearMemOnRestart = true;

// I/O variables
FILE * opf;
static char * files[2]; 
static char * com[5];
static int numTextLines = 21;
static char * helpText[21] =
   {"COMMANDS:",
	"  help                           show all options",
	" PRINT OUT",
	"  current                        print info of current node",
	"  state                          print current state",
	"  trans                          print available transitions and subsequent states",
	"  path (states) (rw)             print path of transitions taken (with states and read-write dependencies)",
	"  tree (states)                  print tree of transitions explored (with states)",
	" STATE EXPLORATION",
	"  go / > [TRANSNUMBER]           take transition with TRANSNUMBER and explore potential successor states",
	"  goback / .. (clear/keep)       go back to parent state.",
	"                                 (and clears/keeps the state. See 'set' to change default.)",
	"  restart (clear/keep)           restart exploration from initial state",
	"                                 (and clears/keeps the explored states. See 'set' to change default.)",
	" SETTINGS",
	"  set [OPTION] [VALUE]           Set OPTION to VALUE. OPTIONS:",
	"      loopDetection              true/false - default: false",
	"      clearMemOnGoBack           true/false - default: false",
	"      clearMemOnGoRestart        true/false - default: true",
	" EXIT",
	"  quit / q                       quit PINSsim"};


//From lts.h
// static int arg_all=0;
// static int arg_table=0;
// static char *arg_value="name";
// static enum { FF_TXT, FF_CSV } file_format = FF_TXT;
static char *arg_sep=",";
// static enum { IDX, NAME } output_value = NAME;

// static si_map_entry output_values[] = {
//     {"name",    NAME},
//     {"idx",     IDX},
//     {NULL, 0}
// };

// static si_map_entry file_formats[] = {
//     {"txt",     FF_TXT},
//     {"csv",     FF_CSV},
//     {NULL, 0}
// };
//END IMPORT lts.h ////////////////////////////////////////////

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *	 Print out functionalities
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void
printHelp(){
	for (int i = 0; i < numTextLines; i++) fprintf(stdout, "%s\n", helpText[i]);
}

void
printDMrow(trace_node * node){
	char rw[N];
	for (int i = 0; i < N; i++){
		if (dm_is_set(dm_r, node->grpNum, i) && (dm_is_set(dm_may_w, node->grpNum, i))) {
            rw[i] = '+';
        } else if (dm_is_set(dm_r, node->grpNum, i)) {
            rw[i] = 'r';
        } else if (dm_is_set(dm_must_w, node->grpNum, i)) {
            rw[i] = 'w';
        } else if (dm_is_set(dm_may_w, node->grpNum, i)) {
            rw[i] = 'W';
        } else {
            rw[i] = '-';
        }
		
		int absVal = node->state[i];
		if (absVal < 0) absVal *= -1;
		if (node->state[i]>=10000) fprintf(stdout, "    ");
		else if (node->state[i]>=1000) fprintf(stdout, "   ");
		else if (node->state[i]>=100) fprintf(stdout, "  ");
		else if (node->state[i]>=10) fprintf(stdout, " ");
	 	fprintf(stdout, "%c,",  rw[i]);
	}
	fprintf(stdout,"\n");
}

void 
printState(trace_node * node, bool withRW){
	if(withRW) printDMrow(node);
	for (int i = 0; i < N; i++){
		 	fprintf(stdout, "%d,", node->state[i]);
	}
	fprintf(stdout,"\n");
}

void 
printTransitions(trace_node * node, bool withSuccStates){

	//fprintf(stdout,"\n");
	if(node->numSuccessors > 0){ 
		fprintf(stdout, CYAN "Transitions available from here:\n" RESET);
		for (int i = 0; i < node->numSuccessors; i++){
			int absVal = node->successors[i].grpNum;
			if (absVal < 0) absVal *= -1;
			if(absVal<10)fprintf(stdout, "group     %d - ", node->successors[i].grpNum);
			else if(absVal<100)fprintf(stdout, "group    %d - ", node->successors[i].grpNum);
			else if(absVal<1000)fprintf(stdout, "group   %d - ", node->successors[i].grpNum);
			else if(absVal<10000)fprintf(stdout, "group  %d - ", node->successors[i].grpNum);
			else if(absVal<100000)fprintf(stdout, "group %d - ", node->successors[i].grpNum);
			if (withSuccStates) printState(&(node->successors[i]),0);
		}
		if (!withSuccStates) fprintf(stdout,"\n");
	}
	else fprintf(stdout, " No transitions available from here!\n " CYAN "INFO: " RESET "Enter 'goback' or '..' to go back to the parent state.parent\n");
	
}

void 
printNode(trace_node * node, bool withSuccStates){
	//fprintf(stdout, "\n");
	fprintf(stdout, BOLDWHITE "---- NODE -----------------------\n" RESET);
	if(node->grpNum >= 0){
		fprintf(stdout, CYAN "State of parent node:\n" RESET);
		fprintf(stdout, "              ");
		printState(node->parent,0);
	}
	else{
		fprintf(stdout, "-> This is the " GREEN "INITIAL" RESET " state!\n");
	}
	fprintf(stdout, CYAN "State of this node:\n" RESET);
	fprintf(stdout, "              ");
	printState(node,0);
	printTransitions(node, withSuccStates);
	if(node->grpNum >= 0) fprintf(stdout, CYAN "Transition that lead here: " RESET "%d ", node->grpNum);
	fprintf(stdout, CYAN "Tree depth: " RESET "%d\n", node->treeDepth);
	fprintf(stdout, BOLDWHITE "---- END NODE -------------------\n" RESET);
	//fprintf(stdout, "\n");
}

void 
printPath(trace_node * start, bool withStates, bool withRW){

	trace_node * temp = start;

	fprintf(stdout, "Path from " RED " CURRENT" RESET " to " GREEN "INITIAL" RESET " state:\n\n");
	fprintf(stdout, RED "CURRENT" RESET);

	if(withStates) fprintf(stdout, "\n");
	while(temp->grpNum >= 0){
		if(withStates){
			if (withRW) printState(temp,1);
			else printState(temp, 0);
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
			printState(temp,0);
			fprintf(stdout, GREEN "INITIAL\n" RESET);
		}
		else fprintf(stdout, GREEN " INITIAL\n" RESET);
	}

}

/*printTreeNode()
  Helper function for printTree() printing a node according to its content.*/
static void
printTreeNode(trace_node * node, bool withStates, char * sign){
	for (int i = 0; i < node->treeDepth; i++) fprintf(stdout, " ");
	if(node->isPath) fprintf(stdout, CYAN "%s " RESET, sign);
	else fprintf(stdout, "%s ", sign);
	if (node->grpNum >= 0) fprintf(stdout, "%d ", node->grpNum);
	else fprintf(stdout, GREEN "INITAL " RESET);
	if(withStates){ 
		fprintf(stdout, "- ");
		printState(node,0);
	}
	else fprintf(stdout, "\n");
}

/*printTree()
  Prints a simple tree of 'node' and all its explored successors recursively.
  Showing the transition group only by default. If 'withStates' is true
  also the states will be printed.*/
static void
printTree(trace_node * node, bool withStates, char * sign){
	printTreeNode(node,withStates, sign);
	for (int i = 0; i < node->numSuccessors; i++){
		if(i == node->numSuccessors-1) printTree(&(node->successors[i]),withStates,"\\");
		else printTree(&(node->successors[i]),withStates,"|");
	}
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 Memory deallocation & node reset functionalities
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void 
freeTreeMem(trace_node * node){
	if (node->numSuccessors > 0){
		for (int i = 0; i < node->numSuccessors; i++){
			freeTreeMem(&(node->successors[i]));
		}
		free(node->successors);
		if(node->parent != NULL) node->parent->numSuccessors -= 1;
		if(node->parent == NULL) free(node);
		return;
	}
	else{
		if(node->parent != NULL) node->parent->numSuccessors -= 1;
		if(node->parent == NULL) free(node);
		return;
	} 
}

static void 
resetNode(trace_node * node){
	for (int i = 0; i < node->numSuccessors; i++){
		for (int j = 0; j < node->successors[i].numSuccessors; j++){
			freeTreeMem(&(node->successors[i].successors[j]));
		}
		free(node->successors);
		node->numSuccessors = 0;
		node->explored = false;
	}
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 State exploration functionalities
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


static void
array2PtrArray(int array[], int size, int * ptrArray){
	for (int i = 0; i < size; ++i) ptrArray[i] = array[i];
}

static bool
isSameState(int * s1, int * s2){
	for (int i = 0; i < N; i++) if(s1[i] != s2[i]) return false;
	return true;
}

/*detectLoops()
  Checks whether 'comp->states' occured before in 'start' and its successors*/
static void
detectLoops(trace_node * start, trace_node * comp){
	if(isSameState(start->state,comp->state) && start->treeDepth != comp->treeDepth){
		fprintf(stdout, CYAN "-> Found reoccurence of state:\n" RESET);
		printNode(start,0);
		printNode(comp,0);
	}
	if(start->numSuccessors > 0){
		for (int i = 0; i < start->numSuccessors; i++) detectLoops(&(start->successors[i]),comp);
	}
}

/*group_add()
 Call back function necessary for GBgetTransitionLong(). 
 Creates successor nodes and adds pointers to the the parent node. 
 Checks whether the new explored states occurred before in the search tree
 if 'loopDetection' is set to true by the user.*/
static void
group_add(void *context, transition_info_t *ti, int *dst, int *cpy){
	trace_node * node = (trace_node*)context;
	node->numSuccessors += 1;
	if (node->numSuccessors==1) node->successors = malloc(sizeof(trace_node)*node->numSuccessors);
	else  node->successors = realloc(node->successors,sizeof(trace_node)*node->numSuccessors);

	if (node->treeDepth+1 > maxTreeDepth) maxTreeDepth = node->treeDepth+1;

	node->successors[node->numSuccessors-1].grpNum = ti->group;
	node->successors[node->numSuccessors-1].state = (int*)malloc(sizeof(int)*N);
	memcpy(node->successors[node->numSuccessors-1].state,dst,sizeof(int)*N);
	node->successors[node->numSuccessors-1].parent = node;
	node->successors[node->numSuccessors-1].numSuccessors = 0;
	node->successors[node->numSuccessors-1].explored = 0;
	node->successors[node->numSuccessors-1].isPath = 0;
	node->successors[node->numSuccessors-1].treeDepth = node->treeDepth+1;
 	
 	if(loopDetection) detectLoops(head,&(node->successors[node->numSuccessors-1]));

}

/*explore_states()
  Explores potential successor states of the current node by going through all possible transitions.
  Does not explore an previously explored node again.*/
void
explore_states(trace_node * node, bool printOut){
	
	if(node->explored == 0){
		if (node->grpNum >= 0 && printOut) fprintf(stdout,  MAGENTA "Taking transition %d - exploring potential successor states.\n\n" RESET, node->grpNum);
		for (int i = 0; i < nGrps; i++){
			 GBgetTransitionsLong(model,i,node->state,group_add,node);
		}
		node->explored = 1;
	}
	else fprintf(stdout, CYAN "\n\t INFO:" RESET " Successors already explored!\n\n");
	node->isPath = true;
	if (printOut) printNode(node, 1);
}

/*proceed()
 proceeds from the current node to its successor with index in the successor array.
 Explores successor states of the new current node*/
bool 
proceed(int transNum, bool printOut){
	int i = 0;
	int numSucc = current->numSuccessors;
	while (i < numSucc){
		if(current->successors[i].grpNum == transNum){
			current = &(current->successors[i]);
			explore_states(current,printOut);
			break;
		}
		i++;
	}
	if (i >= numSucc){ 
		fprintf(stdout, RED "\t ERROR: " RESET " this transition is not available from this state.\n\t Enter 'trans' to see all available transitions and there states.\n");
		return false;
	} else return true;
}

/*proceedByState()
 proceeds from the current node to its successor with index in the successor array.
 Explores successor states of the new current node*/
bool 
proceedByState(int * state, bool printOut){
	int i = 0;
	int numSucc = current->numSuccessors;
	while (i < numSucc){
		if(isSameState(current->successors[i].state,state)){
			current = &(current->successors[i]);
			explore_states(current,printOut);
			break;
		}
		i++;
	}
	if (i >= numSucc){ 
		fprintf(stdout, RED "\t ERROR: " RESET " this state is not as successor of this state.\n\t Enter 'trans' to see all available transitions and there states.\n");
		return false;
	} else return true;
}

void
goBack(bool clearMem){
	if(current->grpNum != -1){
		if (clearMem && current->explored) resetNode(current);
		current->isPath = false;
		current = current->parent;
		fprintf(stdout,  MAGENTA "Going back to parent state.\n\n" RESET);
		printNode(current,1);
	}
	else fprintf(stdout, CYAN "\t INFO:" RESET " This is the INITIAL state! You can't go back!\n");
}


void
restart(bool clearMem){
	current = head;
	if(clearMem){
		for (int i = 0; i < current->numSuccessors; i++){
			resetNode(&(current->successors[i]));
			current->successors[i].isPath = false;
		}
	}
	fprintf(stdout,  MAGENTA "Going back to " RESET GREEN "INITIAL" RESET MAGENTA " state.\n\n" RESET);
	printNode(current, 1);
}

/*replayTransitions()
  Attempts to take all transition in the 'transNumbers' array after each other.
  Stops if a transition is at the current state not available.*/
void 
replayTransitions(int transNumbers[], int amountOf, bool printOut){
	int i = 0;
	bool success = true;
	while (i < amountOf && success){
		success = proceed(transNumbers[i],false);
		if(!success) break;
		i++;
	}
	if(!success){ 
		fprintf(stdout, RED "\t ERROR: " RESET " Stopped replaying transitions. Transition at index %d is not available in this state.\n",i);
	 	fprintf(stdout, "\t Enter 'trans' to see all available transitions and successor states.\n");
	} else if (printOut) {
		fprintf(stdout, "\n Took %d transitions leading to following state:\n", amountOf);
		printNode(current,1);
	}
}

/*replayTransitionsByStates()
  Attempts to take all transition in the 'transNumbers' array after each other.
  Stops if a transition is at the current state not available.*/
void 
replayTransitionsByStates(int ** states, int amountOf, bool printOut){
	int * succ_state = (int*)alloca(N*sizeof(int));
	array2PtrArray(states[0],N,succ_state);
	int i;
	if(isSameState(succ_state,current->state)) i = 1;
	else i = 0;
	
	bool success = true;
	while (i < amountOf && success){
		array2PtrArray(states[i],N,succ_state);
		success = proceedByState(succ_state,false);
		if(!success) break;
		i++;
	}
	if(!success){ 
		fprintf(stdout, RED "\t ERROR: " RESET " Stopped replaying transitions. Transition at index %d is not available in this state.\n",i);
	 	fprintf(stdout, "\t Enter 'trans' to see all available transitions and successor states.\n");
	} else if (printOut) {
		fprintf(stdout, "\n Took %d transitions leading to following state:\n", amountOf);
		printNode(current,1);
	}
}

/*trc_get_type_str() from lts-tracepp.c
  TODO: Add description	*/
static void
trace_get_type_str(lts_t trace, int typeno, int type_idx, size_t dst_size, char* dst) {
    if (typeno==-1) Abort("illegal type");
    // if ( output_value == IDX ) {
    //     snprintf(dst, dst_size, "%d", type_idx);
    //     return;
    // }
    switch(lts_type_get_format(trace->ltstype,typeno)){
        case LTStypeDirect:
        case LTStypeRange:
            snprintf(dst, dst_size, "%d", type_idx);
            break;
        case LTStypeChunk:
        case LTStypeEnum:
            {
            chunk c=VTgetChunk(trace->values[typeno],type_idx);
            chunk2string(c,dst_size,dst);
            }
            break;
    }
}

/*trc_get_edge_label() from lts-tracepp.c
  TODO: Add description	*/
static void
trc_get_edge_label (lts_t trace, int i, int * dst){
    if (trace->edge_idx) TreeUnfold(trace->edge_idx, trace->label[i], dst);
    else dst[0]=trace->label[i];
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 I/O functionalities
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*parseComLine()
 splits a char array into separate char arrays by the separator*/
int parseComLine(char * separator, char * line){
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

/*handleIO()
  Takes a input a char array read from the console. Splits it up in separate commands.
  Executes functioanlities according to commands and arguments.*/
bool handleIO(char * input){

	int nCom = parseComLine(" ", input);

	//////////////////////////////////////////
	// PRINT OUT COMMANDS
	if (strcmp(com[0],"help") == 0) printHelp();
	else if (strcmp(com[0],"current") == 0) printNode(current, 1);
	else if (strcmp(com[0],"state") == 0) printState(current,0);
	else if (strcmp(com[0],"trans") == 0) printTransitions(current, 1);
	else if (strcmp(com[0],"path") == 0){
		if (nCom >= 2){
			if(strcmp(com[1],"states") == 0){
				if (nCom >= 3 && strcmp(com[2],"rw") == 0) printPath(current, 1, 1);
				else printPath(current, 1, 0);
			} 
			else fprintf(stdout, RED "\t ERROR: " RESET " Entered argument for 'path' unknown\n\t See 'help' for description.\n");
		}
		else printPath(current, 0, 0);
	}
	else if (strcmp(com[0],"tree") == 0){
		if (nCom >= 2){
			if(strcmp(com[1],"states") == 0) printTree(head, 1, "\\");
			else fprintf(stdout, RED "\t ERROR: " RESET " Entered argument for 'tree' unknown\n\t See 'help' for description.\n");
		}
		else printTree(head, 0, "\\");
	}
	//////////////////////////////////////////
	// EXPLORATION COMMANDS
	else if (strcmp(com[0],"go") == 0 || strcmp(com[0],">") == 0){
		if (nCom >= 2){
			int transNum;
			sscanf(com[1],"%d",&transNum);
			proceed(transNum,true);
		}
		else fprintf(stdout, RED "\t ERROR: " RESET " explore needs as argument the number of the transition to take.\n");
	}
	else if (strcmp(com[0],"goback") == 0 || strcmp(com[0],"..") == 0){ 
		if (nCom >= 2){
			if (strcmp(com[1],"clear") == 0) goBack(1);
			else if (strcmp(com[1],"keep") == 0) goBack(0);
			else fprintf(stdout, RED "\t ERROR: " RESET " unknown argument for 'goback'.\n");
		}
		else goBack(clearMemOnGoBack);
	}
	else if (strcmp(com[0],"restart") == 0){
		if (nCom >= 2){
			if (strcmp(com[1],"clear") == 0) restart(1);
			else if (strcmp(com[1],"keep") == 0) restart(0);
			else fprintf(stdout, RED "\t ERROR: " RESET " unknown argument for 'restart'.\n");
		}
		else restart(clearMemOnRestart);
	}
	//////////////////////////////////////////
	// CHANGE SETTING COMMAND
	else if (strcmp(com[0],"set") == 0){
		if(nCom >= 3){
			if (strcmp(com[1],"loopDetection") == 0){ 
				if(strcmp(com[2],"true") == 0 || strcmp(com[2],"1") == 0){
					loopDetection = 1;
					fprintf(stdout, CYAN "\t set" RESET " loopDetection = true\n");
				}
				if(strcmp(com[2],"false") == 0 || strcmp(com[2],"0") == 0){
					loopDetection = 0;
					fprintf(stdout, CYAN "\t set" RESET " loopDetection = false\n");
				}
			}
			else if (strcmp(com[1],"clearMemOnGoBack") == 0){ 
				if(strcmp(com[2],"true") == 0 || strcmp(com[2],"1") == 0){
					clearMemOnGoBack = 1;
					fprintf(stdout, CYAN "\t set" RESET " clearMemOnGoBack = true\n");
				}
				if(strcmp(com[2],"false") == 0 || strcmp(com[2],"0") == 0){
					clearMemOnGoBack = 0;
					fprintf(stdout, CYAN "\t set" RESET " clearMemOnGoBack = false\n");
				}
			}
			else if (strcmp(com[1],"clearMemOnRestart") == 0){ 
				if(strcmp(com[2],"true") == 0 || strcmp(com[2],"1") == 0){
					clearMemOnRestart = 1;
					fprintf(stdout, CYAN "\t set" RESET " clearMemOnRestart = true\n");
				}
				if(strcmp(com[2],"false") == 0 || strcmp(com[2],"0") == 0){
					clearMemOnRestart = 0;
					fprintf(stdout, CYAN "\t set" RESET " clearMemOnRestart = false\n");
				}
			}
		}
		else fprintf(stdout, RED "\t ERROR: " RESET " 'set' needs a option name and a values as argument.\n\t See 'help' for description.\n");
	}
	//////////////////////////////////////////
	// EXIT COMMAND & DEFAULT ERROR
	else if ((strcmp(com[0],"q") == 0)||(strcmp(com[0],"quit") == 0)) return 0;
	else fprintf(stdout, RED "\t ERROR: " RESET " Command unknown - enter 'help' for available options.\n");
	return 1;
}

/*runIO()*/
void runIO(){

	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	bool run = true;

	fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
	fprintf(stdout, CYAN "\nEnter command below - 'help' for options - 'quit' to quit:\n" RESET);

	while((read = getline(&line, &len, stdin)) != -1 && run){
		if(read > 0){
			//printf("\n-> read %zd chars from stdin, allocated %zd bytes for line %s",read,len,line);
			line[strlen(line)-1] = '\0';
			fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n\n");
			run = handleIO(line);
			if (!run) break;
		}
		fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
		fprintf(stdout, CYAN "\nEnter command below - 'help' for options - 'quit' to quit:\n" RESET);
	}
	//free (line);
}

/* * SECTION * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	 Main - execution
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

    // Load model from file
    GBloadFile(model, files[0], &model);
    fprintf(stdout,CYAN "INFO: " RESET " Loaded file: %s \n",files[0]);
  
    // if (argc >= 2){
    // 	opf = fopen(files[1],"w");
    // 	if (opf != NULL){ 
    // 		GBprintDependencyMatrixCombined(opf, model);
    // 		fprintf(stdout, CYAN "INFO:" RESET " Saved dependency matrix to file: %s \n",files[1]);
    // 	}
    // 	//GBprintStateLabelMatrix(opf, model);
    // }

    // Initialize global variables
    dm_r = GBgetExpandMatrix(model);	        //Dependency matrices
    dm_may_w = GBgetDMInfoMayWrite(model);
    dm_must_w = GBgetDMInfoMustWrite(model);
    stateLabel = GBgetStateLabelInfo(model);	// State labels

    ltstype = GBgetLTStype(model);				//LTS Type
    N = lts_type_get_state_length(ltstype);		//State length
    eLbls = lts_type_get_edge_label_count(ltstype);
    sLbls = lts_type_get_state_label_count(ltstype);
    nGrps = dm_nrows(GBgetDMInfo(model));
    if (GBhasGuardsInfo(model)){
        nGuards = GBgetStateLabelGroupInfo(model, GB_SL_ALL)->count;
        fprintf(stdout,CYAN "INFO: " RESET " Number of guards %d.", nGuards);
    }
        
  

    fprintf(stdout,CYAN "INFO: " RESET " State vector length is %d; there are %d groups\n", N, nGrps);

    src = (int*)alloca(sizeof(int)*N);
    GBgetInitialState(model, src);

    maxTreeDepth = 0;

    head = (trace_node*)malloc(sizeof(trace_node));
    head->grpNum = -1;
    head->state = (int*)malloc(sizeof(int)*N);
    head->state = src;
    head->parent = NULL;
    head->numSuccessors = 0;
    head->explored = 0;
    head->treeDepth = maxTreeDepth;

    fprintf(stdout,"\n-----------------------------------------------------------------------------------------------\n");
    fprintf(stdout, GREEN "\nINITIAL " RESET "state:\n");
    explore_states(head,true);

    // Set current node to head
    current = head;

    // int *replayTest = (int*)alloca(sizeof(int)*8);
    // replayTest = (int[8]){130,56,127,99,130,101,127,79};
    // replayTransitions(replayTest,8,true);
    
    if (argc >= 2){
    	fprintf(stdout, "\n");
    	opf = stdout;
    	lts_t trace = lts_create();
    	lts_read(files[1],trace);
    	fprintf(stdout,CYAN "\t INFO: " RESET " Loaded trace from file: %s \n\n",files[1]);
    	if (lts_type_get_state_length(trace->ltstype) != N ||
    	    lts_type_get_edge_label_count(trace->ltstype) != eLbls ||
    		lts_type_get_state_label_count(trace->ltstype) != sLbls){
    		fprintf(stdout, RED "\t ERROR:" RESET " This trace does not match the loaded model.\n");
    	}
    	else{
    		fprintf(stdout, CYAN "\t INFO:" RESET " Parameteres of trace match model.\n");
    		int ** trace_states = (int**)alloca(trace->transitions*sizeof(int*));
    		//char tmp[BUFLEN];
            for(uint32_t x=0; x<=trace->transitions; ++x) {
            	uint32_t i = (x != trace->transitions ? x : trace->dest[x-1]);
            	trace_states[i] = (int*)alloca(N*sizeof(int));
            	if (N){
            		int temp[N];
            		TreeUnfold(trace->state_db, i, temp);
            		array2PtrArray(temp,N,trace_states[i]);
            	}
          //   	if (trace->label != NULL) {
		        //     int edge_lbls[eLbls];
		        //     trc_get_edge_label(trace, i, edge_lbls);
		        //     //replayTransitions(edge_lbls,eLbls,1);
		        //     for(int j=1; j<eLbls; ++j) {
		        //         if ((i+1)<trace->states) {
		        //             int typeno = lts_type_get_edge_label_typeno(trace->ltstype, j);
		        //             trace_get_type_str(trace, typeno, edge_lbls[j], BUFLEN, tmp);
		        //             fprintf(opf, "%s%s\n", arg_sep, tmp);
		        //             fprintf(opf, "edge_lbls[%d]: %d\n", j, edge_lbls[j]);
		        //             chunk c = chunk_str(tmp);
    						// int act_index = GBchunkPut(model, typeno, c);
    						// if (GBhasGuardsInfo(model)){
	    					// 	int labels[189];
	    					// 	for (int k = 0; k < 189; k++)
	        	// 					labels[k] = edge_lbls[j] == k ? act_index : -1;
	    					// 	int transNum = -1;
	    					// 	int i = 0;
	    					// 	while (i < nGrps){
	    					// 		if (GBtransitionInGroup(model, labels, i)){
	    					// 			transNum = i;
	    					// 			break;
	    					// 		}
	    					// 		i++;
	    					// 	}
	    					// 	fprintf(opf, "Group Transition number: %d\n", transNum);
	    					// }
		        //         } else {
		        //             //fprintf(opf, "%s", arg_sep);
		        //         }
		        //     }
		        // }
       		}
       		for (uint32_t i = 0; i < trace->transitions; ++i){
       			for (int j = 0; j < N; ++j){
       				fprintf(stdout, "%d,", trace_states[i][j]);
       			}
       			fprintf(stdout, "\n");
       		}
       		replayTransitionsByStates(trace_states,(int)trace->transitions,1);

    	}
    }

    // Start IO procedure
	runIO();

	// Free allocated memory before exit 
	freeTreeMem(head);
	head = NULL;
	current = NULL;
	dm_free(dm_r);
	dm_free(dm_may_w);
	dm_free(dm_must_w);
	//dm_free(stateLabel);

	return 0;
}

 // trc_env_t          *trace = RTmalloc(sizeof(trc_env_t));
 //    lts_type_t          ltstype = GBgetLTStype (model);
 //    trace->N = lts_type_get_state_length (ltstype);
 //    trace->state_labels = lts_type_get_state_label_count (ltstype);
 //    trace->get_state = get;
 //    trace->get_state_arg = arg;
 //    trace->model = model;

