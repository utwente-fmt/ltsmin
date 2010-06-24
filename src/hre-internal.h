#ifndef HRE_INTERNAL_H
#define HRE_INTERNAL_H

#include <popt.h>
#include <stream.h>
#include <pthread.h>
#include <sys/time.h>

/*
Internal details of HRE.
Should not be installed!
Should be used by HRE modules only!
*/


#define MAX_OPTION_GROUPS 31

extern int hre_bare;

extern pthread_key_t hre_key;
struct thread_context {
    hre_context_t global;
    struct timeval init_tv;
    char label[256];
    int next_group;
    struct poptOption option_group[MAX_OPTION_GROUPS+1];
};

extern struct poptOption hre_feedback_options[];
extern struct poptOption hre_boot_options[];

extern void HREinitPopt();
extern void HREinitFeedback();

extern int hre_init_mpi;
extern char* hre_mpirun;
extern int hre_force_mpi;

extern struct poptOption hre_mpi_options[];
extern void HREinitMPI(int*argc_p,char**argv_p[]);
extern void HREmpirun(int argc,char*argv[],const char *args);

extern hre_context_t HREctxLinks(int me,int peers,stream_t *links);

extern void HREpthreadRun(int argc,char*argv[],int threads);

#endif
