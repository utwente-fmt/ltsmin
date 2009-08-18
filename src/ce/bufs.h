
#include <assert.h>
#include <mpi.h>

#define NEWSIGS_MSG 1
#define NEWSIGS_END 2
#define NEWIDS_MSG 3
#define NEWIDS_END 4
#define UPDATE_MSG 5
#define UPDATE_END 6
#define BASE_MSG 7
#define STABLE_MSG 8
#define UNSTABLE_MSG 9
#define IDCOUNTS_MSG 12
#define DUMP_MSG 10
#define DUMP_END 11

#define MSGSIZE 15080  // must be bigger than the max. signature size
#define BUFSIZE 100   // how much to alloc at once - for the state buffers

typedef struct intbuf{
  int* b;
  int index;    
  int size;     // in number of elements (ints)
} *intbuf_t;      // (local) single buffer of ints (state or id..) ; 
                // used by changed (w buffers) and maybechanged (1)
                // and by idbufs

extern int flag;
extern int send_pending;
extern int bufnewids_pending;
extern MPI_Request send_request;

extern void printBuffer(intbuf_t , char* );
extern void checkSend(intbuf_t, int, MPI_Comm, int, int); 
// bufferset, dest-worker, dest-communicator, msgtag, what-to-check-size

extern void rcvBuf(intbuf_t*, int*, MPI_Comm, int*, int*);
// bufferset, src-worker, src-communicator, src-msgtag, what-came-size

extern int IcheckSend(intbuf_t, int,  MPI_Comm, int); 
// bufferset, dest-worker, dest-communicator, msgtag, what-to-check-size

extern int IrcvBuf(intbuf_t*, intbuf_t, int*, MPI_Comm, int*, int*);
// bufferset, src-worker, src-msgtag, what-came-size

extern void Send(int, MPI_Comm, int, int);
// dest-worker, msgtag, content (one int)

extern void Receive(int*, MPI_Comm, int, int*);
// any src-worker, msgtag, any content (one int)


extern intbuf_t newBuffer(int);
extern void resetBuffer(intbuf_t);
extern void freeBuffer(intbuf_t);
extern void add(intbuf_t, int*, int);
extern void add1(intbuf_t, int);
extern void add4(intbuf_t, int,int,int,int);
extern void sendBuffer(intbuf_t, int, MPI_Comm, int); 
// sends MSGSIZE/sizeof(int) ints at a time






