#include <stdarg.h>
#include <stdio.h>
#include "messages.h"

int verbosity;

void Warning(int v,const char *fmt,...) {
	if (v<=verbosity){
		va_list args;
		va_start(args,fmt);
		vfprintf(stderr,fmt,args);
		fprintf(stderr,"\n");
		va_end(args);
	}
}

void WarningCall(int v,const char *fmt,...) {
	if (v<=verbosity){
		va_list args;
		va_start(args,fmt);
		vfprintf(stderr,fmt,args);
		fprintf(stderr,": ");
		perror("");
		va_end(args);
	}
}

void Fatal(int code,int v,const char *fmt,...){
	if (v<=verbosity){
		va_list args;
		va_start(args,fmt);
		vfprintf(stderr,fmt,args);
		fprintf(stderr,"\n");
		va_end(args);
	}
	exit(code);
}

void FatalCall(int code,int v,const char *fmt,...){
	if (v<=verbosity){
		va_list args;
		va_start(args,fmt);
		vfprintf(stderr,fmt,args);
		fprintf(stderr,": ");
		perror("");
		va_end(args);
	}
	exit(code);
}

