#include <config.h>
#include <hre-main.h>
#include <hre-internal.h>

int hre_init_mpi=-1;
char* hre_mpirun=NULL;
int hre_force_mpi=0;


struct poptOption hre_mpi_options[]={
    POPT_TABLEEND
};

void HREinitMPI(int*argc_p,char**argv_p[]){
    (void)argc_p;(void)argv_p;
    Abort("HREinitMPI: this binary has no MPI support");
}
void HREmpirun(int argc,char*argv[],const char *args){
    (void)argc;(void)argv;(void)args;
    Abort("HREmpirun: this binary has no MPI support");
}
