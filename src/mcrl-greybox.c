#include <stdio.h>

#include "mcrl-greybox.h"
#include "chunk-table.h"
#include "runtime.h"
#include "rw.h"
#include "mcrl.h"
#include "step.h"

static ATerm label=NULL;
static ATerm *dst;

static int nPars;
static int nSmds;

typedef struct {
	int count;
	int *proj;
} smdinfo_t ;

static smdinfo_t *summand;

static chunk_table_t termdb_table;
static ATermIndexedSet termdb_set;
static chunk_table_t actiondb_table;
static ATermIndexedSet actiondb_set;

static void new_term(void*context,size_t len,void* chunk){
	((char*)chunk)[len]=0;
	ATerm t=ATreadFromString((char*)chunk);
	ATindexedSetPut((ATermIndexedSet)context,t,NULL);
}

static long find_index(chunk_table_t chunks,ATermIndexedSet terms,ATerm t){
	long idx=ATindexedSetGetIndex(terms,t);
	if (idx>=0) return idx;
	char *tmp=ATwriteToString(t);
	int len=strlen(tmp);
	char chunk[len+1];
	for(int i=0;i<len;i++) chunk[i]=tmp[i];
	chunk[len]=0;
	CTsubmitChunk(chunks,len,chunk,new_term,terms);
	return ATindexedSetGetIndex(terms,t);
}

static ATerm find_term(chunk_table_t chunks,ATermIndexedSet terms,long idx){
	ATerm t=ATindexedSetGetElem(terms,idx);
	if(t) return t;
	CTupdateTable(chunks,idx,new_term,terms);
	return ATindexedSetGetElem(terms,idx);
}

static int expand;
static int smd;
static TransitionCB user_cb;
static void* user_context;

static void callback(void){
	int lbl=find_index(actiondb_table,actiondb_set,label);
	if (expand){
		int dst_p[summand[smd].count];
		for(int i=0;i<summand[smd].count;i++){
			dst_p[i]=find_index(termdb_table,termdb_set,dst[summand[smd].proj[i]]);
		}
		user_cb(user_context,&lbl,dst_p);
	} else {
		int dst_p[nPars];
		for(int i=0;i<nPars;i++){
			dst_p[i]=find_index(termdb_table,termdb_set,dst[i]);
		}
		user_cb(user_context,&lbl,dst_p);
	}
}

static void WarningHandler(const char *format, va_list args) {
     fprintf(stderr,"MCRL grey box: ");
     ATvfprintf(stderr, format, args);
     fprintf(stderr,"\n");
     }
     
static void ErrorHandler(const char *format, va_list args) {
     fprintf(stderr,"MCRL grey box: ");
     ATvfprintf(stdout, format, args);
     fprintf(stdout,"\n");
     Fatal(1,error,"ATerror");
     exit(1);
     }


void MCRLinitGreybox(int argc,char *argv[],void* stack_bottom){
	ATinit(argc, argv, stack_bottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
	int i;
	int c=argc+4;
	char* cpargv[c];
	char** xargv=cpargv;
	xargv[0]=argv[0];
	xargv[1]="-alt";
	xargv[2]="rw";
	xargv[3]="-no-hash";
	xargv[4]="-conditional";
	for(i=1;i<argc;i++) xargv[i+4]=argv[i];
	MCRLsetArguments(&c, &xargv);
	RWsetArguments(&c, &xargv);
	STsetArguments(&c, &xargv);
}


model_t GBcreateModel(char*model){
	int i,j;
	if(!MCRLinitNamedFile(model)) {
		Fatal(1,error,"failed to open %s",model);
		return NULL;
	}
	if (!RWinitialize(MCRLgetAdt())) {
		ATerror("Initialize rewriter");
	}
	nPars=MCRLgetNumberOfPars();
	dst=(ATerm*)malloc(nPars*sizeof(ATerm));
	for(i=0;i<nPars;i++) {
		dst[i]=NULL;
	}
 	ATprotect(&label);
	ATprotectArray(dst,nPars);
	STinitialize(noOrdering,&label,dst,callback);
	STsetInitialState(); // Fills dst with a legal vector for later calls.
	nSmds=STgetSummandCount();
	summand=malloc(nSmds*sizeof(smdinfo_t));
	for(i=0;i<nSmds;i++){
		int temp[nPars];
		summand[i].count=STgetProjection(temp,i);
		summand[i].proj=malloc(summand[i].count*sizeof(int));
		for(j=0;j<summand[i].count;j++) summand[i].proj[j]=temp[j];
	}
	termdb_table=CTcreate("leaf");
	termdb_set=ATindexedSetCreate(1024,75);
	actiondb_table=CTcreate("action");
	actiondb_set=ATindexedSetCreate(1024,75);
	return NULL;
}

int GBgetStateLength(model_t model){
	(void)model;
	return nPars;
}

int GBgetLabelCount(model_t model){
	(void)model;
	return 1;
}

char* GBgetLabelDescription(model_t model,int label){
	(void)model;(void)label;
	return "action";
}

int GBgetGroupCount(model_t model){
	(void)model;
	return nSmds;
}

void GBgetGroupInfo(model_t model,int group,int*length,int**indices){
	(void)model;
	if(length) *length=summand[group].count;
	if(indices) *indices=summand[group].proj;
}

void GBgetInitialState(model_t model,int *state){
	(void)model;
	int i;
	STsetInitialState();
	for(i=0;i<nPars;i++) state[i]=find_index(termdb_table,termdb_set,dst[i]);
}

int GBgetTransitionsShort(model_t model,int group,int*src,TransitionCB cb,void*context){
	(void)model;
	ATerm at_src[nPars];
	expand=1;
	smd=group;
	user_cb=cb;
	user_context=context;
	for(int i=0;i<nPars;i++) {
		at_src[i]=dst[i];
	}
	for(int i=0;i<summand[group].count;i++){
		at_src[summand[group].proj[i]]=find_term(termdb_table,termdb_set,src[i]);
	}
	return STstepSmd(at_src,&smd,1);
}

int GBgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
	(void)model;
	ATerm at_src[nPars];
	expand=0;
	smd=group;
	user_cb=cb;
	user_context=context;
	for(int i=0;i<nPars;i++) {
		at_src[i]=find_term(termdb_table,termdb_set,src[i]);
	}
	return STstepSmd(at_src,&smd,1);
}


