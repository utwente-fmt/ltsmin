// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <pthread.h>
#include <unistd.h>

#ifdef TESTMPI
#include <hre-mpi/user.h>
#else
#include <hre/user.h>
#endif

static int do_exit=-1;
static int do_abort=-1;
static int do_segv=-1;
static int do_illegal=-1;
static int do_table=-1;
static int do_reduce=0;
static int do_forward=0;
static int do_shared=0;
static int do_args=0;

static  struct poptOption options[] = {
    { "illegal" , 0 , POPT_ARG_INT , &do_illegal , 0 , "try illegal exit" , "<worker>" },
    { "abort" , 0 , POPT_ARG_INT , &do_abort , 0 , "try abort" , "<worker>" },
    { "exit" , 0 , POPT_ARG_INT , &do_exit , 0 , "try early exit" , "<worker>" },
    { "segv" , 0 , POPT_ARG_INT , &do_segv , 0 , "try seg fault" , "<worker>" },
    { "table" , 0 , POPT_ARG_INT , &do_table , 0 , "set table value" , "<worker>" },
    { "reduce" , 0 , POPT_ARG_VAL , &do_reduce , 1 , "try reductions" , NULL },
    { "forward" , 0 , POPT_ARG_VAL , &do_forward , 1 , "try message exchange" , NULL },
    { "shared" , 0 , POPT_ARG_VAL , &do_shared , 1 , "try shared memory" , NULL },
    { "enum-args" , 0 , POPT_ARG_VAL , &do_args , 1 , "enumerate arguments" , NULL },
    POPT_TABLEEND
};

struct test {
    int x;
    int y;
};

static void got_it(void* context,hre_msg_t msg){
    Print(infoShort,"got message from %d, length %d, (%d %d)",msg->source,msg->tail,msg->buffer[0],msg->buffer[1]);
    *((int*)context)=0;
    HREmsgReady(msg);
};

static void forward_it(void* context,hre_msg_t msg){
    int me=HREme(HREglobal());
    int peers=HREpeers(HREglobal());
    Print(infoShort,"received message from %d, length %d, (%d)",msg->source,msg->tail,msg->buffer[0]);
    hre_msg_t message=HREnewMessage(HREglobal(),4096);
    message->source=me;
    message->target=(me+1)%peers;
    message->comm=1;
    message->tag=*((uint32_t*)context);
    message->head=0;
    message->buffer[0]=msg->buffer[0];
    message->buffer[1]=me;
    message->tail=2;
    Print(infoShort,"forwarding from %d to %d, tag %u",message->source,message->target,message->tag);
    HREpostSend(message);
    HREmsgReady(msg);
};

int main(int argc, char *argv[]){
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Tool for testing the Hybrid Runtime Environment\n\nOptions");
    HREenableAll();
    HREinitStart(&argc,&argv,0,-1,NULL,NULL);
    int me=HREme(HREglobal());
    int peers=HREpeers(HREglobal());
    Print(infoShort,"I am %d of %d",me,peers);

    Print(infoShort,"%d cores, %.1fGB memory, %dB cache line.",RTnumCPUs(),
        ((float)RTmemSize())/1073741824.0,RTcacheLineSize());

    Print(info,"info");
    Print(infoShort,"infoShort");
    Print(infoLong,"infoLong");
    Print(lerror,"error");
    Print(debug,"debug");

    sleep(1);

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
        Print(lerror,"%s",(char*)1);
    }
    if (do_abort==me){
        Abort("aborting...");
    }
    if (do_table>=0) {
        value_table_t vt=HREcreateTable(HREglobal(),"TEST");
        char message[24]="x";
        if (me==do_table){
            chunk c=chunk_ld(24,message);
            sprintf(message,"set at %d",me);
            Print(infoShort,"inserted \"%s\" at %d",message,VTputChunk(vt,c));
        }
        if (do_table==peers){
            chunk c=chunk_ld(24,message);
            sprintf(message,"set at %d",me);
            Print(infoShort,"inserted \"%s\"at %d",message,VTputChunk(vt,c));
        }
        HREbarrier(HREglobal());
        if (do_table==peers) {
            for(int i=0;i<peers;i++) {
                chunk c=VTgetChunk(vt,i);
                Print(infoShort,"label %d is \"%s\"",i,c.data);
            }
        } else {
            chunk c=VTgetChunk(vt,0);
            Print(infoShort,"label 0 is \"%s\"",c.data);
        }
        HREbarrier(HREglobal());
        Print(infoShort,"done");
        HREexit(0);
    }
    if(do_reduce){
        if (me==0) Print(infoShort,"reducing");
        uint32_t array[2];
        array[0]=2;
        array[1]=me;
        uint32_t res[2];
        sleep(1);
        Print(infoShort,"array is [%u,%u]",array[0],array[1]);
        HREreduce(HREglobal(),2,array,res,UInt32,Sum);
        Print(infoShort,"sum is [%u,%u]",res[0],res[1]);
        sleep(1);
        Print(infoShort,"array is [%u,%u]",array[0],array[1]);
        HREreduce(HREglobal(),2,array,res,UInt32,Max);
        Print(infoShort,"max is [%u,%u]",res[0],res[1]);
        Print(infoShort,"sequence test...");
        for(uint32_t i=0;i<100;i++){
            uint32_t tmp;
            HREreduce(HREglobal(),1,&i,&tmp,UInt32,Max);
            if (tmp!=i) Abort("out of order %u != %u",tmp,i);
        }
        Print(infoShort,"...pass");
        HREexit(0);
    }
    if (do_forward){
        Print(infoShort,"setting up forwarding test");
        int pending=1;
        uint32_t tag1,tag2;
        tag1=HREactionCreate(HREglobal(),2,4096,forward_it,&tag2);
        tag2=HREactionCreate(HREglobal(),1,4096,got_it,&pending);
        Print(infoShort,"synchronizing");
        HREbarrier(HREglobal());
        Print(infoShort,"message run starting tags %u and %u",tag1,tag2);
        HREbarrier(HREglobal());
        hre_msg_t message=HREnewMessage(HREglobal(),4096);
        message->source=me;
        message->target=(me+1)%peers;
        message->comm=2;
        message->head=0;
        message->buffer[0]=me;
        message->tail=1;
        message->tag=tag1;
        sleep(me%2);
        Print(infoShort,"sending from %d to %d",message->source,message->target);
        HREpostSend(message);
        Print(infoShort,"now we wait");
        HREyieldWhile(HREglobal(),&pending);
        Print(infoShort,"got our message");
        HREbarrier(HREglobal());
        Print(infoShort,"action based run complete");
        HREbarrier(HREglobal());
        hre_buffer_t buf=HREbufferCreate(HREglobal(),1,4096);
        Print(infoShort,"buffer created");
        message->comm=1;
        message->tag=HREbufferTag(buf);
        Print(infoShort,"buffer tag is %d",message->tag);
        Print(infoShort,"sending from %d to %d",message->source,message->target);
        HREpostSend(message);
        Print(infoShort,"now we wait");
        hre_msg_t msg=HRErecv(buf,(me+peers-1)%peers);
        Print(infoShort,"got message %d -> %d [%d]",msg->source,msg->target,msg->buffer[0]);
        HREbarrier(HREglobal());
        Print(infoShort,"message run complete");
    }
    if (do_shared){
        if (me==0) Print(infoShort,"trying to get 1GB shared memory");
        volatile int* shm=HREshmGet(HREglobal(),1073741824);
        HREbarrier(HREglobal());
        if (shm==NULL) Abort("no shared memory");
        if (me==0) Print(infoShort,"got it");
        if (peers==1) {
            Print(infoShort,"cannot test with only one peer");
            HREexit(0);
        }
        if (me==0) {
            shm[0]=0xc0ffee;
            for(int i=0;i<100000;i++){
                fprintf(stderr,"%06X %08x %08x %08x\r",shm[0],shm[1],shm[2],shm[3]);
            }
            fprintf(stderr,"\n");
            Abort("enough");
        } else {
            shm[me]=0;
            for(;;) {
                shm[me]++;
            }
        }
        HREexit(0);
    }
    if (do_args){
        HREbarrier(HREglobal());
        Print(info,"enumerating arguments");
        char*arg;
        while((arg=HREnextArg())){
            Print(infoShort,"got argument %s",arg);
        }
        Print(info,"enumeration done");
        HREbarrier(HREglobal());
    }
    sleep(2);
    Print(infoShort,"test run complete");
    HREexit(0);
}

