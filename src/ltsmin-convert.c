#include "archive.h"
#include <stdio.h>
#include "runtime.h"
#include <libgen.h>
#include "ltsman.h"
#include "dirops.h"
#include "amconfig.h"
#ifdef HAVE_BCG_USER_H
#include "bcg_user.h"
#endif

#include <inttypes.h>


static int plain=0;
static int decode=0;
static int blocksize=32768;
static int blockcount=32;
static int segments=0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: ltsmin-convert options input output",
		"The file format is detected as follows:",
		"*.dir : uncompressed DIR format in a directory",
#ifdef HAVE_BCG_USER_H
		"*.bcg : BCG file format"},
#else
		"*.bcg : BCG file format (get CADP and/or recompile to enable)"},
#endif
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"*%s*  : DIR format using pattern substitution",
		" *    : DIR format using a GCF archive",
		NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,"-q",
		"do not print info messages",NULL,NULL,NULL},
	{"-segments",OPT_REQ_ARG,parse_int,&segments,NULL,
		"Set the number of segments in the output file.",
		"The default is the same as the input if the output support segments.",
		"The default is 1 if the output cannot support segmentation.",
		NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",NULL,NULL,NULL},
	{"-bs",OPT_REQ_ARG,parse_int,&blocksize,"-bs <block size>",
		"Set the block size to be used for copying streams.",
		"This is also used as the GCF block size.",
		NULL,NULL},
	{"-bc",OPT_REQ_ARG,parse_int,&blockcount,"-bc <block count>",
		"Set the number of blocks in one GCF cluster.",
		NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static char* plain_code;

int main(int argc, char *argv[]){
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	take_options(options,&argc,argv);
	archive_t ar_in,ar_out;
	stream_t ds;
	int N=0;
	int K=0;
	if(argc!=3){
		printoptions(options);
		exit(1);
	}
	lts_t lts=NULL;
#ifdef HAVE_BCG_USER_H
	BCG_INIT();
	BCG_TYPE_OBJECT_TRANSITION bcg_graph;
#endif
	if (strstr(argv[1],"%s")){
		ar_in=arch_fmt(argv[1],file_input,file_output,blocksize);
	} else if (strcmp(argv[1]+(strlen(argv[1])-4),".bcg")==0) {
#ifdef HAVE_BCG_USER_H
		ar_in=NULL;
#else
		Fatal(1,error,"BCG support has not been configured");
#endif
	} else if (IsADir(argv[1])) {
		ar_in=arch_dir_open(argv[1],blocksize);
	} else {
		ar_in=arch_gcf_read(raf_unistd(argv[1]));
	}
	if (strstr(argv[2],"%s")){
		Warning(info,"writing fmt");
		plain=1;
		plain_code=NULL;
		ar_out=arch_fmt(argv[2],file_input,file_output,blocksize);
	} else if (strcmp(argv[2]+(strlen(argv[2])-4),".dir")==0) {
		Warning(info,"writing dir");
		plain=1;
		plain_code=NULL;
		ar_out=arch_dir_create(argv[2],blocksize,DELETE_ALL);
	} else if (strcmp(argv[2]+(strlen(argv[2])-4),".bcg")==0) {
#ifdef HAVE_BCG_USER_H
		Warning(info,"writing BCG");
		ar_out=NULL;
		segments=1;
#else
		Fatal(1,error,"BCG support has not been configured");
#endif
	} else {
		Warning(info,"writing gcf");
		plain_code="";
		ar_out=arch_gcf_create(raf_unistd(argv[2]),blocksize,blocksize*blockcount,0,1);
	}
	Warning(info,"copying %s to %s",argv[1],argv[2]);
	uint32_t root_seg=(uint32_t)-1;
	uint32_t root_ofs=(uint32_t)-1;
	uint64_t root_64=(uint64_t)-1;
	if (ar_in){
		ds=arch_read(ar_in,"info",NULL);
		lts=lts_read_info(ds,&decode);
		DSclose(&ds);
		N=lts_get_segments(lts);
		root_seg=lts_get_root_seg(lts);
		root_ofs=lts_get_root_ofs(lts);
	} else {
#ifdef HAVE_BCG_USER_H
		if (sizeof(void*)==8){
			setenv("CADP_BITS","64",1);
		} else {
			setenv("CADP_BITS","32",1);
		}
		BCG_OT_READ_BCG_BEGIN (argv[1], &bcg_graph, 0);
		N=1;
		root_64=BCG_OT_INITIAL_STATE (bcg_graph);
#endif
	}
	if (segments==0) {
		K=N;
	} else {
		K=segments;
	}
	if (K==N) {
		Warning(info,"copying %d segment(s)",N);
	} else {
		Warning(info,"converting from %d to %d segments",N,K);
	}
	uint64_t offset[N+1];
	offset[0]=0;
	if(ar_in){
		for(int i=1;i<=N;i++) {
			offset[i]=offset[i-1]+lts_get_states(lts,i-1);
		}
		root_64=root_ofs+offset[root_seg];
	} else {
#ifdef HAVE_BCG_USER_H
		offset[1]=BCG_OT_NB_STATES (bcg_graph);
		root_seg=0;
		root_ofs=(uint32_t)root_64;
#endif
	}
	Warning(info,"state space has %"PRIu64" states",offset[N]);
	lts_t lts2=NULL;
	if (ar_out) {
		Log(info,"output compression is %s",plain?"disabled":"enabled");
		lts2=lts_new();
		lts_set_segments(lts2,K);
		if (N==K) {
			lts_set_root(lts2,root_seg,root_ofs);
		} else {
			lts_set_root(lts2,root_64%K,root_64/K);
		}
		for(int i=0;i<K;i++) {
			if (N==K) {
				lts_set_states(lts2,i,(offset[i+1]-offset[i]));
			} else {
				lts_set_states(lts2,i,(offset[N]+K-1-i)/K);
			}
		}
	} else {
#ifdef HAVE_BCG_USER_H
		BCG_IO_WRITE_BCG_BEGIN (argv[2],root_64,1,"ltsmin-convert",0);
#endif
	}
	//Warning(info,"reading the set of labels");
	string_index_t si=NULL;
	uint32_t tau=(uint32_t)-1;
	if (ar_in){
		si=lts_get_string_index(lts);
		ds=arch_read(ar_in,"TermDB",decode?"auto":NULL);
		for(int L=0;;L++){
			char*lbl=DSreadLN(ds);
			int len=strlen(lbl);
			if (len==0) {
				Warning(info,"read %d labels",L);
				break;
			}
			char*str;
			if (lbl[0]=='"' && lbl[len-1]=='"') {
				lbl[len-1]=0;
				str=lbl+1;
			} else {
				str=lbl;
			}
			if (strcmp(str,"tau")==0) tau=L;
			SIputAt(si,str,L);
			free(lbl);
		}
		DSclose(&ds);
	} else {
#ifdef HAVE_BCG_USER_H
		si=SIcreate();
		int L=BCG_OT_NB_LABELS (bcg_graph);
		for(int i=0;i<L;i++){
			if (BCG_OT_LABEL_VISIBLE (bcg_graph,i)){
				char*str=BCG_OT_LABEL_STRING (bcg_graph,i);
				SIputAt(si,str,i);
			} else {
				if (tau!=(uint32_t)-1) Fatal(1,error,"more than one invisible label");
				SIputAt(si,"tau",i);
				tau=i;
			}
		}
		Warning(info,"read %d labels",L);
#endif
	}
	if (ar_out) {
		int L=SIgetCount(si);
		string_index_t si2=lts_get_string_index(lts2);
		ds=arch_write(ar_out,"TermDB",plain?plain_code:"gzip",1);
		for(int i=0;i<L;i++){
			char*str=SIget(si,i);
			SIputAt(si2,str,i);
			DSwrite(ds,str,strlen(str));
			DSwrite(ds,"\n",1);
		}
		DSclose(&ds);
	}
	// BCG already open, open src/lbl/dst now.
	stream_t src[K][K];
	stream_t lbl[K][K];
	stream_t dst[K][K];
	uint32_t trans[K][K];
	if (ar_out){
		for(int i=0;i<K;i++) {
			for(int j=0;j<K;j++){
				char fname[1024];
				sprintf(fname,"src-%d-%d",i,j);
				src[i][j]=arch_write(ar_out,fname,plain?plain_code:"diff32|gzip",1);
				sprintf(fname,"label-%d-%d",i,j);
				lbl[i][j]=arch_write(ar_out,fname,plain?plain_code:"gzip",1);
				sprintf(fname,"dest-%d-%d",i,j);
				dst[i][j]=arch_write(ar_out,fname,plain?plain_code:"diff32|gzip",1);
				trans[i][j]=0;
			}
		}
	}
	// read then write transitions.
	if (ar_in) {
		for(int i=0;i<N;i++){
			for(int j=0;j<N;j++){
				char name[1024];
				sprintf(name,"src-%d-%d",i,j);
				stream_t src_in=arch_read(ar_in,name,decode?"auto":NULL);
				sprintf(name,"label-%d-%d",i,j);
				stream_t lbl_in=arch_read(ar_in,name,decode?"auto":NULL);
				sprintf(name,"dest-%d-%d",i,j);
				stream_t dst_in=arch_read(ar_in,name,decode?"auto":NULL);
				for(;;){
					if (DSempty(src_in)) break;
					uint32_t s=DSreadU32(src_in);
					uint32_t l=DSreadU32(lbl_in);
					uint32_t d=DSreadU32(dst_in);
					int ii;
					int jj;
					if (N==K) {
						ii=i;
						jj=j;
					} else {
						uint64_t ss=s+offset[i];
						uint64_t dd=d+offset[j];
						ii=ss%K;
						jj=dd%K;
						s=ss/K;
						d=dd/K;
					}
					if (ar_out) {
						DSwriteU32(src[ii][jj],s);
						DSwriteU32(lbl[ii][jj],l);
						DSwriteU32(dst[ii][jj],d);
						trans[ii][jj]++;
					} else {
#ifdef HAVE_BCG_USER_H
						BCG_IO_WRITE_BCG_EDGE (s,(l==tau)?"i":(SIget(si,l)),d);
#endif
					}
				}
			}
		}
	} else {
#ifdef HAVE_BCG_USER_H
		bcg_type_state_number bcg_s1;
		BCG_TYPE_LABEL_NUMBER bcg_label_number;
		bcg_type_state_number bcg_s2;
		BCG_OT_ITERATE_PLN (bcg_graph,bcg_s1,bcg_label_number,bcg_s2){
			uint32_t s;
			uint32_t l=bcg_label_number;
			uint32_t d;
			int ii;
			int jj;
			if (N==K) {
				ii=0;
				jj=0;
				s=bcg_s1;
				d=bcg_s2;
			} else {
				ii=bcg_s1%K;
				jj=bcg_s2%K;
				s=bcg_s1/K;
				d=bcg_s2/K;
			}
			if (ar_out) {
				DSwriteU32(src[ii][jj],s);
				DSwriteU32(lbl[ii][jj],l);
				DSwriteU32(dst[ii][jj],d);
				trans[ii][jj]++;
			} else {
				BCG_IO_WRITE_BCG_EDGE (s,(l==tau)?"i":(SIget(si,l)),d);
			}
		} BCG_OT_END_ITERATE;	
#endif
	}
	if (ar_out) {
		for(int i=0;i<K;i++) {
			for(int j=0;j<K;j++){
				DSclose(&(src[i][j]));
				DSclose(&(lbl[i][j]));
				DSclose(&(dst[i][j]));
				lts_set_trans(lts2,i,j,trans[i][j]);
			}
		}
		ds=arch_write(ar_out,"info",plain_code,1);
		lts_write_info(lts2,ds,LTS_INFO_DIR);
		DSclose(&ds);
		arch_close(&ar_out);
	} else {
#ifdef HAVE_BCG_USER_H
		BCG_IO_WRITE_BCG_END ();
#endif
	}
	if (ar_in) {
		arch_close(&ar_in);
	} else {
#ifdef HAVE_BCG_USER_H
		BCG_OT_READ_BCG_END (&bcg_graph);
#endif
	}
	return 0;
}


