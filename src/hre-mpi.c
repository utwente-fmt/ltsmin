#include <config.h>
#include <hre-main.h>
#include <hre-internal.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mpi.h>

void HRErequireMPI(){
    hre_force_mpi=1;
}

int hre_init_mpi=0;
char* hre_mpirun=NULL;
int hre_force_mpi=0;

struct poptOption hre_mpi_options[]={
    { "mpi" , 0 , POPT_ARG_VAL , &hre_init_mpi , 1 ,
    "Enable MPI during execution",NULL},
    { "mpirun" , 0 , POPT_ARG_STRING , &hre_mpirun , 0 ,
    "execute mpirun, this option is ignored if --mpi is present" , "<mpirun argument>" },
    POPT_TABLEEND
};

struct hre_context_s {
    MPI_Comm comm;
};

static int mpi_check_any(hre_context_t ctx,int arg){
    if(arg) arg=1;
    int res;
    MPI_Allreduce(&arg,&res,1,MPI_INT,MPI_MAX,ctx->comm);
    return res;
}

static void mpi_abort(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void mpi_abort(hre_context_t ctx,int code){
    MPI_Abort(ctx->comm,code);
    exit(EXIT_FAILURE);
}

static void mpi_exit(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void mpi_exit(hre_context_t ctx,int code){
    (void)ctx;
    MPI_Finalize();
    if (code) {
        exit(EXIT_FAILURE);
    } else {
        exit(EXIT_SUCCESS);
    }
}

void HREmpirun(int argc,char*argv[],const char *mpi_args){
    int mpi_argc=0;
    char**mpi_argv=NULL;
    if (strlen(mpi_args)){
        int res=poptParseArgvString(mpi_args,&mpi_argc,(const char***)&mpi_argv);
        if (res){
            Abort("could not parse %s: %s",mpi_args,poptStrerror(res));
        }
    }
    char*newargv[argc+3+mpi_argc];
    newargv[0]="mpirun";
    for(int i=0;i<mpi_argc;i++) newargv[i+1]=mpi_argv[i];
    for(int i=0;i<argc;i++) newargv[i+1+mpi_argc]=argv[i];
    newargv[argc+1+mpi_argc]="--mpi";
    newargv[argc+2+mpi_argc]=NULL;
    execvp("mpirun",newargv);
    AbortCall("execvp");
}

static hre_context_t HREctxMPI(MPI_Comm comm){
    int me,peers;
    MPI_Comm_size(comm, &peers);
    MPI_Comm_rank(comm, &me);
    set_label("%s(%2d/%2d)",strdup(get_label()),me,peers);
    hre_context_t ctx=HREctxCreate(me,peers,sizeof(struct hre_context_s));
    ctx->comm=comm;
    HREsetCheckAny(ctx,mpi_check_any);
    HREsetAbort(ctx,mpi_abort);
    HREsetExit(ctx,mpi_exit);
    HREctxComplete(ctx);
    return ctx;
}

void HREinitMPI(int*argc_p,char**argv_p[]){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    MPI_Init(argc_p,argv_p);
    MPI_Errhandler_set(MPI_COMM_WORLD,MPI_ERRORS_ARE_FATAL);
    ctx->global=HREctxMPI(MPI_COMM_WORLD);
}
