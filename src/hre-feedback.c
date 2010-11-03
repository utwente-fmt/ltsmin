#include <config.h>
#include <hre-main.h>
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include "unix.h"
#include <libgen.h>
#include <pthread.h>
#include <hre-internal.h>
#include <ctype.h>
#include <string.h>

#define LOG_IGNORE 0x00
#define LOG_PRINT 0x01
#define LOG_WHERE 0x02

struct runtime_log {
    FILE*f;
    char *tag;
    int flags;
};

static int HREwhen=0;

struct runtime_log stats_log={NULL,NULL,LOG_IGNORE};
log_t stats=&stats_log;
struct runtime_log error_log={NULL,"** error **",LOG_PRINT};
log_t error=&error_log;
struct runtime_log info_log={NULL,NULL,LOG_PRINT};
log_t info=&info_log;
struct runtime_log infoShort_log={NULL,NULL,LOG_PRINT};
log_t infoShort=&infoShort_log;
struct runtime_log infoLong_log={NULL,NULL,LOG_IGNORE};
log_t infoLong=&infoLong_log;
struct runtime_log hre_debug_log={NULL,NULL,LOG_IGNORE};
log_t hre_debug=&hre_debug_log;

static const char stats_long[]="stats";
static const char debug_long[]="debug";
static const char where_long[]="where";
static const char when_long[]="when";
#define incr_short 'v'
#define quiet_short 'q'

static struct sigaction segv_sa;
static void segv_handle(int signum){
    (void)signum;
    fprintf(stderr,
            "*** segmentation fault ***\n\n"
            "Please send information on how to reproduce this problem to: \n"
            "         " PACKAGE_BUGREPORT "\n"
            "along with all output preceding this message.\n"
            "In addition, include the following information:\n"
            "Package: " PACKAGE_STRING "\n"
            "Stack trace:\n");
#if defined(HAVE_BACKTRACE) && defined(HAVE_BACKTRACE_SYMBOLS)
    void*stacktrace[64];
    int size=backtrace(stacktrace,64);
    char **stackinfo=backtrace_symbols(stacktrace,size);
    for(int i=0;i<size;i++){
        fprintf(stderr," %2d: %s\n",i,stackinfo[i]);
    }
#else
    fprintf (stderr, "not available.\n");
#endif
    _exit (EXIT_FAILURE);
}
static void segv_setup(){
    memset(&segv_sa, 0, sizeof(segv_sa));
    segv_sa.sa_handler = segv_handle;
    sigemptyset(&segv_sa.sa_mask);
    sigaction(SIGSEGV, &segv_sa, NULL);
}

static inline int is_number(const char*str){
    for(int i=0;str[i]!=0;i++){
        if (!isdigit(str[i])) return 0;
    }
    return 1;
}

#define IF_LONG(long) if(((opt->longName)&&!strcmp(opt->longName,long)))

void HREinitFeedback(){
    segv_setup();
}

void popt_callback(
    poptContext con,
    enum poptCallbackReason reason,
    const struct poptOption * opt,
    const char * arg, void * data
){
    (void)con;(void)data;(void)arg;
    switch(reason){
        case POPT_CALLBACK_REASON_PRE:
            Abort("should not be called during pre processing");
        case POPT_CALLBACK_REASON_OPTION:
            IF_LONG(debug_long){
                hre_debug->flags|=LOG_PRINT;
                return;
            }
            IF_LONG(stats_long){
                stats->flags|=LOG_PRINT;
                return;
            }
            IF_LONG(where_long) {
                hre_debug->flags|=LOG_WHERE;
                return;
            }
            IF_LONG(when_long) {
                HREwhen=1;
                return;
            }
            if (opt->shortName == incr_short){
                infoLong->flags|=LOG_PRINT;
                return;
            }
            if (opt->shortName == quiet_short){
                infoShort->flags&=~LOG_PRINT;
                info->flags&=~LOG_PRINT;
                return;
            }
            Abort("bad HRE feedback option %s (%c)",opt->longName,opt->shortName);
            return;
        case POPT_CALLBACK_REASON_POST:
            Abort("should not be called during post processing");
    }
}

struct poptOption hre_feedback_options[]={
    { NULL, 0 , POPT_ARG_CALLBACK ,
    popt_callback , 0 , NULL , NULL},
    { NULL, incr_short ,  POPT_ARG_NONE , NULL , 481 ,
    "increase verbosity" , NULL },
    { NULL, quiet_short , POPT_ARG_NONE , NULL , 481 ,
    "set verbosity to 0" , NULL },
    { debug_long , 0 , POPT_ARG_NONE , NULL, 481 ,
    "enable debugging feedback" , "level" },
    { stats_long , 0 , POPT_ARG_NONE , NULL, 481,
    "enable statistics gathering/printing" , "level" },
#ifdef LTSMIN_DEBUG
    { where_long , 0 , POPT_ARG_NONE , NULL , 481 ,
    "include file and line number in debug messages", NULL},
#endif
    { when_long , 0 , POPT_ARG_NONE , NULL , 481 ,
    "include the wall time since program start in all messages" , NULL },
    POPT_TABLEEND
};


FILE* log_get_stream(log_t log){
    if (log && log->flags & LOG_PRINT) {
        if (log->f) return log->f; else return stderr; 
    } else {
        return NULL;
    }
}

static void log_begin(log_t log,FILE*f,int line,const char*file){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    fprintf(f,"%s",ctx->label);
    if (HREwhen){
        struct timeval tv;
        if (gettimeofday(&tv,NULL)){
            AbortCall("gettimeofday");
        }
        tv.tv_usec-=ctx->init_tv.tv_usec;
        if(tv.tv_usec<0) { tv.tv_usec+=1000000;tv.tv_sec--; }
        tv.tv_sec-=ctx->init_tv.tv_sec;
        fprintf(f,", %d.%03d",(int)tv.tv_sec,(int)(tv.tv_usec/1000));
    }
    if (log->flags & LOG_WHERE) fprintf(f,", file %s, line %d",file,line);
    if (log->tag) fprintf(f,", %s",log->tag);
    fprintf(f,": ");
}

int log_active(log_t log){
    return log && (log->flags & LOG_PRINT);
}

void log_printf(log_t log,const char *fmt,...){
    if (log && log->flags & LOG_PRINT){
        va_list args;
        va_start(args,fmt);
        vfprintf(log->f?log->f:stderr,fmt,args);
        va_end(args);
    }
}

void log_message(log_t log,const char*file,int line,int errnum,const char *fmt,...){
    if (log && (log->flags & LOG_PRINT)==0) return;
    FILE*f=log->f?log->f:stderr;
    log_begin(log,f,line,file);
    va_list args;
    va_start(args,fmt);
    vfprintf(f,fmt,args);
    va_end(args);
    if (errnum) {
        char errmsg[256];
#ifdef STRERROR_R_CHAR_P
        char*err_msg=strerror_r(errnum,errmsg,256);
#else
        char*err_msg=strerror_r(errnum,errmsg,256)?NULL:errmsg;
#endif
        if(!err_msg){
            err_msg=errmsg;
            switch(errno){
                case EINVAL:
                    sprintf(errmsg,"%d is not an error",errnum);
                    break;
                case ERANGE:
                    sprintf(errmsg,"preallocated errmsg too short");
                    break;
                default:
                    sprintf(errmsg,"this statement should have been unreachable");
            }
        }
        fprintf(f,": %s", err_msg);
    }
    fprintf(f,"\n");
}
