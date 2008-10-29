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
#include "unix.h"

static int blocksize=65536;
static int plain=0;
static int segments=0;
static int bcg_ready=0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: ltsmin-convert options input output",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,"-q",
		"do not print info messages",NULL,NULL,NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",NULL,NULL,NULL},
	{"-segments",OPT_REQ_ARG,parse_int,&segments,NULL,
		"set the number of segments for the output",NULL,NULL,NULL},
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


struct bcg_write_s {
	char* name;
	int status;
} bcg_out;

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
static int tau_index=-1;

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

void bcg_cb(
	void*context,
	uint32_t src_seg,
	uint32_t src_ofs,
	uint32_t label,
	uint32_t dst_seg,
	uint32_t dst_ofs
){
#define bcg_p ((struct bcg_write_s*)context)
	printf("%d.%d --%d/%s-> %d.%d\n",src_seg,src_ofs,label,SIget(label_index,label),dst_seg,dst_ofs);
	if (src_seg) Fatal(1,error,"bad source segment");
	if (dst_seg) Fatal(1,error,"bad destination segment");
	if (bcg_p->status==0) {
		BCG_IO_WRITE_BCG_BEGIN (bcg_p->name,lts_get_root(src_lts),1,lts_get_comment(src_lts),0);
		bcg_p->status=1;
	}
	BCG_IO_WRITE_BCG_EDGE (src_ofs,(label==tau_index)?"i":SIget(label_index,label),dst_ofs);
#undef bcg_p
}

void bcg_end(struct bcg_write_s* bcg_p){
	(void)bcg_p;
	BCG_IO_WRITE_BCG_END ();
}


void enumerate_archive(lts_format_t fmt,char*name,lts_t* lts,string_index_t* si,edge_cb_t cb,void* context){
	*si=SIcreate();
	archive_t arch;
	switch (fmt){
	case LTS_FMT:
		arch=arch_fmt(name,file_input,file_output,prop_get_U32("bs",blocksize));;
		break;
	case LTS_GCF:
		arch=arch_gcf_read(raf_unistd(name));
		break;
	default:
		Fatal(1,error,"illegal format");
		return;
	}
	int decode;
	stream_t ds;
	ds=arch_read(arch,"info",NULL);
	*lts=lts_read(ds,&decode);
	DSclose(&ds);
	Warning(info,"got the headers");
	int N=lts_get_labels(*lts);
	ds=arch_read(arch,"TermDB",decode?"auto":NULL);
	for(int i=0;i<N;i++){
		char *l=DSreadLN(ds);
		SIputAt(*si,l,i);
	}
	DSclose(&ds);
	Warning(info,"got %d labels, enumerating transitions",N);
	if(lts_has_tau(*lts)){
		tau_index=lts_get_tau(*lts);
	}
	N=lts_get_segments(*lts);
	for(int i=0;i<N;i++){
		for(int j=0;j<N;j++){
			char name[1024];
			sprintf(name,"src-%d-%d",i,j);
			stream_t src_ds=arch_read(arch,name,decode?"auto":NULL);
			sprintf(name,"label-%d-%d",i,j);
			stream_t lbl_ds=arch_read(arch,name,decode?"auto":NULL);
			sprintf(name,"dest-%d-%d",i,j);
			stream_t dst_ds=arch_read(arch,name,decode?"auto":NULL);
			uint32_t src_ar[1024];			
			uint32_t lbl_ar[1024];
			uint32_t dst_ar[1024];
			for(;;){
				int len=stream_read_max(src_ds,src_ar,4096);
				if (len) {
					stream_read(lbl_ds,lbl_ar,len);
					stream_read(dst_ds,dst_ar,len);
					for(int k=0;k<(len/4);k++){
						cb(context,i,bswap_32(src_ar[k]),bswap_32(lbl_ar[k]),
								j,bswap_32(dst_ar[k]));
					}
				}
				if(len<4096) break;
			}
		}
	}
	arch_close(&arch);
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
			tau_index=i;
			SIputAt(*si,"tau",i);// This will fail if there are 2 or more invisible transitions.
		}
	}
	Warning(info,"enumerating transitions");
	BCG_OT_ITERATE_PLN (bcg_graph, bcg_s1, bcg_label_number, bcg_s2) {
		cb(context,0,bcg_s1,bcg_label_number,0,bcg_s2);
	} BCG_OT_END_ITERATE;
	Warning(info,"finished enumerating transitions");
	BCG_OT_READ_BCG_END (&bcg_graph);
}

int main(int argc, char *argv[]){
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	take_options(options,&argc,argv);
	blocksize=prop_get_U32("bs",blocksize);
	char *appl=basename(argv[0]);
	if(argc!=3){
		Fatal(1,error,"usage %s <input> <output>",appl);
	}
	lts_format_t fmt_in=format_by_name(argv[1]);
	lts_format_t fmt_out=format_by_name(argv[2]);
	edge_cb_t out_cb;
	void* out_ctx;
	switch(fmt_out){
	case LTS_DIR:
		Warning(error,"This tool does not have support for creating directories.");
		Warning(error,"You can make the directory %s yourself and then use",argv[2]);
		Fatal(1,error,"%s/%%s as the output argument",argv[2]);
		break;	
	case LTS_FMT:
	case LTS_GCF:
		Fatal(1,error,"cannot write DIR yet.");
		break;	
	case LTS_BCG:
#ifdef HAVE_BCG
		Warning(info,"preparing for BCG output");
		if (!bcg_ready) {
			BCG_INIT();
			bcg_ready=1;
		}
		bcg_out.name=argv[2];
		bcg_out.status=0;
		out_cb=bcg_cb;
		out_ctx=&bcg_out;
#else
		Fatal(1,error,"BCG support has not been built into this binary");
#endif
		break;
	}
	switch(fmt_in){
	case LTS_DIR:
		Fatal(1,error,"Did you mean %s/%%s as the input?",argv[1]);
		break;	
	case LTS_FMT:
	case LTS_GCF:
		enumerate_archive(fmt_in,argv[1],&src_lts,&label_index,out_cb,out_ctx);
		break;
	case LTS_BCG:
#ifdef HAVE_BCG
		if (!bcg_ready) {
			BCG_INIT();
			bcg_ready=1;
		}
		Warning(info,"enumerating BCG file %s",argv[1]);
		enumerate_bcg(argv[1],&src_lts,&label_index,out_cb,out_ctx);
#else
		Fatal(1,error,"BCG support has not been built into this binary");
#endif
		break;
	}
	switch(fmt_out){
	case LTS_DIR:
	case LTS_FMT:
	case LTS_GCF:
		Fatal(1,error,"cannot write DIR yet.");
		break;	
	case LTS_BCG:
#ifdef HAVE_BCG
		if (bcg_out.status==0) {
			Fatal(1,error,"empty BCG file not supported.");
		}
		BCG_IO_WRITE_BCG_END ();
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


