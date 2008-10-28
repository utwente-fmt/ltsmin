#include "runtime.h"
#include "mcrl-greybox.h"
#include "chunk-table.h"
#include "treedbs.h"


chunk_table_t CTcreate(char *name){
	(void)name;
	return NULL;
}

void CTsubmitChunk(chunk_table_t table,size_t len,void* chunk,chunk_add_t cb,void* context){
	(void) table;
	cb(context,len,chunk);
}

void CTupdateTable(chunk_table_t table,uint32_t wanted,chunk_add_t cb,void* context){
	(void)table;(void)wanted;(void)cb;(void)context;
}

static int N;
static int K;
static int visited=1;
static int explored=0;
static int trans=0;

void print_next(void*arg,int*lbl,int*dst){
	int tmp[N+N];
	printf("%d --%d->",*((int*)arg),lbl[0]);
	for(int i=0;i<N;i++) {
		printf(" %d",dst[i]);
		tmp[N+i]=dst[i];
	}
	Fold(tmp);
	trans++;
	if (tmp[1]>=visited) visited=tmp[1]+1;
	printf(" == %d\n",tmp[1]);
}

int main(int argc, char *argv[]){
	void* stackbottom=&argv;
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	MCRLinitGreybox(argc,argv,stackbottom);
	GBcreateModel(argv[argc-1]);
	N=GBgetStateLength(NULL);
	K=GBgetGroupCount(NULL);
	TreeDBSinit(N,1);
	Warning(info,"length is %d",N);
	int src[N+N];
	GBgetInitialState(NULL,src+N);
	printf("s0:");
	for(int i=0;i<N;i++) printf(" %d",src[N+i]);
	Fold(src);
	printf(" == %d\n",src[1]);
	int count=0;
	while(explored<visited){
		src[1]=explored;
		Unfold(src);
		for(int i=0;i<K;i++){
			int c=GBgetTransitionsLong(NULL,i,src+N,print_next,&(src[1]));
			//printf("group %d had %d transitions\n",i,c);
			count+=c;
		}
		explored++;
	}
	printf("state space has %d states %d ?= %d transitions\n",visited,trans,count);		
	return 0;
}

