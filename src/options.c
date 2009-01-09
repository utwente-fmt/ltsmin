#include "amconfig.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "runtime.h"
#include "git_version.h"

static int option_length(struct option options[]){
	int len;

	for(len=0;options[len].name;len++);
	return len;
}

static char *prefix_add(char *prefix,char *name){
	int prelen=strlen(prefix);
	int namelen=strlen(name);
	char *newname=(char*)malloc(prelen+namelen+1);

	memcpy(newname,prefix,prelen);
	memcpy(newname+prelen,name,namelen+1);
	return newname;
}

static char newhelp[256];

static char *prefix_help(char *prefix,char *help){
	int i=0;
	int j=0;
	int prelen=strlen(prefix);
	int k;

	while(help[i]){
		if (i>1 && help[i-2]=='-' && help[i-1]=='$' && help[i]=='-') {
			for(k=0;k<prelen;k++,j++) newhelp[j]=prefix[k];
		}
		newhelp[j]=help[i];
		i++;
		j++;
	}
	newhelp[j]=0;

	if (strcmp(newhelp,help)) return strdup(newhelp); else return help;
}

void print_conflicts(struct option options1[],struct option options2[]){
	int count1=option_length(options1);
	int count2=option_length(options2);
	int i,j;

	for(i=0;i<count1;i++) if(strlen(options1[i].name)) for(j=0;j<count2;j++) {
		if (strcmp(options1[i].name,options2[j].name)==0) {
			printf("option %s occurs in both option lists\n",options1[i].name);
			break;
		}
	}
}

struct option *concat_options(struct option options1[],struct option options2[]){
	struct option *newoptions;
	int count1=option_length(options1);
	int count2=option_length(options2);

	newoptions=(struct option *)malloc((count1+count2+1)*sizeof(struct option));
	memcpy(newoptions,options1,count1*sizeof(struct option));
	memcpy(newoptions+count1,options2,(count2+1)*sizeof(struct option));
	return newoptions;
}

struct option *prefix_options(char *prefix,struct option options[]){
	struct option *newoptions;
	int count=option_length(options);
	int size,i;

	size=(count+1)*sizeof(struct option);
	newoptions=(struct option *)malloc(size);
	memcpy(newoptions,options,size);
	for (i=0;i<count;i++){
		if (strlen(options[i].name)) {
			newoptions[i].name=prefix_add(prefix,options[i].name);
			if(options[i].print) newoptions[i].print=prefix_add(prefix,options[i].print);
		}
		newoptions[i].help0=prefix_help(prefix,options[i].help0);
		if(!options[i].help1) continue;
		newoptions[i].help1=prefix_help(prefix,options[i].help1);
		if(!options[i].help2) continue;
		newoptions[i].help2=prefix_help(prefix,options[i].help2);
		if(!options[i].help3) continue;
		newoptions[i].help3=prefix_help(prefix,options[i].help3);
	}
	return newoptions;
}

static char formattedhelp[256];

static char* formathelp(char *help){
	int i=0;
	int j=0;
	while(help[i]){
		if (help[i]=='-' && help[i+1]=='$' && help[i+2]=='-') i+=2;
		formattedhelp[j]=help[i];
		i++;
		j++;
	}
	formattedhelp[j]=0;
	return formattedhelp;
}

void printoptions(struct option options[]){
	int i,len,max;
	char format[32];
	char *printable;

	max=0;
	for(i=0;options[i].name;i++){
		printable=(options[i].print)?(options[i].print):(options[i].name);
		len=strlen(printable);
		if (len>max) max=len;
	}
	format[0]=0;
	sprintf(format,"  %%-%ds  %%s\n",max);

	for(i=0;options[i].name;i++){
		if (strlen(options[i].name)==0) {
			if (options[i].help0) printf("%s\n",formathelp(options[i].help0));
			if (options[i].help1) printf("%s\n",formathelp(options[i].help1));
			if (options[i].help2) printf("%s\n",formathelp(options[i].help2));
			if (options[i].help3) printf("%s\n",formathelp(options[i].help3));
			continue;
		}
		printable=(options[i].print)?(options[i].print):(options[i].name);
		printf(format,printable,formathelp(options[i].help0));
		if (options[i].help1) printf(format,"",formathelp(options[i].help1));
		if (options[i].help2) printf(format,"",formathelp(options[i].help2));
		if (options[i].help3) printf(format,"",formathelp(options[i].help3));
	}
}

void take_options(struct option options[],int *argc,char *argv[]){
	int count=option_length(options);
	int found;
	int used;
	int j;

	int rd=1,wr=1;
	for(;rd<(*argc);rd+=used){
		found=0;
		used=0;
		for(j=0;j<count;j++){
			if (!strcmp(argv[rd],options[j].name)) {
				switch(options[j].type) {
				case OPT_NORMAL:
					if (used!=0 && used !=1) {				
						fprintf(stderr,"inconsistent use of option %s\n",options[j].name);
						exit(1);
					}
					used=1;
					break;
				case OPT_OPT_ARG:
					if (used) break;
					if ((rd+1)<(*argc) && *(argv[rd+1]) != '-') {
						used=2;
					} else {
						used=1;
					}
					break;
				case OPT_REQ_ARG:
					if (used!=0 && used !=2) {				
						fprintf(stderr,"inconsistent use of option %s\n",options[j].name);
						exit(1);
					}
					used=2;
					if ((rd+1)>=(*argc)) {
						fprintf(stderr,"option %s needs argument\n",options[j].name);
						exit(1);
					}
					break;
				}
				found++;
				if (options[j].action) {
					char *optarg=(used==2)?argv[rd+1]:NULL;
					int result=options[j].action(argv[rd],optarg,options[j].arg);
					switch(result){
					case OPT_OK:
						break;
					case OPT_USAGE:
						printoptions(options);
						exit(1);
					case OPT_EXIT:
						exit(1);
					}
				}
			}
		}
		if (!found) {
			argv[wr]=argv[rd];
			wr++;
			used=1;
		}
	}
	*argc=wr;
}


int parse_options(struct option options[],int argc,char *argv[]){
	int i,j;
	int count=option_length(options);
	int found;
	int used;

	for(i=1;i<argc;i+=used){
		found=0;
		used=0;
		for(j=0;j<count;j++){
			if (!strcmp(argv[i],options[j].name)) {
				switch(options[j].type) {
				case OPT_NORMAL:
					if (used!=0 && used !=1) {				
						fprintf(stderr,"inconsistent use of option %s\n",options[j].name);
						exit(1);
					}
					used=1;
					break;
				case OPT_OPT_ARG:
					if (used) break;
					if ((i+1)<argc && *(argv[i+1]) != '-') {
						used=2;
					} else {
						used=1;
					}
					break;
				case OPT_REQ_ARG:
					if (used!=0 && used !=2) {				
						fprintf(stderr,"inconsistent use of option %s\n",options[j].name);
						exit(1);
					}
					used=2;
					if ((i+1)>=argc) {
						fprintf(stderr,"option %s needs argument\n",options[j].name);
						exit(1);
					}
					break;
				}
				found++;
				if (options[j].action) {
					char *optarg=(used==2)?argv[i+1]:NULL;
					int result=options[j].action(argv[i],optarg,options[j].arg);
					switch(result){
					case OPT_OK:
						break;
					case OPT_USAGE:
						printoptions(options);
						exit(1);
					case OPT_EXIT:
						exit(1);
					}
				}
			}
		}
		if (!found) break;
	}
	return i;
}

int usage(char* opt,char*optarg,void *arg){
	(void)opt;(void)optarg;(void)arg;
	return OPT_USAGE;
}

int inc_int(char* opt,char*optarg,void *arg){
	(void)opt;(void)optarg;
	(*((int*)arg))++;
	return OPT_OK;
}

int dec_int(char* opt,char*optarg,void *arg){
	(void)opt;(void)optarg;
	(*((int*)arg))--;
	return OPT_OK;
}

int set_int(char* opt,char*optarg,void *arg){
	(void)opt;(void)optarg;
	*((int*)arg)=1;
	return OPT_OK;
}

int reset_int(char* opt,char*optarg,void *arg){
	(void)opt;(void)optarg;
	*((int*)arg)=0;
	return OPT_OK;
}

int parse_int(char* opt,char*optarg,void *arg){
	(void)opt;
	char* tmp;
	*((int*)arg)=strtol(optarg,&tmp,0);
	if (*tmp!='\0') {
		fprintf(stderr,"set_int: cannot parse %s\n",optarg);
		return OPT_EXIT;
	} else {
		return OPT_OK;
	}
}

int parse_float(char* opt,char*optarg,void *arg){
	(void)opt;
	if (optarg==NULL) return OPT_EXIT;
	if (sscanf(optarg,"%f",(float*)arg)!=1) return OPT_EXIT;
	return OPT_OK;
}

int assign_string(char* opt,char*optarg,void *arg){
	(void)opt;
	*((char**)arg)=optarg;
	return OPT_OK;
}

int log_suppress(char* opt,char*optarg,void *arg){
	(void)opt;(void)optarg;
	log_set_flags(*((log_t*)arg),LOG_IGNORE);
	return OPT_OK;
}


static int prop_count=0;
static char**prop_name;
static char**prop_val;

void take_vars(int*argc,char*argv[]){
	prop_name=RTmalloc((*argc)*sizeof(char*));
	prop_val=RTmalloc((*argc)*sizeof(char*));
	int rd=1,wr=1;
	for(;rd<(*argc);rd++){
		//Warning(info,"argument %d is %s",i,(*argv)[i]);
		char *pos=strchr(argv[rd],'=');
		if(pos){
			*pos=0;
			Warning(info,"property %s is %s",argv[rd],pos+1);
			prop_name[prop_count]=argv[rd];
			prop_val[prop_count]=pos+1;
			prop_count++;
		} else {
			argv[wr]=argv[rd];
			wr++;
		}
	}
	*argc=wr;
}

char* prop_get_S(char*name,char* def_val){
	for(int i=0;i<prop_count;i++){
		if(!strcmp(name,prop_name[i])) return prop_val[i];
	}
	return def_val;
}

uint32_t prop_get_U32(char*name,uint32_t def_val){
	for(int i=0;i<prop_count;i++){
		if(!strcmp(name,prop_name[i])) return atoi(prop_val[i]);
	}
	return def_val;
}

uint64_t prop_get_U64(char*name,uint64_t def_val){
	for(int i=0;i<prop_count;i++){
		if(!strcmp(name,prop_name[i])) return atoll(prop_val[i]);
	}
	return def_val;
}

int print_version(char* opt,char*optarg,void *arg){
	if (strcmp(GIT_VERSION,"")) {
		fprintf(stdout,"%s\n",GIT_VERSION);
	} else {
		fprintf(stdout,"%s\n",PACKAGE_STRING);
	}
	return OPT_EXIT;
}

