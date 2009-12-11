#include <config.h>
#include <hre-main.h>
#include <hre-internal.h>

/* Type definitions */

struct hre_context_s{
    int me;
    int peers;
    stream_t *links;
    hre_destroy_m destroy;
    hre_check_any_m check_any;
    hre_abort_m abort;
    hre_exit_m exit;
};

static const size_t system_size=((sizeof(struct hre_context_s)+7)/8)*8;

#define SYS2USR(var) ((hre_context_t)(((void*)(var))+system_size))
#define USR2SYS(var) ((hre_context_t)(((void*)(var))-system_size))

/* generic link methods */

static stream_t dummy_links[1]={NULL};

hre_context_t HREctxLinks(int me,int peers,stream_t *links){
    hre_context_t ctx=HREctxCreate(me,peers,0);
    HREsetLinks(ctx,(peers==1)?dummy_links:links);
    HREctxComplete(ctx);
    return ctx;
}

typedef enum {CheckAny=1} msg_t;

static int check_any_links(hre_context_t ctx,int arg){
    ctx=USR2SYS(ctx);
    if (ctx->peers==1) return arg;
    //Info(1,"writing %d",arg);
    for(int i=0;i<ctx->peers;i++){
        if (i==ctx->me) continue;
        DSwriteU8(ctx->links[i],CheckAny);
        DSwriteS8(ctx->links[i],arg);
        //DSflush(ctx->links[i]);
    }
    //Info(1,"reading");
    for(int i=0;i<ctx->peers;i++){
        if (i==ctx->me) continue;
        uint8_t hdr=DSreadU8(ctx->links[i]);
        if (hdr!=CheckAny) Abort("unexpected header %d",(int)hdr);
        int8_t msg=DSreadS8(ctx->links[i]);
        //Info(1,"got %d",(int)msg);
        arg=arg||msg;
    }
    //Info(1,"done %d",arg);
    return arg;
}

static void nop_destroy(hre_context_t ctx){
    (void)ctx;
}

/* abstract class methods */

void HREsetLinks(hre_context_t ctx,stream_t* links){
    ctx=USR2SYS(ctx);
    ctx->links=links;
}

hre_context_t HREctxCreate(int me,int peers,size_t user_size){
    hre_context_t ctx=(hre_context_t)HREmallocZero(hre_heap,system_size+user_size);
    ctx->me=me;
    ctx->peers=peers;
    return SYS2USR(ctx);
}
static void process_abort(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void process_abort(hre_context_t ctx,int code){
    (void)ctx;(void)code;
    exit(EXIT_FAILURE);
}
static void process_exit(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void process_exit(hre_context_t ctx,int code){
    (void)ctx;
    if (code) {
        exit(EXIT_FAILURE);
    } else {
        exit(EXIT_SUCCESS);
    }
}

void HREctxComplete(hre_context_t ctx){
    ctx=USR2SYS(ctx);
    if(ctx->destroy==NULL) {
        ctx->destroy=nop_destroy;
    }
    if(ctx->check_any==NULL) {
        if (ctx->links){
            ctx->check_any=check_any_links;
        } else {
            Abort("The check any method is mandatory!");
        }
    }
    if(ctx->abort==NULL) ctx->abort=process_abort;
    if(ctx->exit==NULL) ctx->exit=process_exit;
}

void HREsetDestroy(hre_context_t ctx,hre_destroy_m method){
    USR2SYS(ctx)->destroy=method;
}

void HREctxDestroy(hre_context_t ctx){
    USR2SYS(ctx)->destroy(ctx);
    if (ctx->links && ctx->links != dummy_links){
        for(int i=0;i<ctx->peers;i++){
            if (ctx->links[i]) stream_close(ctx->links+i);
        }
    }
    HREfreeGuess(hre_heap,USR2SYS(ctx));
}

int HREme(hre_context_t ctx){
    return USR2SYS(ctx)->me;
}

int HREpeers(hre_context_t ctx){
    return USR2SYS(ctx)->peers;
}

int HREleader(hre_context_t ctx){
    return USR2SYS(ctx)->me == 0;
}

void HREsetCheckAny(hre_context_t ctx,hre_check_any_m method){
    USR2SYS(ctx)->check_any=method;
}

int HREcheckAny(hre_context_t ctx,int arg){
    return USR2SYS(ctx)->check_any(ctx,arg);
}

void HREsetAbort(hre_context_t ctx,hre_abort_m method){
    USR2SYS(ctx)->abort=method;
}

void HREctxAbort(hre_context_t ctx,int code){
    USR2SYS(ctx)->abort(ctx,code);
}

void HREsetExit(hre_context_t ctx,hre_exit_m method){
    USR2SYS(ctx)->exit=method;
}

void HREctxExit(hre_context_t ctx,int code){
    USR2SYS(ctx)->exit(ctx,code);
}
