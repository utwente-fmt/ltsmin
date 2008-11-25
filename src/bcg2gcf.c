#include "config.h"
#include <stdlib.h>

#include "archive.h"
#include "stream.h"
#include "bcg_user.h"
#include "runtime.h"
#include "ltsman.h"

static int plain=0;
static int blocksize=32768;
static int blockcount=32;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: bcg2gcf [options] <bcg input> <gcf output>",
		NULL,NULL,NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,"-q",
		"do not print info messages",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{"-bs",OPT_REQ_ARG,parse_int,&blocksize,"-bs <block size>",
		"Set the block size to be used for copying streams.",
		NULL,NULL,NULL},
	{"-bc",OPT_REQ_ARG,parse_int,&blockcount,"-bc <block count>",
		"Set the number of block in one GCF cluster.",
		NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

int main(int argc,char**argv){
	BCG_TYPE_OBJECT_TRANSITION bcg_graph;
	bcg_type_state_number bcg_s1;
	BCG_TYPE_C_STRING bcg_comment;
	BCG_TYPE_LABEL_NUMBER bcg_label_number;
	bcg_type_state_number bcg_s2;
	int i,N,invis;
	archive_t gcf;
	stream_t ds;

	runtime_init_args(&argc,&argv);
	take_options(options,&argc,argv);
	if (argc!=3) {
		printoptions(options);
		exit(1);
	}
	char*bcg=argv[1];
	BCG_INIT();
	if (sizeof(void*)==8){
		setenv("CADP_BITS","64",1);
	} else {
		setenv("CADP_BITS","32",1);
	}
	BCG_OT_READ_BCG_BEGIN (bcg, &bcg_graph, 0);
	gcf=arch_gcf_create(raf_unistd(argv[2]),blocksize,blocksize*blockcount,0,1);
	lts_t lts=lts_new();
	lts_set_segments(lts,1);
	lts_set_states(lts,0,BCG_OT_NB_STATES (bcg_graph));
	lts_set_trans(lts,0,0,BCG_OT_NB_EDGES (bcg_graph));
	lts_set_root(lts,0,BCG_OT_INITIAL_STATE (bcg_graph));
	BCG_READ_COMMENT (BCG_OT_GET_FILE (bcg_graph), &bcg_comment);
	N=BCG_OT_NB_LABELS (bcg_graph);
	ds=arch_write(gcf,"TermDB",plain?"":"gzip",1);
	invis=0;
	string_index_t si=lts_get_string_index(lts);
	for(i=0;i<N;i++){
		if (BCG_OT_LABEL_VISIBLE (bcg_graph,i)){
			char*str=BCG_OT_LABEL_STRING (bcg_graph,i);
			SIputAt(si,str,i);
			DSwrite(ds,str,strlen(str));
			DSwrite(ds,"\n",1);
		} else {
			if (invis) Fatal(1,error,"more than one invisible label");
			SIputAt(si,"tau",i);
			DSwrite(ds,"tau\n",4);
			invis++;
		}
	}
	DSclose(&ds);

	ds=arch_write(gcf,"info","",1);
	lts_write_info(lts,ds,LTS_INFO_DIR);
	DSclose(&ds);

	stream_t src=arch_write(gcf,"src-0-0",plain?"":"diff32|gzip",1);
	stream_t lbl=arch_write(gcf,"label-0-0",plain?"":"gzip",1);
	stream_t dst=arch_write(gcf,"dest-0-0",plain?"":"diff32|gzip",1);
	BCG_OT_ITERATE_PLN (bcg_graph,bcg_s1,bcg_label_number,bcg_s2){
		DSwriteU32(src,bcg_s1);
		DSwriteU32(lbl,bcg_label_number);
		DSwriteU32(dst,bcg_s2);
	} BCG_OT_END_ITERATE;
	BCG_OT_READ_BCG_END (&bcg_graph);
	DSclose(&src);
	DSclose(&lbl);
	DSclose(&dst);
	arch_close(&gcf);
	return 0;
}

