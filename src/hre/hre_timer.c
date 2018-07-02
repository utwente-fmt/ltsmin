// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/times.h>
#include <unistd.h>

#include <hre/user.h>

#ifdef _WIN32
# include <windows.h>
#endif

struct timer {
    clock_t real_time;
    struct tms times;
    int running;
};

rt_timer_t RTcreateTimer(){
    rt_timer_t timer;
    timer=(rt_timer_t)RTmalloc(sizeof(struct timer));
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

void RTdeleteTimer(rt_timer_t timer){
    RTfree(timer);
}

void RTresetTimer(rt_timer_t timer){
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

void RTstartTimer(rt_timer_t timer){
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

void RTrestartTimer(rt_timer_t timer){
    RTresetTimer (timer);
    RTstartTimer (timer);
}

void RTstopTimer(rt_timer_t timer){
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

void RTprintTimer(log_t log,rt_timer_t timer,char *msg, ...){
#ifdef _WIN32
    size_t tick=CLK_TCK;
#else
    size_t tick=sysconf(_SC_CLK_TCK);
#endif
    float tm_real, tm_user, tm_sys;
    if (timer->running) {
        struct tms tmp;
        clock_t real_time;
        real_time=times(&tmp);
        tm_real=((float)(real_time+timer->real_time))/((float)tick);
        tm_user=((float)(tmp.tms_utime+timer->times.tms_utime))/((float)tick);
        tm_sys=((float)(tmp.tms_stime+timer->times.tms_stime))/((float)tick);
    } else {
        tm_real=((float)(timer->real_time))/((float)tick);
        tm_user=((float)(timer->times.tms_utime))/((float)tick);
        tm_sys=((float)(timer->times.tms_stime))/((float)tick);
    }

    va_list argptr;
    va_start(argptr, msg);
    char buf[255];
    vsnprintf(buf, 255, msg, argptr);
    va_end(argptr);
    Print(log,"%s %5.3f real %5.3f user %5.3f sys",buf,tm_real,tm_user,tm_sys);
}

float RTrealTime(rt_timer_t timer){
#ifdef _WIN32
    size_t tick=CLK_TCK;
#else
    size_t tick=sysconf(_SC_CLK_TCK);
#endif
    if (timer->running) {
        struct tms tmp;
        clock_t real_time;
        real_time=times(&tmp);
        return ((float)(real_time+timer->real_time))/((float)tick);
    } else {
        return ((float)(timer->real_time))/((float)tick);
    }
}

