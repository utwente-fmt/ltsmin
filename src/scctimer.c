#include "config.h"
#include <sys/types.h>
#include <sys/times.h>
#include <stdlib.h>
#include <unistd.h>
#include "runtime.h"
#include "scctimer.h"

struct timer {
	clock_t	real_time;
	struct tms times;
	int running;
};

mytimer_t SCCcreateTimer(){
	mytimer_t timer;
	timer=(mytimer_t)malloc(sizeof(struct timer));
	if (timer){
		timer->real_time=0;
		timer->times.tms_utime=0;
		timer->times.tms_stime=0;
		timer->times.tms_cutime=0;
		timer->times.tms_cstime=0;
		timer->running=0;
	}
	return timer;
}

void SCCdeleteTimer(mytimer_t timer){
	free(timer);
}

void SCCresetTimer(mytimer_t timer){
	if (timer->running) {
		timer->real_time=times(&(timer->times));
	} else {
		timer->real_time=0;
		timer->times.tms_utime=0;
		timer->times.tms_stime=0;
		timer->times.tms_cutime=0;
		timer->times.tms_cstime=0;
	}
}

void SCCstartTimer(mytimer_t timer){
	if (!(timer->running)) {
		struct tms tmp;
		clock_t real_time;
		real_time=times(&tmp);
		timer->real_time-=real_time;
		timer->times.tms_utime-=tmp.tms_utime;
		timer->times.tms_stime-=tmp.tms_stime;
		timer->times.tms_cutime-=tmp.tms_cutime;
		timer->times.tms_cstime-=tmp.tms_cstime;
		timer->running=1;
	}
}

void SCCstopTimer(mytimer_t timer){
	if (timer->running) {
		struct tms tmp;
		clock_t real_time;
		real_time=times(&tmp);
		timer->real_time+=real_time;
		timer->times.tms_utime+=tmp.tms_utime;
		timer->times.tms_stime+=tmp.tms_stime;
		timer->times.tms_cutime+=tmp.tms_cutime;
		timer->times.tms_cstime+=tmp.tms_cstime;
		timer->running=0;
	}
}


void SCCreportTimer(mytimer_t timer,char *msg){
	clock_t tick=sysconf(_SC_CLK_TCK);
	float tm_real=((float)(timer->real_time))/((float)tick);
	float tm_user=((float)(timer->times.tms_utime))/((float)tick);
	float tm_sys=((float)(timer->times.tms_stime))/((float)tick);
	Warning(info,"%s %5.3f real %5.3f user %5.3f sys",msg,tm_real,tm_user,tm_sys);
}

