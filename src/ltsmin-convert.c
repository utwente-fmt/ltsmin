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

static int plain=0;
static int decode=0;
static int blocksize=32768;
static int blockcount=32;
static int segments=0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: ltsmin-convert options input output",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,"-q",
		"do not print info messages",NULL,NULL,NULL},
	{"-segments",OPT_REQ_ARG,parse_int,&segments,NULL,
		"Set the number of segments in the output file.",
		"The default is the same as the input.",
		NULL,NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",NULL,NULL,NULL},
	{"-bs",OPT_REQ_ARG,parse_int,&blocksize,"-bs <block size>",
		"Set the block size to be used for copying streams.",
		NULL,NULL,NULL},
	{"-bc",OPT_REQ_ARG,parse_int,&blockcount,"-bc <block count>",
		"Set the number of block in one GCF cluster.",
		NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static char* plain_code;

static void stream_copy(archive_t src,archive_t dst,char*name,char*decode,char*encode){
	stream_t is=arch_read(src,name,decode);
	stream_t os=arch_write(dst,name,encode,1);
	char buf[blocksize];
	for(;;){
		int len=stream_read_max(is,buf,blocksize);
		if (len) stream_write(os,buf,len);
		if(len<blocksize) break;
	}
	stream_close(&is);
	stream_close(&os);	
}

int main(int argc, char *argv[]){
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	take_options(options,&argc,argv);
	archive_t ar_in,ar_out;
	if(argc!=3){
		printoptions(options);
		exit(1);
	}
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
#else
		Fatal(1,error,"BCG support has not been configured");
#endif
	} else {
		Warning(info,"writing gcf");
		plain_code="";
		ar_out=arch_gcf_create(raf_unistd(argv[2]),blocksize,blocksize*blockcount,0,1);
	}
	Warning(info,"copying %s to %s",argv[1],argv[2]);
	stream_t ds;
	ds=arch_read(ar_in,"info",NULL);
	lts_t lts=lts_read_info(ds,&decode);
	DSclose(&ds);
	Log(info,"output compression is %s",plain?"disabled":"enabled");

	ds=arch_read(ar_in,"TermDB",decode?"auto":NULL);
	string_index_t si=lts_get_string_index(lts);
	int L;
	for(L=0;;L++){
		char*lbl=DSreadLN(ds);
		if (strlen(lbl)==0) {
			Warning(info,"read %d labels",L);
			break;
		}
		if (strcmp(lbl,"\"tau\"")==0) {
			Warning(info,"the invisible step should be tau rather than \"tau\".");
			sprintf(lbl,"tau");
		}
		SIputAt(si,lbl,L);
		free(lbl);
	}
	DSclose(&ds);

	int N=lts_get_segments(lts);
	int K;
	if (segments==0) {
		K=N;
	} else {
		K=segments;
	}
	if (K==N) {
		Warning(info,"copying %d segments",N);
		ds=arch_write(ar_out,"TermDB",plain?plain_code:"gzip",1);
		for(int i=0;i<L;i++){
			char*str=SIget(si,i);
			DSwrite(ds,str,strlen(str));
			DSwrite(ds,"\n",1);
		}
		DSclose(&ds);
		ds=arch_write(ar_out,"info",plain?plain_code:"",1);
		lts_write_info(lts,ds,LTS_INFO_DIR);
		DSclose(&ds);
		for(int i=0;i<N;i++){
			for(int j=0;j<N;j++){
				char name[1024];
				sprintf(name,"src-%d-%d",i,j);
				stream_copy(ar_in,ar_out,name,decode?"auto":NULL,plain?plain_code:"diff32|gzip");
				sprintf(name,"label-%d-%d",i,j);
				stream_copy(ar_in,ar_out,name,decode?"auto":NULL,plain?plain_code:"gzip");
				sprintf(name,"dest-%d-%d",i,j);
				stream_copy(ar_in,ar_out,name,decode?"auto":NULL,plain?plain_code:"diff32|gzip");
			}
		}
	} else {
		Warning(info,"converting from %d to %d segments",N,K);
		lts_t lts2=lts_new();
		lts_set_segments(lts2,K);
		string_index_t si2=lts_get_string_index(lts2);
		ds=arch_write(ar_out,"TermDB",plain?plain_code:"gzip",1);
		for(int i=0;i<L;i++){
			char*str=SIget(si,i);
			SIputAt(si2,str,i);
			DSwrite(ds,str,strlen(str));
			DSwrite(ds,"\n",1);
		}
		DSclose(&ds);

		uint64_t offset[N+1];
		offset[0]=0;
		for(int i=1;i<=N;i++) {
			offset[i]=offset[i-1]+lts_get_states(lts,i-1);
		}
		uint64_t root2=lts_get_root_ofs(lts)+offset[lts_get_root_seg(lts)];
		lts_set_root(lts2,root2%K,root2/K);

		stream_t src[K][K];
		stream_t lbl[K][K];
		stream_t dst[K][K];
		uint32_t trans[K][K];
		for(int i=0;i<K;i++) {
			lts_set_states(lts2,i,(offset[N]+K-1-i)/K);
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
					uint64_t s=DSreadU32(src_in)+offset[i];
					uint32_t l=DSreadU32(lbl_in);
					uint64_t d=DSreadU32(dst_in)+offset[j];
					int ii=s%K;
					int jj=d%K;
					DSwriteU32(src[ii][jj],s/K);
					DSwriteU32(lbl[ii][jj],l);
					DSwriteU32(dst[ii][jj],d/K);
					trans[ii][jj]++;	
				}
			}
		}
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
	}
	arch_close(&ar_in);
	arch_close(&ar_out);
	return 0;
}


