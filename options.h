#ifndef OPTIONS_H
#define OPTIONS_H

#include "config.h"
#include <stdint.h>

typedef int (*option_action)(char*,char*,void*);
#define OPT_OK 0
#define OPT_USAGE 1
#define OPT_EXIT 2

extern int usage(char* opt,char*optarg,void *arg);
extern int inc_int(char* opt,char*optarg,void *arg);
extern int dec_int(char* opt,char*optarg,void *arg);
extern int set_int(char* opt,char*optarg,void *arg);
extern int reset_int(char* opt,char*optarg,void *arg);
extern int parse_int(char* opt,char*optarg,void *arg);
extern int parse_float(char* opt,char*optarg,void *arg);
extern int assign_string(char* opt,char*optarg,void *arg);
extern int log_suppress(char* opt,char*optarg,void *arg);

#define OPT_NORMAL 0
#define OPT_OPT_ARG 1
#define OPT_REQ_ARG 2

struct option {
	char *name;
	int type;
	option_action action;
	void *arg;
	char *print;
	char *help0;
	char *help1;
	char *help2;
	char *help3;
};

extern void print_conflicts(struct option options1[],struct option options2[]);
extern struct option *concat_options(struct option options1[],struct option options2[]);
extern struct option *prefix_options(char *prefix,struct option options[]);
extern void printoptions(struct option options[]);
extern int parse_options(struct option options[],int argc,char *argv[]);
extern void take_options(struct option options[],int *argc,char *argv[]);

extern void take_vars(int *argc,char *argv[]);
extern char* prop_get_S(char*name,char* def_val);
extern uint32_t prop_get_U32(char*name,uint32_t def_val);
extern uint64_t prop_get_U64(char*name,uint64_t def_val);

#endif


