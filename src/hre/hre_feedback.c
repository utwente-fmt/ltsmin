// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <ctype.h>
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <hre/internal.h>
#include <hre/provider.h>
#include <hre/unix.h>
#include <hre/stringindex.h>

#define LOG_IGNORE 0x00
#define LOG_PRINT 0x01
#define LOG_WHERE 0x02

struct runtime_log {
    FILE*f;
    char *tag;
    int flags;
    string_index_t index;
};

static int HREwhen=0;

struct runtime_log stats_log={NULL,NULL,LOG_IGNORE,NULL};
log_t stats=&stats_log;
struct runtime_log error_log={NULL,"** error **",LOG_PRINT,NULL};
log_t lerror=&error_log;
struct runtime_log assert_log={NULL,NULL,LOG_PRINT|LOG_WHERE,NULL};
log_t assertion=&assert_log;
struct runtime_log info_log={NULL,NULL,LOG_PRINT,NULL};
log_t info=&info_log;
struct runtime_log infoShort_log={NULL,NULL,LOG_PRINT,NULL};
log_t infoShort=&infoShort_log;
struct runtime_log infoLong_log={NULL,NULL,LOG_IGNORE,NULL};
log_t infoLong=&infoLong_log;
struct runtime_log hre_debug_log={NULL,NULL,LOG_IGNORE,NULL};
log_t hre_debug=&hre_debug_log;

static const char stats_long[]="stats";
static const char debug_long[]="debug";
static const char where_long[]="where";
static const char when_long[]="when";
static const char timeout_long[]="timeout";
static int timeout = -1;
#define incr_short 'v'
#define quiet_short 'q'

static struct sigaction segv_sa;

void
HREprintStack ()
{
#if defined(HAVE_BACKTRACE) && defined(HAVE_BACKTRACE_SYMBOLS)
    void* stacktrace[64];
    int size = backtrace (stacktrace, 64);
    char** stackinfo = backtrace_symbols (stacktrace, size);
    for (int i = 0; i < size; i++) {
        fprintf (stderr, " %2d: %s\n", i, stackinfo[i]);
    }
#else
    fprintf (stderr, "not available.\n");
#endif
}

static void segv_handle (int signum)
{
    (void)signum;
    fprintf(stderr,
            "*** segmentation fault ***\n\n"
            "Please send information on how to reproduce this problem to: \n"
            "         " PACKAGE_BUGREPORT "\n"
            "along with all output preceding this message.\n"
            "In addition, include the following information:\n"
            "Package: " PACKAGE_STRING "\n"
            "Stack trace:\n");
    HREprintStack ();
    _exit (HRE_EXIT_FAILURE);
}
void timeout_handler (int signum) {
  struct sigaction sao;
  memset(&sao, 0, sizeof(sao));
  sigaction(SIGINT, NULL, &sao);
  if (timeout == -1 || sao.sa_handler == NULL) {
      Warning (info, " ");
      Warning (info, " ");
      Warning (info, "TIMED OUT (ungraceful exit)!\n");
      HREexit (HRE_EXIT_TIMEOUT); // ungracefuly
  } else {
      killpg (0, SIGINT); // try grace fully
      Warning (info, " ");
      Warning (info, " ");
      Warning (info, "TIMED OUT (%d seconds)!", timeout);
      Warning (info, " ");
      Warning (info, " ");
      struct sigaction sa;
      memset(&sa, 0, sizeof(sa));
      sa.sa_handler = timeout_handler;
      sigaction(SIGALRM, &sa, NULL);
      alarm(1);
      timeout = -1;
  }
  (void) signum;
}
static void segv_setup(){
    memset(&segv_sa, 0, sizeof(segv_sa));
    segv_sa.sa_handler = segv_handle;
    sigemptyset(&segv_sa.sa_mask);
    sigaction(SIGSEGV, &segv_sa, NULL);
}

#define IF_LONG(long) if(((opt->longName)&&!strcmp(opt->longName,long)))

void HREinitFeedback(){
    if (hre_debug->index==NULL) hre_debug->index=SIcreate();
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
                SIput(hre_debug->index,arg);
                Print(infoShort,"printing debug info for %s",arg);
                return;
            }
            IF_LONG(stats_long){
                stats->flags|=LOG_PRINT;
                return;
            }
            IF_LONG(where_long) {
                hre_debug->flags|=LOG_WHERE;
                lerror->flags|=LOG_WHERE;
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
            IF_LONG(timeout_long) {
                Print (infoLong,"setting timeout to %d seconds", timeout);
                struct sigaction sa;
                memset(&sa, 0, sizeof(sa));
                sa.sa_handler = timeout_handler;
                sigaction(SIGALRM, &sa, NULL);
                alarm(timeout);
                return;
            }
            Abort("bad HRE feedback option %s (%c)",opt->longName,opt->shortName);
            return;
        case POPT_CALLBACK_REASON_POST:
            Abort("should not be called during post processing");
    }
}

struct poptOption hre_feedback_options[]={
    { NULL, 0 , POPT_ARG_CALLBACK , popt_callback , 0 , NULL , NULL},
    { NULL, incr_short ,  POPT_ARG_NONE , NULL , 0 ,
    "increase verbosity" , NULL },
    { NULL, quiet_short , POPT_ARG_NONE , NULL , 0 ,
    "set verbosity to 0" , NULL },
    { debug_long , 0 , POPT_ARG_STRING , NULL, 0 ,
    "enable debugging feedback for one file" , "<source file>" },
    { stats_long , 0 , POPT_ARG_NONE , NULL, 0 ,
    "enable statistics gathering/printing" , "level" },
    { where_long , 0 , POPT_ARG_NONE , NULL , 0 ,
    "include file and line number in debug messages", NULL},
    { when_long , 0 , POPT_ARG_NONE , NULL , 0 ,
    "include the wall time since program start in all messages" , NULL },
    { timeout_long , 0 , POPT_ARG_INT , &timeout , 0 ,
    "terminate after the given amount of seconds" , NULL },
    POPT_TABLEEND
};


FILE* log_get_stream(log_t log){
    if (log && (log->flags & LOG_PRINT)) {
        if (log->f) return log->f; else return stderr;
    } else {
        return NULL;
    }
}

static inline int
is_active(log_t log,const char*file)
{
    if (log == NULL || (log->flags & LOG_PRINT)==0) return false;
    if (file != NULL && log->index){
        const char *tmp=file;
        for(;;){
            int idx=SIlookup(log->index,tmp);
            if (idx!=SI_INDEX_FAILED) break; // found match.
            tmp=strstr(tmp,"/");
            if (!tmp) return false; // not found.
            tmp=tmp+1; // try again with shorter prefix.
        }
    }
    return true;
}

int log_active(log_t log) {
    return is_active(log, NULL);
}

void log_printf(log_t log,const char*file,const char *fmt,...){
    if (is_active(log,file)) {
        va_list args;
        va_start(args,fmt);
        vfprintf(log->f?log->f:stderr,fmt,args);
        va_end(args);
    }
}

void log_message(log_t log,const char*file,int line,int errnum,const char *fmt,...){
    if (!is_active(log,file)) return;
    FILE*f=log->f?log->f:stderr;
    struct thread_context *ctx=pthread_getspecific(hre_key);
    // If needed put the time in the string when.
    char when[128];
    if (HREwhen && ctx){
        struct timeval tv;
        if (gettimeofday(&tv,NULL)){
            HREwhen=0;
            AbortCall("gettimeofday");
        }
        tv.tv_usec-=ctx->init_tv.tv_usec;
        if(tv.tv_usec<0) { tv.tv_usec+=1000000;tv.tv_sec--; }
        tv.tv_sec-=ctx->init_tv.tv_sec;
        snprintf(when,128,", %d.%03d",(int)tv.tv_sec,(int)(tv.tv_usec/1000));
    } else {
        when[0]=0;
    }
    // If needed put file and line number in where.
    char where[128];
    if (log->flags & LOG_WHERE) {
        snprintf(where,128,", file %s, line %d",file,line);
    } else {
        where[0]=0;
    }
    // If needed put the tag in tag.
    char tag[128];
    if (log->tag) {
        snprintf(tag,128,", %s",log->tag);
    } else {
        tag[0]=0;
    }
    // Put the main message in main_msg.
    char main_msg[4096];
    va_list args;
    va_start(args,fmt);
    vsnprintf(main_msg,4096,fmt,args);
    va_end(args);
    // If needed put the error message in errmsg.
    char errmsg[256];
    char*err_msg;
    if (errnum) {
        errmsg[0]=':';
        errmsg[1]=' ';
        errmsg[2]=0;
#ifdef STRERROR_R_CHAR_P
        err_msg=strerror_r(errnum,errmsg+2,126);
#else
        err_msg=strerror_r(errnum,errmsg+2,126)?NULL:errmsg;
#endif
        if(!err_msg){
            err_msg=errmsg;
            switch(errno){
                case EINVAL:
                    sprintf(errmsg+2,"%d is not an error",errnum);
                    break;
                case ERANGE:
                    sprintf(errmsg+2,"preallocated errmsg too short");
                    break;
                default:
                    sprintf(errmsg+2,"this statement should have been unreachable");
            }
        }
    } else {
        errmsg[0]=0;
        err_msg=errmsg;
    }
    char *label = ctx ? ctx->label : "HRE";
    // print the entire line in one statement to minimize interleaving in output.
    fprintf(f,"%s%s%s%s: %s%s\n",label,when,where,tag,main_msg,err_msg);
    if (log == debug && log_active(infoLong)) {
        HREprintStack();
    }
}

