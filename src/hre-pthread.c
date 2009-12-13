#include <config.h>
#include <hre-main.h>
#include <hre-internal.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

static int global_argc=0;
static char **global_argv=NULL;
static char *global_program;

extern int main(int,char**);

static void* thread_main(void*arg){
    struct thread_context *ctx=arg;
    pthread_setspecific(hre_key,ctx);
    set_label("%s(%2d/%2d)",global_program,
              HREme(ctx->global),HREpeers(ctx->global));
              main(global_argc,global_argv);
              return NULL;
}

static void hre_pthread_exit(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void hre_pthread_exit(hre_context_t ctx,int code){
    (void)ctx;(void)code;
    pthread_exit(NULL);
}

void HREpthreadRun(int argc,char*argv[],int threads){
    pthread_t thr[threads];
    global_argc=argc;
    global_argv=argv;
    global_program=strdup(get_label());
    stream_t* comm_matrix[threads];
    for(int i=0;i<threads;i++){
        comm_matrix[i]=HREmalloc(hre_heap,threads*sizeof(stream_t));
    }
    for(int i=0;i<threads;i++){
        for(int j=0;j<threads;j++){
            if (i<=j) {
                comm_matrix[i][j]=NULL;
                continue;
            }
            int pipe_a[2];
            if (pipe(pipe_a)) AbortCall("pipe");
            int pipe_b[2];
            if (pipe(pipe_b)) AbortCall("pipe");
            comm_matrix[i][j]=fd_stream_pair(pipe_a[0],pipe_b[1]);
            comm_matrix[j][i]=fd_stream_pair(pipe_b[0],pipe_a[1]);
        }
    }
    for(int i=0;i<threads;i++){
        struct thread_context*thr_ctx=HRE_NEW(hre_heap,struct thread_context);
        thr_ctx->global=HREctxCreate(i,threads,0);
        HREsetExit(thr_ctx->global,hre_pthread_exit);
        HREsetLinks(thr_ctx->global,comm_matrix[i]);
        HREctxComplete(thr_ctx->global);
        if (pthread_create(thr+i,NULL,thread_main,thr_ctx)) {
            Abort("couldn't create thread %d",i);
        }
    }
    for(int i=0;i<threads;i++){
        if (pthread_join(thr[i],NULL)) {
            Abort("couldn't join with thread %d",i);
        }
    }
    exit(EXIT_SUCCESS);
}
