#include <hre/config.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CAESAR_GRAPH_IMPLEMENTATION 1
#include <caesar_standard.h>
#include <caesar_graph.h>

#include <hre/user.h>
#include <hre-io/user.h>
#include <lts-io/user.h>
#include <pins-lib/pins-impl.h>
#include <util-lib/fast_hash.h>
#include <hre/stringindex.h>
#include <util-lib/treedbs.h>

static model_t model;
static lts_type_t ltstype;

static int N;
static int K;
static int state_labels;
static int edge_labels;
static int edge_size;
static int edge_encode=0;

static void ltsminopen_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST:
		break;
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
}

static  struct poptOption options[] = {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION  , (void*)ltsminopen_popt , 0 , NULL , NULL },
	SPEC_POPT_OPTIONS,
        { "edge-encode" , 0 , POPT_ARG_VAL, &edge_encode , 1 , "encode the state labels on edges" , NULL },
/*
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_setonly_options , 0 , "Vector set options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL , NULL },
*/
	POPT_TABLEEND
};

/* ADMINISTRATIVE STUFF */

CAESAR_TYPE_STRING CAESAR_GRAPH_COMPILER() {
	return "pins_open";
}

CAESAR_TYPE_VERSION CAESAR_GRAPH_VERSION() {
	return 2.14; /* TODO: what version to return??? */
}

/* ENCODING OF STATES AND LABELS */

typedef  struct CAESAR_STRUCT_STATE { int state[0]; } CAESAR_BODY_STATE;
typedef  struct CAESAR_STRUCT_LABEL { int label[0]; } CAESAR_BODY_LABEL;

/*
typedef  CAESAR_TYPE_ABSTRACT(CAESAR_STRUCT_STATE) CAESAR_TYPE_STATE;
typedef  CAESAR_TYPE_ABSTRACT(CAESAR_STRUCT_LABEL) CAESAR_TYPE_LABEL;
*/

CAESAR_TYPE_NATURAL CAESAR_HINT_SIZE_STATE;
CAESAR_TYPE_NATURAL CAESAR_HINT_SIZE_LABEL;

CAESAR_TYPE_NATURAL CAESAR_HINT_HASH_SIZE_STATE;
CAESAR_TYPE_NATURAL CAESAR_HINT_HASH_SIZE_LABEL;

CAESAR_TYPE_NATURAL CAESAR_HINT_ALIGNMENT_STATE = sizeof(int); /* was 4 */
CAESAR_TYPE_NATURAL CAESAR_HINT_ALIGNMENT_LABEL = sizeof(int); /* was 4 */

CAESAR_TYPE_BOOLEAN CAESAR_COMPARE_STATE(CAESAR_TYPE_STATE s1, CAESAR_TYPE_STATE s2) {
	int i;
	 for(i=0; i < N; i++) {
		if (s1->state[i] != s2->state[i])
			return CAESAR_FALSE;
	}
	return CAESAR_TRUE;
}

CAESAR_TYPE_BOOLEAN CAESAR_COMPARE_LABEL(CAESAR_TYPE_LABEL l1, CAESAR_TYPE_LABEL l2) {
	int i;
	for(i=0; i < edge_labels; i++) {
		if (l1->label[i] != l2->label[i])
			return CAESAR_FALSE;
	}
	return CAESAR_TRUE;
}

/* PRINTING OF STATES AND LABELS */

static int state_format = 0;
static int label_format = 0;
CAESAR_TYPE_NATURAL CAESAR_MAX_FORMAT_LABEL(void) {return 2;}
CAESAR_TYPE_NATURAL CAESAR_MAX_FORMAT_STATE(void) {return 1;}

void CAESAR_FORMAT_STATE(CAESAR_TYPE_NATURAL x) {
	if (x<=CAESAR_MAX_FORMAT_STATE())
		state_format = x;
	else
		CAESAR_ERROR("pins_open: %d exceeds max state format",x);
}

void CAESAR_FORMAT_LABEL(CAESAR_TYPE_NATURAL x) {
	if (x<=CAESAR_MAX_FORMAT_LABEL())
		label_format = x;
	else
		CAESAR_ERROR("pins_open: %d exceeds max label format",x);
}

void CAESAR_PRINT_STATE(CAESAR_TYPE_FILE f,CAESAR_TYPE_STATE s) {
	int i;
	chunk c;
	int labels[state_labels];
	
	if (state_format==0) {
		fprintf(f, "state [");
		for(i=0; i < N; i++) {
			if (i>0)
				fprintf(f, ",");
			fprintf(f, "%d", s->state[i]);
		}
		fprintf(f, "]\n");
	} else if (state_format==1) {
		GBgetStateLabelsAll(model,s->state,labels);
		fprintf(f, "state <");
		for(i=0; i < state_labels; i++) {
			if (i>0)
				fprintf(f, ";");
			c=GBchunkGet(model,lts_type_get_state_label_typeno(ltstype,i),labels[i]);
			char str[c.len*2+6];
			chunk2string(c,sizeof str,str);
			fprintf(f, "%s", str);
		}
		fprintf(f, ">");
	}
}

void CAESAR_PRINT_LABEL(CAESAR_TYPE_FILE f,CAESAR_TYPE_LABEL l) {
	fputs(CAESAR_STRING_LABEL(l), f);
 }

CAESAR_TYPE_STRING CAESAR_STRING_LABEL(CAESAR_TYPE_LABEL l) {
	static char *s = NULL;  /* start of string */
	static char *b = NULL; /*beyond*/
	static char *tau = "i";
	char *p = NULL;  /* current insertion point */
	int u, n; /* used, needed */
	int i;
	chunk c;
	size_t clen;
	
	p = s;
	n = 5;
	if (b-p < n) { 
		u = p-s;
		s = RTrealloc(s, u+n);/* TODO: check s!=0 */
		p = s+u;
		b = s + u+n;
	}
    if (edge_labels > 0 && l->label[0]<0){
        n = 6;
        if (b-p < n) { 
            u = p-s;
            s = RTrealloc(s, u+n); /* TODO: check s!=0 */
            p = s+u;
            b = s + u+n;
        }
        sprintf(p, "delta");
        p+=strlen(p);
    } else {
        if (edge_labels > 1 || edge_encode) {
                sprintf(p, "|");
                p += strlen(p);
        }
        for(i=0; i < edge_labels; i++) {
            char *name=lts_type_get_edge_label_name(ltstype,i);
            c=GBchunkGet(model,lts_type_get_edge_label_typeno(ltstype,i),l->label[i]);
            if (c.len==3 && strncmp(c.data, "tau", c.len)==0)
                clen=strlen(tau);
            else
                clen=c.len*2+6;
            n = strlen(name)+ 1 + clen+1+1+1; /* for name , '=' , c, ';',  '>', '\0' */
            if (b-p < n) { 
                u = p-s;
                s = RTrealloc(s, u+n); /* TODO: check s!=0 */
                p = s+u;
                b = s + u+n;
            }
            if (i>0) {
                sprintf(p, "|");
                p += strlen(p);
            }
            if (edge_labels > 1 || edge_encode ) {
                sprintf(p, "%s=",name);
                p += strlen(p);
            }
            if (c.len==3 && strncmp(c.data, "tau", c.len)==0)
                sprintf(p, "%s", tau);
            else
                chunk2string(c,b-p,p);
            p += strlen(p);
        }
    }
    if (edge_labels > 1 || edge_encode ) {
        sprintf(p, "|");
        p += strlen(p);
    }
    if (edge_encode){
        int ofs=edge_labels;
        /*
        for(i=0;i<N;i++){
            char*name=lts_type_get_state_name(ltstype,i);
            c=GBchunkGet(model,lts_type_get_state_typeno(ltstype,i),l->label[ofs+i]);
            n=strlen(name)+c.len*2+7;
            if (b-p < n) { 
                u = p-s;
                s = realloc(s, u+n); // TODO: check s!=0
                p = s+u;
                b = s + u+n;
            }
            sprintf(p, "%s=",name);
            p+=strlen(p);
            chunk2string(c,b-p,p);
            p+=strlen(p);
            sprintf(p, "|");
            p +=strlen(p);
        }
        */
        ofs+=N;
        for(i=0;i<state_labels;i++){
            char*name=lts_type_get_state_label_name(ltstype,i);
            c=GBchunkGet(model,lts_type_get_state_label_typeno(ltstype,i),l->label[ofs+i]);
            n=strlen(name)+c.len*2+7;
            if (b-p < n) { 
                u = p-s;
                s = RTrealloc(s, u+n); /* TODO: check s!=0 */
                p = s+u;
                b = s + u+n;
            }
            sprintf(p, "%s=",name);
            p+=strlen(p);
            chunk2string(c,b-p,p);
            p+=strlen(p);
            sprintf(p, "|");
            p +=strlen(p);
       }
    }
    return s;
}

CAESAR_TYPE_STRING CAESAR_INFORMATION_LABEL(CAESAR_TYPE_LABEL l) {
	static char *s = NULL;  /* start of string */
	static char *b = NULL; /*beyond*/
	char *p = NULL;  /* current insertion point */
	int u, n; /* used, needed */
	int i;
	
	if (label_format!=2)
		return "";
		
	p = s;
	n = 5;
	if (b-p < n) {
		u = p-s;
		s = RTrealloc(s, u+n);/* TODO: check s!=0 */
		p = s+u;
		b = s + u+n;
	}
	if (edge_labels > 1) {
		sprintf(p, "<");
		p += strlen(p);
	}
	for(i=0; i < edge_labels; i++) {
		n = 15; /* size to print integer + ';',  '>', '\0' */
		if (b-p < n) { 
			u = p-s;
			s = RTrealloc(s, u+n); /* TODO: check s!=0 */
			p = s+u;
			b = s + u+n;
		}
		if (i>0) {
			sprintf(p, ";");
			p += strlen(p);
		}
		sprintf(p, "%d", l->label[i]);
		p += strlen(p);
	}
	if (edge_labels > 1) {
		sprintf(p, ">");
		p += strlen(p);
	}
	return s;
}


void CAESAR_PRINT_STATE_HEADER(CAESAR_TYPE_FILE fp) {
	(void) fp; /* TODO */
}

void CAESAR_DELTA_STATE(CAESAR_TYPE_FILE f,CAESAR_TYPE_STATE s1,CAESAR_TYPE_STATE s2) {
	int i;
	chunk c;
	int labels1[state_labels];
	int labels2[state_labels];
	
	if (state_format==0) {
		fprintf(f, "state [");
		for(i=0; i < N; i++) {
			if (i>0)
				fprintf(f, ",");
			if (s1->state[i] == s2->state[i])
				fprintf(f, "-");
			else
				fprintf(f, "%d:=%d", s1->state[i], s2->state[i]);
		}
		fprintf(f, "]\n");
	} else if (state_format==1) {
		GBgetStateLabelsAll(model,s1->state,labels1);
		GBgetStateLabelsAll(model,s2->state,labels2);
		fprintf(f, "state <");
		for(i=0; i < state_labels; i++) {
			if (i>0)
				fprintf(f, ";");
			if (s1->state[i] == s2->state[i])
				fprintf(f, "-");
			else {
				c=GBchunkGet(model,lts_type_get_state_label_typeno(ltstype,i),labels1[i]);
				{
					char str[c.len*2+6];
					chunk2string(c,sizeof str,str);
					fprintf(f, "%s", str);
				}
				fprintf(f, ":=");
				c=GBchunkGet(model,lts_type_get_state_label_typeno(ltstype,i),labels2[i]);
				{
					char str[c.len*2+6];
					chunk2string(c,sizeof str,str);
					fprintf(f, "%s", str);
				}
			}
		}
		fprintf(f, ">");
	}
}



/* ON THE FLY EXPLORATION */

/* global variables to store arguments of CAESAR_ITERATE_STATE,
   i.e. the current call-back function, the source, label, destination
*/


typedef struct callback_context_t {
	CAESAR_TYPE_STATE src;
	CAESAR_TYPE_LABEL lbl;
	CAESAR_TYPE_STATE dst;
        int *labels;
	void (*callback)(CAESAR_TYPE_STATE, CAESAR_TYPE_LABEL, CAESAR_TYPE_STATE);
} callback_struct_t;

static void iterate_transition(void*arg,transition_info_t*ti,int*dst){
	int i;
	callback_struct_t *context=(callback_struct_t*)arg;
	
	for(i=0; i<N; i++)
		context->dst->state[i] = dst[i];
	for(i=0; i<edge_labels; i++)
		context->lbl->label[i] = ti->labels[i];
        if (edge_encode){
            int ofs=edge_labels;
            for(i=0;i<N;i++) context->lbl->label[ofs+i] = context->src->state[i];
            ofs+=N;
            for(i=0;i<state_labels;i++) context->lbl->label[ofs+i] = context->labels[i];
        }
	context->callback(context->src, context->lbl, context->dst);
}


void CAESAR_ITERATE_STATE(CAESAR_TYPE_STATE s1, 
			CAESAR_TYPE_LABEL l, 
			CAESAR_TYPE_STATE s2, 
			void (*callback) 
				(CAESAR_TYPE_STATE, CAESAR_TYPE_LABEL, CAESAR_TYPE_STATE)) {
    int i,c;
    if (edge_encode && s1->state[0]<0) return;
    if (edge_encode){
        int labels[state_labels];
        GBgetStateLabelsAll(model,s1->state,labels);
        {
            callback_struct_t context = { s1, l, s2, labels, callback };
            c=GBgetTransitionsAll(model,s1->state,iterate_transition,&context);
            if (c==0){
                int ofs=edge_labels;
                for(i=0;i<edge_labels;i++) l->label[i]=-1;
                for(i=0;i<N;i++) {
                    l->label[ofs+i] = s1->state[i];
                    /* for dummy deadlock
                    s2->state[i] = - 1;
                    */
                    /* for selfloop deadlock */
                    s2->state[i]=s1->state[i];
                }
                ofs+=N;
                for(i=0;i<state_labels;i++) l->label[ofs+i] = labels[i];
                callback(s1,l,s2);
            }
        }
    } else {
        callback_struct_t context = { s1, l, s2, NULL, callback };
	c=GBgetTransitionsAll(model,s1->state,iterate_transition,&context);
    }
}

void CAESAR_START_STATE(CAESAR_TYPE_STATE s) {
	GBgetInitialState(model,s->state);
}

/* SOME OTHER REQUIRED FUNCTIONS */

CAESAR_TYPE_BOOLEAN CAESAR_VISIBLE_LABEL(CAESAR_TYPE_LABEL x) {
	CAESAR_TYPE_BOOLEAN vis = CAESAR_TRUE;
	if (edge_labels==1) {
		chunk c=GBchunkGet(model,lts_type_get_edge_label_typeno(ltstype,0),x->label[0]);
		if (c.len==3 && strncmp(c.data, "tau", c.len)==0)
			vis =CAESAR_FALSE;
	}
	return vis;
}

CAESAR_TYPE_NATURAL CAESAR_HASH_STATE(CAESAR_TYPE_STATE s,CAESAR_TYPE_NATURAL m) {
	return SuperFastHash((char*)s->state,N*sizeof(int),0) % m;
}

CAESAR_TYPE_NATURAL CAESAR_HASH_LABEL(CAESAR_TYPE_LABEL l,CAESAR_TYPE_NATURAL m) {
	return SuperFastHash((char*)l->label,edge_size*sizeof(int),0) % m;
}


CAESAR_TYPE_STRING CAESAR_GATE_LABEL(CAESAR_TYPE_LABEL l) {
	static char *s = NULL;  /* start of string */
	static char *b = NULL; /*beyond*/
	char *p = NULL;  /* current insertion point */
	int u, n; /* used, needed */
	unsigned int i; /*unsigned to avoid warning in comparsison to c.len*/
	chunk c;
	
	if (edge_labels==1) {
		c=GBchunkGet(model,lts_type_get_edge_label_typeno(ltstype,0),l->label[0]);
		i = 0;
		while(i < c.len && strchr(" \t(),.:!?;[]{}",c.data[i])==0)
			i++;
		p = s;
		n = i;
		if (b-p < n) { 
			u = p-s;
			s = RTrealloc(s, u+n);
			p = s+u;
			b = s + u+n;
		}
		strncpy(s, c.data, i);
		if (i > 0) s[i-1] = '\0'; else s[0] = '\0';
		return s;
	} else
		return "gate";
}

CAESAR_TYPE_NATURAL CAESAR_CARDINAL_LABEL(CAESAR_TYPE_LABEL l) {
	(void)l;
	return 1; /* TODO: obtain first word of LOTOS or mucrl or promela style label */
}

/* INITIALIZATION */


static void *new_string_index(void* context){
	(void)context;
	return SIcreate();
}


void CAESAR_INIT_GRAPH(void) {
	char *opencaesar_args, *opencaesar_prog,*ltsmin_options;
	 int argc;
	 char **argv;

	opencaesar_prog = getenv ("OPEN_CAESAR_PROG");
	if (opencaesar_prog == NULL)
		CAESAR_ERROR ("undefined environment variable $OPEN_CAESAR_PROG");
	opencaesar_args = getenv ("OPEN_CAESAR_FILE");
	if (opencaesar_args == NULL)
		CAESAR_ERROR ("undefined environment variable $OPEN_CAESAR_FILE");
	ltsmin_options = getenv ("LTSMIN_OPTIONS");
	if (ltsmin_options == NULL)
		CAESAR_ERROR ("undefined environment variable $LTSMIN_OPTIONS");
	
	
	int len=strlen(opencaesar_prog)+strlen(ltsmin_options)+strlen(opencaesar_args);
	char cmdline[len+6];
	sprintf(cmdline,"%s %s %s",opencaesar_prog,ltsmin_options,opencaesar_args);

	int res=poptParseArgvString(cmdline,&argc,(void*)(&argv));
	if (res){
		Abort("could not parse %s: %s",opencaesar_args,poptStrerror(res));
	}

 	char *files[2];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Options");
    HREinitStart(&argc,&argv,1,1,(char**)files,"<model>");

	Warning(info,"loading model from %s",files[0]);
	model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);

	GBloadFile(model,files[0],&model);

	ltstype=GBgetLTStype(model);
	N = lts_type_get_state_length(ltstype);
	K = dm_nrows(GBgetDMInfo(model));
	Warning(info,"length is %d, there are %d groups",N,K);
	state_labels=lts_type_get_state_label_count(ltstype);
	edge_labels=lts_type_get_edge_label_count(ltstype);
	Warning(info,"There are %d state labels and %d edge labels",state_labels,edge_labels);
        if (edge_encode){
            edge_size=edge_labels+N+state_labels;
            Warning(info,"encoding state information on edges");
        } else {
            edge_size=edge_labels;
            Warning(info,"state information is hidden");
        } 
	CAESAR_HINT_SIZE_STATE = N*sizeof(int);
	CAESAR_HINT_HASH_SIZE_STATE = CAESAR_HINT_SIZE_STATE;
	CAESAR_HINT_SIZE_LABEL = edge_size*sizeof(int);
	CAESAR_HINT_HASH_SIZE_LABEL = CAESAR_HINT_SIZE_LABEL;
	Warning(info,"CAESAR_HINT_SIZE_STATE=%d CAESAR_HINT_SIZE_LABEL=%d",
                CAESAR_HINT_SIZE_STATE,CAESAR_HINT_SIZE_LABEL);
}
