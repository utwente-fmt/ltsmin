// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <hre-io/user.h>
#include <lts-io/internal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Ddlts.h"
#include <ltsmin-lib/ltsmin-standard.h>

#define BUFFER_SIZE (1024 * 1024)


/****************************************************

dlts_t dlts_create(MPI_Comm communicator)

 ****************************************************/
 
dlts_t dlts_read(MPI_Comm communicator,char*filename){
    int me, nodes;
    int i,j;
    dlts_t lts;
    
    hre_context_t ctx=HREglobal();
    HREbarrier(ctx);
    nodes=HREpeers(ctx);
    me=HREme(ctx);
    
    lts=(dlts_t)malloc(sizeof(struct dlts));
    if (!lts) Abort("out of memory in dlts_create");
    lts->dirname=filename;
    lts->segment_count=nodes;
    lts->comm=communicator;
    lts->tau=-1;
    
    lts_file_t file=lts_file_open(filename);
    value_table_t vt=HREcreateTable(ctx,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    lts_file_set_table(file,0,vt);
    lts->label_count=VTgetCount(vt);
    Debug("there are %d labels",lts->label_count);
    lts->label_string=(char**)malloc(lts->label_count*sizeof(char*));
    for(i=0;i<lts->label_count;i++){
        chunk c=VTgetChunk(vt,i);
        lts->label_string[i]=RTmalloc(c.len + 1);
        strncpy(lts->label_string[i], c.data,c.len);
        lts->label_string[i][c.len] = '\0';
        if ( (c.len==3 && strncmp(c.data,LTSMIN_EDGE_VALUE_TAU,3)==0)
          || (c.len==1 && strncmp(c.data,"i",1)==0)
           )
        {
            if (me==0) Print(infoShort,"invisible label is %s",c.data);
            if (lts->tau>=0) Abort("two silent labels");
            lts->tau=i;
        }
        if (me==0) Debug("label %d is \"%s\"",i,lts->label_string[i]);
    }
    
    if (me==0) {
        lts_read_init(file,&(lts->root_seg),&(lts->root_ofs));
    } else {
        lts->root_seg=0;
        lts->root_ofs=0;
    }
    HREreduce(ctx,1,&(lts->root_seg),&(lts->root_seg),UInt32,Max);
    HREreduce(ctx,1,&(lts->root_ofs),&(lts->root_ofs),UInt32,Max);
    Debug("root is %d/%d",lts->root_seg,lts->root_ofs);

    lts->state_count=(int*)malloc(lts->segment_count*sizeof(int));
    for(i=0;i<lts->segment_count;i++){
        lts->state_count[i]=lts_get_state_count(file,i);
        Debug("count[%d]=%d",i,lts->state_count[i]);
    }
    lts->transition_count=(int**)malloc(lts->segment_count*sizeof(int*));
    lts->src=(int***)malloc(lts->segment_count*sizeof(int**));
    lts->label=(int***)malloc(lts->segment_count*sizeof(int**));
    lts->dest=(int***)malloc(lts->segment_count*sizeof(int**));
    for(i=0;i<lts->segment_count;i++){
        lts->transition_count[i]=(int*)malloc(lts->segment_count*sizeof(int));
        lts->src[i]=(int**)malloc(lts->segment_count*sizeof(int*));
        lts->label[i]=(int**)malloc(lts->segment_count*sizeof(int*));
        lts->dest[i]=(int**)malloc(lts->segment_count*sizeof(int*));
        for(j=0;j<lts->segment_count;j++){
            lts->transition_count[i][j]=0;
            lts->src[i][j]=NULL;
            lts->label[i][j]=NULL;
            lts->dest[i][j]=NULL;
        }
    }
    switch(lts_file_get_edge_owner(file)){
    case SourceOwned:
        Debug("source owned edges");
        break;
    case DestOwned:
        Debug("target owned edges");
        break;
    default:
        Abort("unknown edge ownership");
    }
    int N=lts_get_edge_count(file,me);

    Debug("loading %d edges",N);
    switch(lts_file_get_edge_owner(file)){
    case SourceOwned:{
        for(i=0;i<lts->segment_count;i++){
            lts->src[me][i]=(int*)malloc(N*sizeof(int));
            lts->label[me][i]=(int*)malloc(N*sizeof(int));
            lts->dest[me][i]=(int*)malloc(N*sizeof(int));
        }
        int src_seg,src_ofs,dst_seg,dst_ofs,lbl;
        src_seg=me;
        while(lts_read_edge(file,&src_seg,&src_ofs,&dst_seg,&dst_ofs,&lbl)){
            lts->src[me][dst_seg][lts->transition_count[me][dst_seg]]=src_ofs;
            lts->label[me][dst_seg][lts->transition_count[me][dst_seg]]=lbl;
            lts->dest[me][dst_seg][lts->transition_count[me][dst_seg]]=dst_ofs;
            lts->transition_count[me][dst_seg]++;
        }
        break;
    }
    case DestOwned:{
        for(i=0;i<lts->segment_count;i++){
            lts->src[i][me]=(int*)malloc(N*sizeof(int));
            lts->label[i][me]=(int*)malloc(N*sizeof(int));
            lts->dest[i][me]=(int*)malloc(N*sizeof(int));
        }
        int src_seg,src_ofs,dst_seg,dst_ofs,lbl;
        dst_seg=me;
        while(lts_read_edge(file,&src_seg,&src_ofs,&dst_seg,&dst_ofs,&lbl)){
            lts->src[src_seg][me][lts->transition_count[src_seg][me]]=src_ofs;
            lts->label[src_seg][me][lts->transition_count[src_seg][me]]=lbl;
            lts->dest[src_seg][me][lts->transition_count[src_seg][me]]=dst_ofs;
            lts->transition_count[src_seg][me]++;
        }
        break;
    }}
    Debug("distributing edge counts");
    for(i=0;i<lts->segment_count;i++){
        HREreduce(ctx,lts->segment_count,lts->transition_count[i],lts->transition_count[i],UInt32,Max);
    }
    Debug("adjusting allocated memory");
    for(i=0;i<lts->segment_count;i++){
        lts->src[i][me]=(int*)realloc(lts->src[i][me],lts->transition_count[i][me]*sizeof(int));
        lts->label[i][me]=(int*)realloc(lts->label[i][me],lts->transition_count[i][me]*sizeof(int));
        lts->dest[i][me]=(int*)realloc(lts->dest[i][me],lts->transition_count[i][me]*sizeof(int));
        if (i==me) continue;
        lts->src[me][i]=(int*)realloc(lts->src[me][i],lts->transition_count[me][i]*sizeof(int));
        lts->label[me][i]=(int*)realloc(lts->label[me][i],lts->transition_count[me][i]*sizeof(int));
        lts->dest[me][i]=(int*)realloc(lts->dest[me][i],lts->transition_count[me][i]*sizeof(int));            
    }
    Debug("copying edge data");
    MPI_Barrier(communicator);
    switch(lts_file_get_edge_owner(file)){
    case SourceOwned:
        for(int r=1;r<lts->segment_count;r++){
            i=(me+r)%nodes;
            j=(me+nodes-r)%nodes;
            Debug("transitions from %d and to %d",i,j);
            MPI_Sendrecv(
                lts->src[me][j], lts->transition_count[me][j] , MPI_INT, j , 1,
                lts->src[i][me], lts->transition_count[i][me] , MPI_INT, i , 1,
                communicator,NULL
            );
            MPI_Sendrecv(
                lts->label[me][j], lts->transition_count[me][j] , MPI_INT, j , 2,
                lts->label[i][me], lts->transition_count[i][me] , MPI_INT, i , 2,
                communicator,NULL
            );
            MPI_Sendrecv(
                lts->dest[me][j], lts->transition_count[me][j] , MPI_INT, j , 3,
                lts->dest[i][me], lts->transition_count[i][me] , MPI_INT, i , 3,
                communicator,NULL
            );
            MPI_Barrier(communicator);
        }
        break;
    case DestOwned:
        for(int r=1;r<lts->segment_count;r++){
            i=(me+r)%nodes;
            j=(me+nodes-r)%nodes;
            Debug("transitions from %d and to %d",i,j);
            MPI_Sendrecv(
                lts->src[i][me], lts->transition_count[i][me] , MPI_INT, i , 1,
                lts->src[me][j], lts->transition_count[me][j] , MPI_INT, j , 1,
                communicator,NULL
            );
            MPI_Sendrecv(
                lts->label[i][me], lts->transition_count[i][me] , MPI_INT, i , 2,
                lts->label[me][j], lts->transition_count[me][j] , MPI_INT, j , 2,
                communicator,NULL
            );
            MPI_Sendrecv(
                lts->dest[i][me], lts->transition_count[i][me] , MPI_INT, i , 3,
                lts->dest[me][j], lts->transition_count[me][j] , MPI_INT, j , 3,
                communicator,NULL
            );
            MPI_Barrier(communicator);
        }
        break;
    }
    return lts;
}


/****************************************************

void dlts_free(dlts_t lts)

 ****************************************************/

void dlts_free(dlts_t lts){
    int i,j;
    for(i=0;i<lts->segment_count;i++){
        free(lts->transition_count[i]);
        free(lts->label_string[i]);
        for(j=0;j<lts->segment_count;j++){
            if (lts->src[i][j]) free(lts->src[i][j]);
            if (lts->label[i][j]) free(lts->label[i][j]);
            if (lts->dest[i][j]) free(lts->dest[i][j]);
        }
        free(lts->src[i]);
        free(lts->label[i]);
        free(lts->dest[i]);
    }
    free(lts->transition_count);
    free(lts->label_string);
    free(lts->src);
    free(lts->label);
    free(lts->dest);
    free(lts->comm);
    free(lts);
}

/****************************************************

 ****************************************************/

void dlts_writedir(dlts_t lts, char* name){

 int me, nodes;
 int i,j;
 
 hre_context_t ctx=HREglobal();
 HREbarrier(ctx);
 nodes=HREpeers(ctx);
 me=HREme(ctx);
 lts_type_t ltstype=single_action_type();
 lts_file_t template=lts_index_template();
 lts_file_set_edge_owner(template,SourceOwned);
 lts_file_t file=lts_file_create(name,ltstype,nodes,template);
 value_table_t vt=HREcreateTable(ctx,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
 lts_file_set_table(file,0,vt);
 if (me==0) {
    lts_write_init(file,lts->root_seg,&lts->root_ofs);
    for(i=0;i<lts->label_count;i++){
        VTputChunk(vt,chunk_str(lts->label_string[i]));
    }
 }
 for(j=0;j<lts->segment_count;j++) {
    for(i=0;i<lts->transition_count[me][j];i++){
        lts_write_edge(file,me,&lts->src[me][j][i],j,&lts->dest[me][j][i],&lts->label[me][j][i]);
    }
 }
 HREbarrier(ctx);
 lts_file_close(file);
}

