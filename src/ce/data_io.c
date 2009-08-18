#include "data_io.h"

int fwrite64(FILE *f,uint64_t i){
	if (EOF==fputc(0xff & (i>>56),f)) return EOF;
	if (EOF==fputc(0xff & (i>>48),f)) return EOF;
	if (EOF==fputc(0xff & (i>>40),f)) return EOF;
	if (EOF==fputc(0xff & (i>>32),f)) return EOF;
	if (EOF==fputc(0xff & (i>>24),f)) return EOF;
	if (EOF==fputc(0xff & (i>>16),f)) return EOF;
	if (EOF==fputc(0xff & (i>>8) ,f)) return EOF;
	if (EOF==fputc(0xff &  i     ,f)) return EOF;
	return 0;
}

int fwrite32(FILE *f,int i){
	if (EOF==fputc(0xff & (i>>24),f)) return EOF;
	if (EOF==fputc(0xff & (i>>16),f)) return EOF;
	if (EOF==fputc(0xff & (i>>8) ,f)) return EOF;
	if (EOF==fputc(0xff &  i     ,f)) return EOF;
	return 0;
}

int fwrite16(FILE *f,int i){
	if (EOF==fputc(0xff & (i>>8) ,f)) return EOF;
	if (EOF==fputc(0xff &  i     ,f)) return EOF;
	return 0;
}

int fwrite8(FILE *f,int i){
	if (EOF==fputc(0xff & i ,f)) return EOF;
	return 0;
}

int fread64(FILE *f,uint64_t *ip){
	uint64_t i;
	int c;

	if (EOF==(c=fgetc(f))) return EOF;
	i=c&0xff;
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);

	*ip=i;
	return 0;
}

int fread32(FILE *f,int *ip){
	int i;
	int c;

	if (EOF==(c=fgetc(f))) return EOF;
	i=c&0xff;
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);

	*ip=i;
	return 0;
}

int fread16(FILE *f,int *ip){
	int i;
	int c;

	if (EOF==(c=fgetc(f))) return EOF;
	i=c&0xff;
	if (EOF==(c=fgetc(f))) return EOF;
	i=(i<<8) | (c&0xff);

	*ip=i;
	return 0;
}

int fread8(FILE *f,int *ip){
	int i;
	int c;

	if (EOF==(c=fgetc(f))) return EOF;
	i=c&0xff;

	*ip=i;
	return 0;
}

int fwriteN(FILE *f,char data[],int N) {
	int i;
	for(i=0;i<N;i++) if (EOF==fputc(data[i] ,f)) return EOF;
	return 0;
}

int freadN(FILE *f,char data[],int N) {
	int i,c;
	for(i=0;i<N;i++) if (EOF==(c=fgetc(f))) return EOF ; else data[i]=c;
	return 0;
}


