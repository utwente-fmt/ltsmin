#ifndef OPTIONS_H
#define OPTIONS_H

typedef int (*option_action)(char*,char*,void*);
#define OPT_OK 0
#define OPT_USAGE 1
#define OPT_EXIT 2

extern int usage(char* opt,char*optarg,void *arg);
extern int inc_int(char* opt,char*optarg,void *arg);
extern int set_int(char* opt,char*optarg,void *arg);
extern int reset_int(char* opt,char*optarg,void *arg);
extern int parse_int(char* opt,char*optarg,void *arg);
extern int assign_string(char* opt,char*optarg,void *arg);

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

#endif
