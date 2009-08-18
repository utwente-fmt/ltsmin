
#include <malloc.h>
#include "messages.h"
#include "dlts.h"
#include <unistd.h>
#include <stdio.h>
#ifdef USE_ZLIB
#include <zlib.h>
#endif
#include <string.h>
#include "data_io.h"
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFFER_SIZE (1024 * 1024)

dlts_t dlts_create(){
	dlts_t lts;

	lts=(dlts_t)malloc(sizeof(struct dlts));
	if (!lts) Fatal(1,1,"out of memory in dlts_create");
	lts->dirname=NULL;
	lts->segment_count=0;
	return lts;
}

void dlts_free(dlts_t lts){
	int i,j;
	for(i=0;i<lts->segment_count;i++){
		free(lts->transition_count[i]);
		free(lts->label_string[i]);
		for(j=0;j<lts->segment_count;j++){
			if (lts->src[i][j]) free(lts->src[i][j]);
			if (lts->label[i][j]) free(lts->label[i][j]);
			if (lts->dest[i][j]) free(lts->dest[i][j]);
		}
		free(lts->src[i]);
		free(lts->label[i]);
		free(lts->dest[i]);
	}
	free(lts->transition_count);
	free(lts->label_string);
	free(lts->src);
	free(lts->label);
	free(lts->dest);
	free(lts);
}

void dlts_getinfo(dlts_t lts){
	FILE* info;
	char name[1024],buf[BUFFER_SIZE];
	int i,j,version,len,dummy;

	sprintf(name,"%s/info",lts->dirname);
	info=fopen(name,"r");
	setvbuf(info,buf,_IOFBF,BUFFER_SIZE);
	fread32(info,&version);
	if (version!=31) Fatal(1,1,"wrong file version: %d",version);
	fread16(info,&len);
	lts->info=(char*)malloc(len+1);
	freadN(info,lts->info,len);
	lts->info[len]=0;
	fread32(info,&(lts->segment_count));
	fread32(info,&(lts->root_seg));
	fread32(info,&(lts->root_ofs));
	fread32(info,&(lts->label_count));
	fread32(info,&(lts->tau));
	fread32(info,&dummy);
	lts->state_count=(int*)malloc(lts->segment_count*sizeof(int));
	for(i=0;i<lts->segment_count;i++){
		fread32(info,(lts->state_count)+i);
	}
	lts->transition_count=(int**)malloc(lts->segment_count*sizeof(int*));
	for(i=0;i<lts->segment_count;i++){
		lts->transition_count[i]=(int*)malloc(lts->segment_count*sizeof(int));
		for(j=0;j<lts->segment_count;j++){
			fread32(info,&(lts->transition_count[i][j]));
		}
	}
	lts->src=(int***)malloc(lts->segment_count*sizeof(int**));
	lts->label=(int***)malloc(lts->segment_count*sizeof(int**));
	lts->dest=(int***)malloc(lts->segment_count*sizeof(int**));
	for(i=0;i<lts->segment_count;i++){
		lts->src[i]=(int**)malloc(lts->segment_count*sizeof(int*));
		lts->label[i]=(int**)malloc(lts->segment_count*sizeof(int*));
		lts->dest[i]=(int**)malloc(lts->segment_count*sizeof(int*));
		for(j=0;j<lts->segment_count;j++){
			lts->src[i][j]=NULL;
			lts->label[i][j]=NULL;
			lts->dest[i][j]=NULL;
		}
	}
}

#ifdef USE_ZLIB
void dlts_getTermDB(dlts_t lts){
	char buf[MAX_TERM_LEN+2];
	char name[1024];
	gzFile f;
	int i;
	int len;

	sprintf(name,"%s/TermDB",lts->dirname);
	f=gzopen(name,"r");
	lts->label_string=(char**)malloc(lts->label_count*sizeof(char*));
	for(i=0;i<lts->label_count;i++){
		gzgets(f,buf,MAX_TERM_LEN+1);
		len=strlen(buf);
		len--;
		buf[len]=0;
		lts->label_string[i]=strdup(buf);
	}
	gzclose(f);
}
#else
void dlts_getTermDB(dlts_t lts){
	char name[1024];
	int fd;
	struct stat info;
	char *ptr;
	int i;
	char *termdata;

	sprintf(name,"%s/TermDB",lts->dirname);
	fd=open(name,O_RDONLY);
	fstat(fd,&info);
	lts->label_string=(char**)malloc(lts->label_count*sizeof(char*));
	termdata=(char*)malloc(info.st_size);
	read(fd,termdata,info.st_size);
	ptr=termdata;
	for(i=0;i<lts->label_count;i++){
		lts->label_string[i]=ptr;
		while(*ptr!='\n') ptr++;
		*ptr=0;
		lts->label_string[i]=strdup(lts->label_string[i]);
		ptr++;
	}
	close(fd);
	free(termdata);
}
#endif

void dlts_load_src(dlts_t lts,int from,int to){
	int i;
	char name[1024],buf[BUFFER_SIZE];
	FILE* file;
	int *data;

	sprintf(name,"%s/src-%d-%d",lts->dirname,from,to);
	file=fopen(name,"r");
	setvbuf(file,buf,_IOFBF,BUFFER_SIZE);
	data=(int*)malloc((lts->transition_count[from][to])*sizeof(int));
	for(i=0;i<lts->transition_count[from][to];i++){
		fread32(file,data+i);
	}
	lts->src[from][to]=data;
	fclose(file);
}

void dlts_load_label(dlts_t lts,int from,int to){
	int i;
	char name[1024],buf[BUFFER_SIZE];
	FILE* file;
	int *data;

	sprintf(name,"%s/label-%d-%d",lts->dirname,from,to);
	file=fopen(name,"r");
	setvbuf(file,buf,_IOFBF,BUFFER_SIZE);
	data=(int*)malloc(lts->transition_count[from][to]*sizeof(int));
	for(i=0;i<lts->transition_count[from][to];i++){
		fread32(file,data+i);
	}
	lts->label[from][to]=data;
	fclose(file);
}

void dlts_free_dest(dlts_t lts,int from,int to){
	free(lts->dest[from][to]);
	lts->dest[from][to]=NULL;
}
void dlts_free_src(dlts_t lts,int from,int to){
	free(lts->src[from][to]);
	lts->src[from][to]=NULL;
}
void dlts_free_label(dlts_t lts,int from,int to){
	free(lts->label[from][to]);
	lts->label[from][to]=NULL;
}

void dlts_load_dest(dlts_t lts,int from,int to){
	int i;
	char name[1024],buf[BUFFER_SIZE];
	FILE* file;
	int *data;

	sprintf(name,"%s/dest-%d-%d",lts->dirname,from,to);
	file=fopen(name,"r");
	setvbuf(file,buf,_IOFBF,BUFFER_SIZE);
	data=(int*)malloc(lts->transition_count[from][to]*sizeof(int));
	for(i=0;i<lts->transition_count[from][to];i++){
		fread32(file,data+i);
	}
	lts->dest[from][to]=data;
	fclose(file);
}





