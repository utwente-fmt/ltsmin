
#ifndef DATA_IO_H
#define DATA_IO_H

#include "config.h"
#include <sys/types.h>
#include <stdio.h>

int fwrite64(FILE *f,u_int64_t i);
int fwrite32(FILE *f,int i);
int fwrite16(FILE *f,int i);
int fwrite8(FILE *f,int i);

int fread64(FILE *f,u_int64_t *ip);
int fread32(FILE *f,int *ip);
int fread16(FILE *f,int *ip);
int fread8(FILE *f,int *ip);

int fwriteN(FILE *f,char data[],int N);
int freadN(FILE *f,char data[],int N);

#endif
