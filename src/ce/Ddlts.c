
#include <runtime.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dir_ops.h"
#include "seglts.h"
#include "data_io.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Ddlts.h"

#define BUFFER_SIZE (1024 * 1024)


/****************************************************

dlts_t dlts_create(MPI_Comm communicator)

 ****************************************************/

dlts_t dlts_create(MPI_Comm communicator){
	dlts_t lts;
	lts=(dlts_t)malloc(sizeof(struct dlts));
	if (!lts) Fatal(1,error,"out of memory in dlts_create");
	lts->dirname=NULL;
	lts->segment_count=0;
  lts->comm=communicator;
	return lts;
}


/****************************************************

void dlts_free(dlts_t lts)

 ****************************************************/

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
  free(lts->comm);
	free(lts);
}


/****************************************************

 ****************************************************/

void dlts_getinfo(dlts_t lts){
	FILE* info;
	char name[1024],buf[BUFFER_SIZE];
	int i,j,version,len,dummy;

	sprintf(name,"%s/info",lts->dirname);
	info=fopen(name,"r");
	setvbuf(info,buf,_IOFBF,BUFFER_SIZE);
	fread32(info,&version);
	if (version!=31) Fatal(1,error,"wrong file version: %d",version);
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



/****************************************************

 ****************************************************/

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

/****************************************************

 ****************************************************/

void dlts_load_src(dlts_t lts,int from,int to){
	int i;
	char name[1024],buf[BUFFER_SIZE];
	FILE* file;
	int *data;

	sprintf(name,"%s/src-%d-%d",lts->dirname,from,to);
	if ((file = fopen(name,"r")) == NULL)
	 Fatal(1,error,"error opening file %s ",name);

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


/****************************************************

 ****************************************************/

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





/****************************************************

 ****************************************************/

int dlts_read(dlts_t lts, char* filename, int type){
(void)type;
 int me, nodes, i;

 MPI_Barrier(lts->comm);
 MPI_Comm_size(lts->comm, &nodes);
 MPI_Comm_rank(lts->comm, &me);
 // Warning(info,"%d starts reading", me); fflush(stdout);
 if (me==0) Warning(info,"start reading");
 lts->dirname=filename;
 dlts_getinfo(lts);
 dlts_getTermDB(lts);

 if (nodes != lts->segment_count){
	if (me==0){ 
	 Warning(info,"segment count: %d does not equal worker count: %d", 
					 lts->segment_count, nodes);
	 fflush(stdout);
	}
	MPI_Barrier(lts->comm);
	MPI_Finalize();
	exit(1);
 }

 for(i=0;i<lts->segment_count;i++){
	dlts_load_src(lts,me,i);
	dlts_load_label(lts,me,i);
	dlts_load_dest(lts,me,i);
	dlts_load_src(lts,i,me);
	dlts_load_label(lts,i,me);
	dlts_load_dest(lts,i,me);
 }

 // Warning(info,"%d finished reading", me); fflush(stdout);
 MPI_Barrier(lts->comm); 
 if (me==0) Warning(info,"finished reading");
 return 0;
}

/****************************************************

 ****************************************************/

int dlts_writedir(dlts_t lts, char* name, int writemode){
(void)writemode;
 /*
code more or less copy-pasted from lts.c (lts_write_dir) 
	*/
 int me, nodes;
 seginfo_t info;
 char filename[1024];
 int i,j;
 int* tmp;
 FILE *output;
 FILE *src_out;
 FILE *lbl_out;
 FILE *dst_out;
 
 MPI_Barrier(lts->comm);
 MPI_Comm_size(lts->comm, &nodes);
 MPI_Comm_rank(lts->comm, &me);
 // Warning(info,"%d starts writing", me); 

 SLTSCreateInfo(&info,lts->segment_count);
 if (me==0){
	info->label_tau=lts->tau;
	info->label_count=lts->label_count;
	info->initial_seg=lts->root_seg; 
	info->initial_ofs=lts->root_ofs;
	create_empty_dir(name,DELETE_ALL);
	snprintf(filename, sizeof filename, "%s/TermDB",name);
	output=fopen(filename,"w");
	for(i=0;i<lts->label_count;i++){
	 fprintf(output,"%s\n",lts->label_string[i]);
	}
	fclose(output);
 }

 MPI_Barrier(lts->comm);

 for(j=0;j<lts->segment_count;j++) {
	//	Warning(info,"Writing transitions %d->%d   ",me,j);
	sprintf(filename,"%s/src-%d-%d",name,me,j);
	if((src_out=fopen(filename,"w"))==NULL)
	 Fatal(1,error,"error creating file %s  ",filename);
	sprintf(filename,"%s/label-%d-%d",name,me,j);
	lbl_out=fopen(filename,"w");
	sprintf(filename,"%s/dest-%d-%d",name,me,j);
	dst_out=fopen(filename,"w");
	
	for(i=0;i<lts->transition_count[me][j];i++){
	 fwrite32(src_out,lts->src[me][j][i]);
	 fwrite32(lbl_out,lts->label[me][j][i]);
	 fwrite32(dst_out,lts->dest[me][j][i]);
	}
	fclose(src_out);
	fclose(lbl_out);
	fclose(dst_out);
	MPI_Barrier(lts->comm);
 }

 i = lts->state_count[me];
 MPI_Gather(&i, 1, MPI_INT, info->state_count, 1, MPI_INT, 0, lts->comm);
 tmp = (int*)calloc(nodes, sizeof(int));
 for(j=0; j<nodes; j++){
	i = lts->transition_count[me][j];
	MPI_Gather(&i, 1, MPI_INT, tmp, 1, MPI_INT, 0, lts->comm);
	if (me==0)
	 for(i=0;i<nodes;i++)
		info->transition_count[i][j] = tmp[i];
 }
 
 if (me==0)
	SLTSWriteInfo(info,name);

 // Warning(info,"%d finished writing", me); 
 MPI_Barrier(lts->comm);
 return 0;
}

