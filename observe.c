#define _XOPEN_SOURCE 500
#include "config.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "runtime.h"
#include <string.h>
#include <stdio.h>

typedef struct stat_buf* stat_buf_t;

struct stat_buf {
	int fd;
	char buf[2048];
};


#define STAT_BUF 2048
#define STAT_PID  0
#define STAT_COMM  1
#define STAT_STATE 2
#define STAT_PPID 3
#define STAT_PGRP 4
#define STAT_SESSION 5
#define STAT_TTY 6
#define STAT_TPGID 7
#define STAT_FLAGS 8
#define STAT_MINFLT 9
#define STAT_CMINFLT 10
#define STAT_MAJFLT 11
#define STAT_CMAJFLT 12
#define STAT_UTIME 13
#define STAT_STIME 14
#define STAT_CUTIME 15
#define STAT_CSTIME 16
#define STAT_PRIO 17
#define STAT_NICE 18
#define STAT_THREADS 19
#define STAT_ITREAL 20
#define STAT_START 21
#define STAT_VSIZE 22
#define STAT_RSS 23
#define STAT_RLIM 24
#define STAT_START_CODE 25
#define STAT_END_CODE 26
#define STAT_START_STACK 27
#define STAT_ESP 28
#define STAT_EIP 29
#define STAT_SIGNAL 30
#define STAT_BLOCKED 31
#define STAT_IGNORE 32
#define STAT_CATCH 33
#define STAT_WCHAN 34
#define STAT_NSWAP 35
#define STAT_CNSWAP 36
#define STAT_EXIT 37
#define STAT_CPU 38
#define STAT_RT_PRIO 39
#define STAT_POLICY 40
#define STAT_DELAY 41
#define STAT_ITEMS 44

void* observe(void*arg){
#define pid (*((pid_t*)arg))
	set_label("observe aux");
	Warning(info,"observing %d",pid);
	char buffer[STAT_BUF];
	int  ofs[STAT_ITEMS];
	int fd;
	int i,j;
	ofs[0]=0;
	sprintf(buffer,"/proc/%d/stat",pid);
	fd = open(buffer, O_RDONLY);
	if (fd<0) {
		WarningCall(error,"open");
		return NULL;
	}
	int count=0;
	for(;;){
		count++;
		//int len=pread(fd,buffer,STAT_BUF,0);
		int len=read(fd,buffer,STAT_BUF);
		if (len==STAT_BUF){
			Warning(error,"buffer overflow");
			if (close(fd)<0){
				WarningCall(error,"close");
			}
			return NULL;
		}
		if (len<0) {
			WarningCall(error,"(p)read failed at attempt %d",count);
			if (close(fd)<0){
				WarningCall(error,"close");
			}
			return NULL;
		}
		buffer[len]=0;
		for(i=0;;i++){
			if(buffer[i]==' '){
				buffer[i]=0;
				ofs[1]=i+1;
				break;
			}
		}
		i=STAT_ITEMS-1;
		j=len-2;
		while(i>1){
			if (buffer[j]==' ') {
				ofs[i]=j+1;
				buffer[j]=0;
				i--;
			}
			j--;
		}
		Warning(info,"poll %s STATE %s VSIZE %s RSS %s UTIME %s START %s",&(buffer[ofs[STAT_COMM]]),&buffer[ofs[STAT_STATE]],
			&buffer[ofs[STAT_VSIZE]],&buffer[ofs[STAT_RSS]],&buffer[ofs[STAT_UTIME]],&buffer[ofs[STAT_START]]);
		len=lseek(fd, 0, SEEK_SET);
		if (len<0) {
			WarningCall(error,"seek failed at attempt %d",count);
			if (close(fd)<0){
				WarningCall(error,"close");
			}
			return NULL;
		}
		usleep(250000);
	}
#undef pid
}

int main(int argc,char *const argv[]){
	pid_t pid;
	pthread_t thr;

	runtime_init();
	set_label("observe main");

	pid=fork();
	if (pid<0) FatalCall(1,error,"new process fork");
	if (pid==0) {
		//int i,j,c;
		//for(j=0;j<1000;j++){
		//	c=0;
		//	for(i=0;i<1000000;i++) c=c+i;
		//}
		execvp(argv[1],&(argv[1]));
		FatalCall(1,error,"new process execv");
	}
	Warning(info,"pid is %d",pid);
	if (pthread_create(&thr,NULL,observe,&pid)) {
		Fatal(1,error,"couldn't create observer thread");
	}
	int status;
	if (waitpid(pid, &status, 0)<0){
		FatalCall(1,error,"waitpid");
	}
	if (WIFEXITED(status)) {
		Warning(info,"exited, status=%d\n", WEXITSTATUS(status));
	}
	if (WIFSIGNALED(status)) {
		Warning(info,"killed by signal %d\n", WTERMSIG(status));
	}
	pthread_join(thr,NULL);
	return 0;
}


