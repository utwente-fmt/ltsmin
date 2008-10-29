#include "archive.h"
#include <stdio.h>
#include "runtime.h"
#include <libgen.h>
#include "ltsmeta.h"
#include "stringindex.h"

#include "amconfig.h"
#ifdef HAVE_BCG_USER_H
#define HAVE_BCG
#include "bcg_user.h"
#endif

static int blocksize=65536;
static int plain=0;
static int decode=0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: ltsmin-convert options input output",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,"-q",
		"do not print info messages",NULL,NULL,NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static void stream_copy(archive_t src,archive_t dst,char*name,char*decode,char*encode){
	stream_t is=arch_read(src,name,decode);
	stream_t os=arch_write(dst,name,encode);
	char buf[blocksize];
	for(;;){
		int len=stream_read_max(is,buf,blocksize);
		if (len) stream_write(os,buf,len);
		if(len<blocksize) break;
	}
	stream_close(&is);
	stream_close(&os);	
}

typedef enum {LTS_GCF,LTS_DIR,LTS_FMT,LTS_BCG} lts_format_t;

static lts_format_t format_by_name(char*name){
	int len=strlen(name);
	if (strstr(name,"%s")) return LTS_FMT;
	if (len>=4 && !strcmp(name+(len-4),".dir")) return LTS_DIR;
	if (len>=4 && !strcmp(name+(len-4),".bcg")) return LTS_BCG;
	if (len>=4 && !strcmp(name+(len-4),".gcf")) return LTS_GCF;
	Warning(info,"format of %s not detectable by name, using default.",name);
	return LTS_GCF;
}

typedef void(*edge_cb_t)(
	void*context,
	uint32_t src_seg,
	uint32_t src_ofs,
	uint32_t label,
	uint32_t dst_seg,
	uint32_t dst_ofs
);

static string_index_t label_index;
static lts_t src_lts;

void print_edge(
	void*context,
	uint32_t src_seg,
	uint32_t src_ofs,
	uint32_t label,
	uint32_t dst_seg,
	uint32_t dst_ofs
){
	printf("%d.%d --%s-> %d.%d\n",src_seg,src_ofs,SIget(label_index,label),dst_seg,dst_ofs);
}

void enumerate_archive(lts_format_t fmt,char*name,edge_cb_t cb,void* context){
//	string_index_t si=SIcreate();
}

void enumerate_bcg(char*name,lts_t* lts,string_index_t* si,edge_cb_t cb,void* context){
	*si=SIcreate();
	*lts=lts_create();
	lts_set_segments(*lts,1);
	BCG_TYPE_OBJECT_TRANSITION bcg_graph;
	BCG_TYPE_C_STRING bcg_comment;
	bcg_type_state_number bcg_s1, bcg_s2;
	BCG_TYPE_LABEL_NUMBER bcg_label_number;
	BCG_OT_READ_BCG_BEGIN (name, &bcg_graph, 0);
	BCG_READ_COMMENT (BCG_OT_GET_FILE (bcg_graph), &bcg_comment);
	lts_set_comment(*lts,bcg_comment);
	lts_set_states(*lts,BCG_OT_NB_STATES (bcg_graph));
	lts_set_trans(*lts,BCG_OT_NB_EDGES (bcg_graph));
	lts_set_root(*lts,BCG_OT_INITIAL_STATE (bcg_graph));
	int N=BCG_OT_NB_LABELS (bcg_graph);
	lts_set_labels(*lts,N);
	for(int i=0;i<N;i++){
		if (BCG_OT_LABEL_VISIBLE (bcg_graph,i)){
			SIputAt(*si,BCG_OT_LABEL_STRING (bcg_graph,i),i);
		} else {
			SIputAt(*si,"tau",i);// This will fail if there are 2 or more invisible transitions.
		}
	}
	BCG_OT_ITERATE_PLN (bcg_graph, bcg_s1, bcg_label_number, bcg_s2) {
		cb(context,0,bcg_s1,bcg_label_number,0,bcg_s2);
	} BCG_OT_END_ITERATE;
	BCG_OT_READ_BCG_END (&bcg_graph);
}

int main(int argc, char *argv[]){
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	take_options(options,&argc,argv);
	blocksize=prop_get_U32("bs",blocksize);
	char *appl=basename(argv[0]);
	archive_t ar_in,ar_out;
	//if(argc!=3){
	//	Fatal(1,error,"usage %s <input> <output>",appl);
	//}
	lts_format_t fmt_in=format_by_name(argv[1]);
	switch(fmt_in){
	case LTS_DIR:
	case LTS_FMT:
	case LTS_GCF:
		enumerate_archive(fmt_in,argv[1],print_edge,NULL);
		break;
	case LTS_BCG:
#ifdef HAVE_BCG
		BCG_INIT();
		Warning(info,"enumerating BCG file %s",argv[1]);
		enumerate_bcg(argv[1],&src_lts,&label_index,print_edge,NULL);
#else
		Fatal(1,error,"BCG support has not been built into this binary");
#endif
		break;
	}
/*
	if (strstr(argv[1],"%s")){
		ar_in=arch_fmt(argv[1],file_input,file_output,prop_get_U32("bs",blocksize));
	} else {
		ar_in=arch_gcf_read(raf_unistd(argv[1]));
	}
	if (strstr(argv[2],"%s")){
		ar_out=arch_fmt(argv[2],file_input,file_output,prop_get_U32("bs",blocksize));
	} else {
		uint32_t bc=prop_get_U32("bc",128);
		ar_out=arch_gcf_create(raf_unistd(argv[2]),blocksize,blocksize*bc,0,1);
	}
	Warning(info,"copying %s to %s",argv[1],argv[2]);
	stream_t ds;
	ds=arch_read(ar_in,"info",NULL);
	lts_t lts=lts_read(ds,&decode);
	DSclose(&ds);
	Log(info,"output compression is %s",plain?"disabled":"enabled");
	int N=lts_get_segments(lts);
	ds=arch_write(ar_out,"info",plain?NULL:"");
	lts_write_info(lts,ds,DIR_INFO);
	DSclose(&ds);
	stream_copy(ar_in,ar_out,"TermDB",decode?"auto":NULL,plain?NULL:"gzip");
	for(int i=0;i<N;i++){
		for(int j=0;j<N;j++){
			char name[1024];
			sprintf(name,"src-%d-%d",i,j);
			stream_copy(ar_in,ar_out,name,decode?"auto":NULL,plain?NULL:"diff32|gzip");
			sprintf(name,"label-%d-%d",i,j);
			stream_copy(ar_in,ar_out,name,decode?"auto":NULL,plain?NULL:"gzip");
			sprintf(name,"dest-%d-%d",i,j);
			stream_copy(ar_in,ar_out,name,decode?"auto":NULL,plain?NULL:"diff32|gzip");
		}
	}
	arch_close(&ar_in);
	arch_close(&ar_out);
*/
	return 0;
}


