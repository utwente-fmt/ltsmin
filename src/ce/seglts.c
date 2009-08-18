
#include "seglts.h"
#include "data_io.h"
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

static char errormessage[1024]="Programming error in seglts: success or forgot to write error message";

char *SLTSerror(){
	return errormessage;
}

int SLTSCreateInfo(seginfo_t *info,int segment_count){
	int i,j;

	(*info)=(seginfo_t)malloc(sizeof(struct seginfo));
	if ((*info)==NULL) {
		sprintf(errormessage,"out of memory");
		return 1;
	}
	(*info)->info=NULL;
	(*info)->segment_count=segment_count;
	(*info)->initial_seg=0;
	(*info)->initial_ofs=0;
	(*info)->label_count=0;
	(*info)->label_tau=0;
	(*info)->top_count=0;
	(*info)->state_count=(int*)malloc(segment_count*sizeof(int));
	(*info)->transition_count=(int**)malloc(segment_count*sizeof(int*));
	for(i=0;i<segment_count;i++) {
		(*info)->state_count[i]=0;
		(*info)->transition_count[i]=(int*)malloc(segment_count*sizeof(int));
		for(j=0;j<segment_count;j++) {
			(*info)->transition_count[i][j]=0;
		}
	}
	return 0;
}


static int create_info_v31(seginfo_t *info,FILE *f) {
	char *info_string;
	int len;
	int segment_count;
	int i,j;

	fread16(f,&len);
	info_string=(char*)malloc(len+1);
	freadN(f,info_string,len);
	info_string[len]=0;
	fread32(f,&segment_count);
	SLTSCreateInfo(info,segment_count);
	(*info)->info=info_string;
	fread32(f,&((*info)->initial_seg));
	fread32(f,&((*info)->initial_ofs));
	fread32(f,&((*info)->label_count));
	fread32(f,&((*info)->label_tau));
	fread32(f,&((*info)->top_count));
	for(i=0;i<segment_count;i++){
		fread32(f,((*info)->state_count)+i);
	}
	for(i=0;i<segment_count;i++){
		for(j=0;j<segment_count;j++){
			fread32(f,&((*info)->transition_count[i][j]));
		}
	}
	return 0;
}


int SLTSReadInfo(seginfo_t *info,char *name){
	char filename[1024];
	FILE* f;
	int version;

	sprintf(filename,"%s/info",name);
	f=fopen(filename,"r");
	fread32(f,&version);
	switch(version){
		default:
			sprintf(errormessage,"unknown version %d",version);
			fclose(f);
			return 1;
		case 31:
			if(create_info_v31(info,f)) {
				fclose(f);
				return 1;
			}
			break;
	}
	return 0;
}

int SLTSWriteInfo(seginfo_t info,char *name){
	int i,j,len;
	char info_name[1024];
	FILE *f;

	sprintf(info_name,"%s/info",name);
	f=fopen(info_name,"w");
	fwrite32(f,31);
	if (info->info==NULL){
		fwrite16(f,0);
	} else {
		len=strlen(info->info);
		fwrite16(f,len);
		fwriteN(f,info->info,len);
	}
	fwrite32(f,info->segment_count);
	fwrite32(f,info->initial_seg);
	fwrite32(f,info->initial_ofs);
	fwrite32(f,info->label_count);
	fwrite32(f,info->label_tau);
	fwrite32(f,info->top_count);
	for(i=0;i<info->segment_count;i++){
		fwrite32(f,info->state_count[i]);
	}
	for(i=0;i<info->segment_count;i++){
		for(j=0;j<info->segment_count;j++){
			fwrite32(f,info->transition_count[i][j]);
		}
	}
	fclose(f);
	return 0;
}

