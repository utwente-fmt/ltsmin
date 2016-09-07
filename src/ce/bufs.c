#include <hre/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "bufs.h"

#include <hre/runtime.h>

//#define DEBUG


//_______________________________________________
//
//

extern int mpi_me;
char* tname[13]={"","NEWSIGS_MSG","NEWSIGS_END","NEWIDS_MSG",
			"NEWIDS_END","UPDATE_MSG","UPDATE_END","BASE_MSG",
			"STABLE_MSG","UNSTABLE_MSG","DUMP_MSG","DUMP_END",
			"IDCOUNTS_MSG"};

//_________________________________________
// 
// checkSend   b  w  tag size    
//   checks if the _b_ [  _w_ ] has enough space for another _size_ bytes
//   If not, it sends the buffer (with tag _tag_) i.o.t. make space

void checkSend(intbuf_t buf, int w, MPI_Comm comm, int tag, int size){ 

  //  Warning(info,"checkSend %d %d %d",w,tag,size);
  assert(size <= MSGSIZE);
  if (buf->index + size > MSGSIZE){
#ifdef DEBUG
    fprintf(stderr,"\n%d SEND %s (size %d) to %d ", 
	    mpi_me, tname[tag], buf->index, w);
#endif
    assert (buf->index <= MSGSIZE);
    
    if ((tag==NEWIDS_MSG) || (tag==NEWIDS_END))
      MPI_Ssend(buf->b, buf->index, 
		MPI_INT, w, tag, comm);
    else
      MPI_Send(buf->b, buf->index, 
	       MPI_INT, w, tag, comm);
    
    buf->index = 0;
  }
}
 
//                                                                              

int IcheckSend(intbuf_t buf, int w, MPI_Comm comm, int size){ 
  // sends only UPDATE_MSG messages
  //  Warning(info,"IcheckSend %d %d %d",w,tag,size);
  assert(size <= MSGSIZE);
  if (buf->index + size > MSGSIZE){
#ifdef DEBUG
    fprintf(stderr,"\n%d SEND UPDATE_MSG (size %d) to %d ", 
	    mpi_me, buf->index, w);
#endif
    assert (buf->index <= MSGSIZE);
    
    MPI_Isend(buf->b, buf->index, 
	      MPI_INT, w, UPDATE_MSG, comm, &send_request);

    send_pending = 1;
    buf->index = 0;
    return 0;
  }
  return 1;
}


//                                                                                  

void rcvBuf(intbuf_t* bufs, int* w, MPI_Comm comm, int* tag, int* size){

  MPI_Status status; 
  if ((MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &status)) != MPI_SUCCESS)
    Abort("ERROR IN PROBE");
  MPI_Get_count(&status, MPI_INT, size);
  *w = status.MPI_SOURCE;
  *tag = status.MPI_TAG;

   MPI_Recv(bufs[*w]->b, *size, 
	    MPI_INT, status.MPI_SOURCE, 
	    *tag, comm, &status);
#ifdef DEBUG  
   Warning(info,"       %d rcvBuf %s (size %d) from %d", 
	   mpi_me, tname[*tag], *size, *w);
#endif
}


int IrcvBuf(intbuf_t* bufs, intbuf_t serversbuf, int* w, MPI_Comm comm, int* tag, int* size){
  // if there is a send request pending,
  //     receive UPDATE_END and UPDATE_MSG
  // else
  //     if there's processing pending
  //        receive UPDATE_END, UPDATE_MSG, NEWIDS_END
  //     else 
  //        receive UPDATE_END, UPDATE_MSG, NEWIDS_END, NEWIDS_MSG 

  intbuf_t bb = NULL;
  MPI_Status status; 
  int flag;
  //  Warning(info,"$$$$$$$$$ (W%d) SP: %d   PP: %d  ", mpi_me, send_pending, bufnewids_pending);
  if (send_pending) {
      MPI_Iprobe( MPI_ANY_SOURCE, UPDATE_END, comm, &flag, &status );
      if (!flag) 
	MPI_Iprobe( MPI_ANY_SOURCE, UPDATE_MSG, comm, &flag, &status );
      //   if (!flag) printf("\n...%d probing...", mpi_me);
  }
  else { 
    if (bufnewids_pending) {
      MPI_Iprobe( MPI_ANY_SOURCE, UPDATE_END, comm, &flag, &status );
      if (!flag) 
	MPI_Iprobe( MPI_ANY_SOURCE, UPDATE_MSG, comm, &flag, &status );
      if (!flag) 
	MPI_Iprobe( MPI_ANY_SOURCE, NEWIDS_END, comm, &flag, &status );
      //      if (!flag) printf("\n...%d PROBING...", mpi_me);
    }
    else {
       MPI_Probe(MPI_ANY_SOURCE,MPI_ANY_TAG,comm, &status);
      //MPI_Iprobe(MPI_ANY_SOURCE,MPI_ANY_TAG,comm, &flag, &status);
      flag = 1;
    }
  };


  if (!flag) return 0;
  
  MPI_Get_count(&status, MPI_INT, size);
  *w = status.MPI_SOURCE;
  *tag = status.MPI_TAG;
  
   if ((*tag == NEWIDS_MSG) || (*tag == NEWIDS_END))
     bb = serversbuf;
   else 
     bb = bufs[*w];
   

   MPI_Recv(bb->b, *size, 
	    MPI_INT, status.MPI_SOURCE, 
	    *tag, comm, &status);

   bb->index = 0;
   bb->size = *size;

#ifdef DEBUG  
   Warning(info,"\n      %d IrcvBuf %s (size %d) from %d", 
	   mpi_me, tname[*tag], *size, *w);
#endif

   return 1;
}

//                                                                           
void Send(int w, MPI_Comm comm, int tag, int msg){

  int aux;

  aux = msg;
  
  MPI_Send(&aux, 1, MPI_INT, w, tag, comm);
  
#ifdef DEBUG
  fprintf(stderr,"\n%d Sent %s:%d to MPI%d", mpi_me, tname[tag], aux, w);
#endif 
}

//                                                                 
void Receive(int* w, MPI_Comm comm, int tag, int* msg){

  MPI_Status status;
  MPI_Probe(MPI_ANY_SOURCE, tag,comm,&status);
  *w = status.MPI_SOURCE;
  MPI_Recv(msg, 1, MPI_INT, status.MPI_SOURCE, tag, comm, &status);

#ifdef DEBUG
  fprintf(stderr,"\n!!!Received %s:%d from %d", tname[tag], *msg, *w);
#endif 
}

intbuf_t newBuffer(int initialsize){
  intbuf_t buf;
  if ((buf = (intbuf_t)calloc(1,sizeof(struct intbuf))) == NULL)
      Abort("Could not allocate IntBuffer");
  buf->size = initialsize;
  if ((buf->b = (int*)calloc(buf->size, sizeof(int))) == NULL)
      Abort("Could not allocate IntBuffer");
  buf->index = 0;
  return buf;
}

void freeBuffer(intbuf_t buf){
  if (buf==NULL) return;
  if (buf->b != NULL) free(buf->b);
  buf->b = NULL;
  buf->index = buf->size = 0;
  free(buf);  
}

void resetBuffer(intbuf_t buf){
  if (buf==NULL)
    Abort("IntBuffer not initialized");
  buf->size = BUFSIZE;
  if ((buf->b = (int*)realloc(buf->b, buf->size*sizeof(int))) == NULL)
    Abort("Could not re-allocate IntBuffer");
  buf->index = 0;  
}

void printBuffer(intbuf_t buf, char* text){
  int i;
  printf("\n%s ", text);
  for(i=0;i<buf->index;i++)
    printf(" %d ",buf->b[i]);
  printf("\n");
}

void add(intbuf_t buf, int* x, int n){
  
  int i;
  
  while (buf->index  + n >= buf->size){
    buf->size += BUFSIZE;
    if(( buf->b = (int*)realloc(buf->b , (buf->size)*sizeof(int))) == NULL)
      Abort("Could not reallocate a Buffer");
  };
  for(i = 0; i < n; i++)
    buf->b[buf->index++] = x[i];
}

void add1(intbuf_t buf, int x){
  if (buf->index  + 1 >= buf->size){
    buf->size += BUFSIZE;
    if(( buf->b = (int*)realloc(buf->b , (buf->size)*sizeof(int))) == NULL)
      Abort("Could not reallocate a Buffer");
  };
  buf->b[buf->index++] = x;
}

void add4(intbuf_t buf, int x, int y, int z, int t){
  
  while (buf->index  + 4 >= buf->size){
    buf->size += BUFSIZE;
    if(( buf->b = (int*)realloc(buf->b , (buf->size)*sizeof(int))) == NULL)
      Abort("Could not reallocate a Buffer");
  };
  buf->b[buf->index++] = x;  
  buf->b[buf->index++] = y;  
  buf->b[buf->index++] = z;  
  buf->b[buf->index++] = t;
}



void printSig(int* s, int size){
  int i;
  if (size == 0) return;
  for(i = 0; i < size; i++)
    fprintf(stderr," &&&& %d ", s[i]);
}

void intbufSearch(intbuf_t buf, int ssize, int* s, int* tmpid){
  int i,j;
  *tmpid = -1;
  for(i = 0; (i < buf->index) && (*tmpid == -1) ; ){
    j = 0; while ((j < ssize) && (s[j] == buf->b[i+j])) j++;
    if (j >= ssize)
      *tmpid = buf->b[i + ssize];
    else 
      i += ssize + 1;
  }
}

//                                                                          

void sendBuffer(intbuf_t buf, int w, MPI_Comm comm, int tag){
 int i,n;

 n = MSGSIZE;
 if (buf->index == 0) 
   return;

 for (i = 0; i + n < buf->index; i+=n)
   MPI_Send(buf->b+i, n, MPI_INT, w, tag, comm);

 if (buf->index - i > 0)
   MPI_Send(buf->b+i, buf->index - i, 
	    MPI_INT, w, tag, comm); 

#ifdef DEBUG
 fprintf(stderr,"\nSent Buf %s (size %d) to %d", tname[tag], buf->index, w);
 printf("\n  ");
#endif 
}

