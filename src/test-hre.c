#include <hre-main.h>
#include <pthread.h>
#include <unistd.h>

static int do_exit=-1;
static int do_abort=-1;
static int do_segv=-1;
static int do_illegal=-1;

static  struct poptOption options[] = {
    { "illegal" , 0 , POPT_ARG_INT , &do_illegal , 0 , "try illegal exit" , "<worker>" },
    { "abort" , 0 , POPT_ARG_INT , &do_abort , 0 , "try abort" , "<worker>" },
    { "exit" , 0 , POPT_ARG_INT , &do_exit , 0 , "try early exit" , "<worker>" },
    { "segv" , 0 , POPT_ARG_INT , &do_segv , 0 , "try seg fault" , "<worker>" },
    POPT_TABLEEND
};

struct test {
    int x;
    int y;
};

int main(int argc, char *argv[]){
    HREinit(&argc,&argv);
    HREaddOptions(options,"Tool for testing the Hybrid Runtime Environment\n\nOptions");
    HREparseOptions(argc,argv,0,0,NULL,"[test]");
    int me=HREme(HREglobal());
    int peers=HREpeers(HREglobal());
    Print(infoShort,"I am %d of %d",me,peers);
    
    Print(info,"info");
    Print(infoShort,"infoShort");
    Print(infoLong,"infoLong");
    Print(error,"error");
    Print(debug,"debug");
    
    struct test*xx=HRE_NEW(hre_heap,struct test);
    HRE_FREE(hre_heap,xx);
    sleep(2);
    if (do_exit==me) {
        Print(infoShort,"early exit");
        HREexit(0);
    }
    if (do_illegal==me){
        Print(infoShort,"illegal exit");
        exit(0);
    }
    if (do_segv==me){
        Print(infoShort,"causing a seg fault");
        Print(error,"%s",(void*)1);
    }
    if (do_abort==me){
        Abort("aborting...");
    }
    sleep(2);
    Print(infoShort,"test run complete");
    HREexit(0);
}
