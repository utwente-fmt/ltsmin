#include "archive.h"
#include <stdio.h>
#include "runtime.h"
#include <libgen.h>
#include "ltsman.h"
#include "bcg_user.h"

static int blocksize=32768;
static int decode=0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: gcf2bcg <gcf input> <bcg oputput>",
		NULL,NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,"-q",
		"do not print info messages",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{"-bs",OPT_REQ_ARG,parse_int,&blocksize,"-bs <block size>",
		"Set the block size to be used for copying streams.",
		NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};


int main(int argc, char **argv){
	archive_t gcf;
	stream_t ds;

	runtime_init_args(&argc,&argv);
	take_options(options,&argc,argv);
	if (argc!=3) {
		printoptions(options);
		exit(1);
	}
	gcf=arch_gcf_read(raf_unistd(argv[1]));
	BCG_INIT();
	ds=arch_read(gcf,"info",NULL);
	lts_t lts=lts_read_info(ds,&decode);
	DSclose(&ds);
	ds=arch_read(gcf,"TermDB",decode?"auto":NULL);
	string_index_t si=lts_get_string_index(lts);
	uint32_t tau=(uint32_t)-1;
	for(int i=0;;i++){
		char*lbl=DSreadLN(ds);
		if (strlen(lbl)==0) {
			Warning(info,"read %d labels",i);
			break;
		}
		if (strcmp(lbl,"tau")==0) tau=i;
		if (strcmp(lbl,"\"tau\"")==0) {
			Warning(info,"the invisible step should be tau rather than \"tau\".");
			tau=i;
		}
		//Warning(info,"label %d is %s",i,lbl);
		SIputAt(si,lbl,i);
		free(lbl);
	}
	DSclose(&ds);
	int N=lts_get_segments(lts);
	Warning(info,"copying %d segments",N);
	int offset[N];
	offset[0]=0;
	for(int i=1;i<N;i++) {
		offset[i]=offset[i-1]+lts_get_states(lts,i-1);
	}
	BCG_IO_WRITE_BCG_BEGIN (argv[2],lts_get_root_ofs(lts)+offset[lts_get_root_seg(lts)],1,"gcf2bcg",0);
	for(int i=0;i<N;i++){
		for(int j=0;j<N;j++){
			char name[1024];
			sprintf(name,"src-%d-%d",i,j);
			stream_t src_in=arch_read(gcf,name,decode?"auto":NULL);
			sprintf(name,"label-%d-%d",i,j);
			stream_t lbl_in=arch_read(gcf,name,decode?"auto":NULL);
			sprintf(name,"dest-%d-%d",i,j);
			stream_t dst_in=arch_read(gcf,name,decode?"auto":NULL);
			for(;;){
				if (DSempty(src_in)) break;
				uint32_t s=DSreadU32(src_in)+offset[i];
				uint32_t l=DSreadU32(lbl_in);
				uint32_t d=DSreadU32(dst_in)+offset[j];
				BCG_IO_WRITE_BCG_EDGE (s,(l==tau)?"i":(SIget(si,l)),d);
			}
		}
	}
	BCG_IO_WRITE_BCG_END ();
	arch_close(&gcf);
	return 0;
}



