// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <string.h>
#include <stdlib.h>

#include <hre/user.h>
#include <util-lib/tables.h>
#include <util-lib/dynamic-array.h>


struct value_table_s {
    char               *type_name;
    size_t              user_size;
    user_destroy_t      destroy;
    put_native_t        put_native;
    get_native_t        get_native;
    put_chunk_t         put_chunk;
    put_at_chunk_t      put_at_chunk;
    get_chunk_t         get_chunk;
    vt_get_count_t      get_count;
    vt_iterator_t       get_iterator;
};

static const size_t vt_size=((sizeof(struct value_table_s)+7)/8)*8;
#define SYS2USR(var) ((value_table_t)(((char*)(var))+vt_size))
#define USR2SYS(var) ((value_table_t)(((char*)(var))-vt_size))

static value_index_t missing_put_native(value_table_t vt,va_list args){
    (void)vt;(void)args;
    Abort("method put_native has not been set");
    return (value_index_t)0;
}

static void missing_get_native(value_table_t vt,value_index_t idx,va_list args){
    (void)vt;(void)idx;(void)args;
    Abort("method get_native has not been set");
}

static value_index_t missing_put_chunk(value_table_t vt,chunk item){
    (void)vt;(void)item;
    Abort("method put_chunk has not been set");
    return (value_index_t)0;
}

static void missing_put_at_chunk(value_table_t vt,chunk item,value_index_t pos){
    (void)vt;(void)item;(void)pos;
    Abort("method put_chunk has not been set");
}

static chunk missing_get_chunk(value_table_t vt,value_index_t idx){
    (void)vt;(void)idx;
    Abort("method get_chunk has not been set");
    return chunk_str("");
}

static int missing_get_count(value_table_t vt){
    (void)vt;
    Abort("method get_count has not been set");
    return -1;
}

static table_iterator_t missing_get_iterator (value_table_t vt) {
    (void)vt;
    Abort("method get_iterator has not been set");
    return NULL;
}

value_table_t VTcreateBase(char*type_name,size_t user_size){
    value_table_t object = (value_table_t)RTmallocZero(vt_size + user_size);
    object->type_name=strdup(type_name);
    object->user_size=user_size;
    object->destroy=NULL;
    object->put_native  = missing_put_native;
    object->get_native  = missing_get_native;
    object->put_chunk   = missing_put_chunk;
    object->get_chunk   = missing_get_chunk;
    object->get_count   = missing_get_count;
    object->get_iterator= missing_get_iterator;
    return SYS2USR(object);
}

void VTdestroy(value_table_t vt){
    value_table_t object=USR2SYS(vt);
    if (object->destroy) object->destroy(vt);
    free(object->type_name); //strdup
    RTfree(object);
}

void VTdestroyZ(value_table_t *vt_ptr){
    VTdestroy(*vt_ptr);
    *vt_ptr=NULL;
}

char* VTgetType(value_table_t vt){
    value_table_t object=USR2SYS(vt);
    return object->type_name;
}

void VTdestroySet(value_table_t vt,user_destroy_t method){
    value_table_t object=USR2SYS(vt);
    object->destroy=method;
}

value_index_t VTputChunk(value_table_t vt,chunk item){
    value_table_t object=USR2SYS(vt);
    return object->put_chunk(vt,item);
}

void VTputChunkSet(value_table_t vt,put_chunk_t method){
    value_table_t object=USR2SYS(vt);
    object->put_chunk=method?method:missing_put_chunk;
}

void VTputAtChunk(value_table_t vt,chunk item,value_index_t pos){
    value_table_t object=USR2SYS(vt);
    object->put_at_chunk(vt,item,pos);
}

void VTputAtChunkSet(value_table_t vt,put_at_chunk_t method){
    value_table_t object=USR2SYS(vt);
    object->put_at_chunk=method?method:missing_put_at_chunk;
}

chunk VTgetChunk(value_table_t vt,value_index_t idx){
    value_table_t object=USR2SYS(vt);
    return object->get_chunk(vt,idx);
}

void VTgetChunkSet(value_table_t vt,get_chunk_t method){
    value_table_t object=USR2SYS(vt);
    object->get_chunk=method?method:missing_get_chunk;
}

int VTgetCount(value_table_t vt){
    value_table_t object=USR2SYS(vt);
    return object->get_count(vt);
}

void VTgetCountSet(value_table_t vt,vt_get_count_t method){
    value_table_t object=USR2SYS(vt);
    object->get_count=method?method:missing_get_count;
}

void VTiteratorSet (value_table_t vt, vt_iterator_t method) {
    value_table_t object=USR2SYS(vt);
    object->get_iterator = method;
}

table_iterator_t VTiterator(value_table_t vt) {
    value_table_t object=USR2SYS(vt);
    return object->get_iterator (vt);
}

#undef SYS2USR
#undef USR2SYS


struct table_iterator_s {
    it_next_t           next;
    it_has_next_t       has_next;
};

static const size_t iterator_size=((sizeof(struct table_iterator_s)+7)/8)*8;
#define SYS2USR(var) ((table_iterator_t)(((char*)(var))+iterator_size))
#define USR2SYS(var) ((table_iterator_t)(((char*)(var))-iterator_size))


static chunk missing_next(table_iterator_t it){
    (void) it;
    Abort ("method next has not been set");
    return (chunk){ .data = NULL, .len = 0 };
}

static int missing_has_next(table_iterator_t it){
    (void) it;
    Abort ("method has_next has not been set");
    return -1;
}

table_iterator_t ITcreateBase (size_t user_size) {
    table_iterator_t object = (table_iterator_t) RTmallocZero (iterator_size+user_size);
    object->next = missing_next;
    object->has_next = missing_has_next;
    return SYS2USR(object);
}

chunk ITnext (table_iterator_t vt) {
    table_iterator_t object=USR2SYS(vt);
    return object->next (vt);
}

void ITnextSet (table_iterator_t vt, it_next_t method) {
    table_iterator_t object=USR2SYS(vt);
    object->next = method;
}

int IThasNext (table_iterator_t vt) {
    table_iterator_t object=USR2SYS(vt);
    return object->has_next (vt);
}

void IThasNextSet (table_iterator_t vt, it_has_next_t method) {
    table_iterator_t object=USR2SYS(vt);
    object->has_next = method;
}


#undef SYS2USR
#undef USR2SYS


struct matrix_table_struct{
    int width;
    array_manager_t man;
    uint32_t**column;
    uint32_t *begin;
    uint32_t count;
    int cluster_col;
    uint32_t cluster_count;
};

matrix_table_t MTcreate(int width){
    if (width<=0) Abort("illegal argument");
    matrix_table_t mt=RT_NEW(struct matrix_table_struct);
    mt->width=width;
    mt->man=create_manager(65536);
    mt->column=RTmalloc(width*sizeof(uint32_t*));
    for(int i=0;i<width;i++){
        mt->column[i]=NULL;
        ADD_ARRAY(mt->man,mt->column[i],uint32_t);
    }
    mt->count=0;
    mt->cluster_col=-1;
    return mt;
}

void MTdestroy(matrix_table_t mt){
    for(int i=0;i<mt->width;i++){
        RTfree(mt->column[i]);
    }
    RTfree(mt->column);
    // TODO destroy_manager(mt->man);
    RTfree(mt);
}

void MTdestroyZ(matrix_table_t* mt_ptr){
    MTdestroy(*mt_ptr);
    *mt_ptr=NULL;
}

int MTgetWidth(matrix_table_t mt){
    return mt->width;
}

int MTgetCount(matrix_table_t mt){
    return mt->count;
}

void MTaddRow(matrix_table_t mt,uint32_t *row){
    ensure_access(mt->man,mt->count);
    for(int i=0;i<mt->width;i++){
        mt->column[i][mt->count]=row[i];
    }
    mt->count++;
}

void MTgetRow(matrix_table_t mt,int row_no,uint32_t *row){
     for(int i=0;i<mt->width;i++){
        if (i!=mt->cluster_col) row[i]=mt->column[i][row_no];
    }
}

static void MTsetRow(matrix_table_t mt,int row_no,uint32_t *row){
     for(int i=0;i<mt->width;i++){
        if (i!=mt->cluster_col) mt->column[i][row_no]=row[i];
    }
}

static void MTcopyRow(matrix_table_t mt,int src,int dst){
     for(int i=0;i<mt->width;i++){
        if (i!=mt->cluster_col) mt->column[i][dst]=mt->column[i][src];
    }
}

void MTupdate(matrix_table_t mt,int row,int col,uint32_t val){
    mt->column[col][row]=val;
}

void MTclusterSort(matrix_table_t mt,int col){
    if (mt->cluster_col==-1) Abort("please cluster first");
    if (mt->cluster_col==col) Abort("cannot sort on clustered column");
    uint32_t row[mt->width];
    for(uint32_t i=0;i<mt->cluster_count;i++){
        for(uint32_t j=mt->begin[i]+1;j<mt->begin[i+1];j++){
            uint32_t k=j;
            while(k>mt->begin[i] && mt->column[col][j]<mt->column[col][k-1]) k--;
            if (k==j) continue;
            (void)row;(void)MTcopyRow;
            MTgetRow(mt,j,row);
            for(uint32_t l=j;l>k;l--){
                MTcopyRow(mt,l-1,l);
            }
            MTsetRow(mt,k,row);
        }
    }
}


void MTclusterBuild(matrix_table_t mt,int col,uint32_t count){
    if (mt->cluster_col!=-1) Abort("can only cluster once");
    mt->cluster_count=count;
    mt->begin=(uint32_t*)RTmallocZero((count+1)*sizeof(uint32_t));
    Warning(debug,"counting cluster sizes");
    for(uint32_t i=0;i<mt->count;i++){
        if(mt->column[col][i]>=count) Abort("value exceeds cluster count");
        mt->begin[mt->column[col][i]]++;
    }
    Warning(debug,"summing up");
    for(uint32_t i=1;i<=count;i++){
        mt->begin[i]+=mt->begin[i-1];
    }
    Warning(debug,"replace column value with position in array");
    for(uint32_t i=0;i<mt->count;i++){
        mt->column[col][i]=--mt->begin[mt->column[col][i]];
    }
    Warning(debug,"moving rows to correct positions");
    uint32_t row1[mt->width];
    uint32_t row2[mt->width];
    for(uint32_t i=0;i<mt->count;i++){
        if(mt->column[col][i]==i) continue;
        uint32_t pos1,pos2;
        pos1=mt->column[col][i];
        MTgetRow(mt,i,row1);
        //Warning(debug,"moving row %d to %d",i,pos1);
        for(;;){
            if (pos1==i) {
                MTsetRow(mt,pos1,row1);
                break;
            }
            pos2=mt->column[col][pos1];
            //Warning(debug,"moving row %d to %d",pos1,pos2);
            MTgetRow(mt,pos1,row2);
            MTsetRow(mt,pos1,row1);
            //Progress(debug,"pos1=%d pos2=%d i=%d",pos1,pos2,i);
            if (pos2==i) {
                MTsetRow(mt,pos2,row2);
                break;
            }
            pos1=mt->column[col][pos2];
            //Warning(debug,"moving row %d to %d",pos2,pos1);
            MTgetRow(mt,pos2,row1);
            MTsetRow(mt,pos2,row2);
        }
    }
    Warning(debug,"deleting old column");
    RTfree(mt->column[col]);
    mt->column[col]=NULL;
    mt->cluster_col=col;
}

int MTclusterSize(matrix_table_t mt,uint32_t cluster){
    return mt->begin[cluster+1]-mt->begin[cluster];
}

void MTclusterGetRow(matrix_table_t mt,uint32_t cluster,int row_ofs,uint32_t *row){
    uint32_t row_no=mt->begin[cluster]+row_ofs;
    for(int i=0;i<mt->width;i++){
        row[i]=(i==mt->cluster_col)?cluster:mt->column[i][row_no];
    } 
}

uint32_t MTclusterGetElem(matrix_table_t mt,uint32_t cluster,int row_ofs,int col){
    uint32_t row_no=mt->begin[cluster]+row_ofs;
    return (col==mt->cluster_col)?cluster:mt->column[col][row_no];
}

uint32_t* MTclusterMapBegin(matrix_table_t mt){
    return mt->begin;
}

uint32_t* MTclusterMapColumn(matrix_table_t mt,int col){
    return mt->column[col];
}

uint32_t MTclusterCount(matrix_table_t mt){
    return mt->cluster_count;
}

void MTclusterUpdate(matrix_table_t mt,uint32_t cluster,int row_ofs,int col,uint32_t val){
    uint32_t row_no=mt->begin[cluster]+row_ofs;
    mt->column[col][row_no]=val;
}

uint32_t MTgetMax(matrix_table_t mt,int col){
    uint32_t max=0;
    for(uint32_t i=0;i<mt->count;i++){
        if(mt->column[col][i]>max) max=mt->column[col][i];
    }
    return max;
}

#define NIL ((uint32_t)-1)
#define UNUSED ((uint32_t)-2)

typedef enum { Lt=-1, Eq=0, Gt=1 } compare_t;

static compare_t lex_cmp(matrix_table_t mt,uint32_t row1,uint32_t row2){
    for(int i=0 ; i<mt->width ; i++){
        if (mt->column[i][row1]<mt->column[i][row2]) return Lt;
        if (mt->column[i][row1]>mt->column[i][row2]) return Gt;
    }
    return Eq;
}

static uint32_t MTsort(matrix_table_t mt,uint32_t *next,uint32_t offset,uint32_t size){
    if(size==1) return offset;
    uint32_t half=size/2;
    uint32_t list1=MTsort(mt,next,offset,half);
    uint32_t list2=MTsort(mt,next,offset+half,size-half);
    uint32_t head = ~0;
    uint32_t tail = ~0;
    uint32_t tmp;
    switch(lex_cmp(mt,list1,list2)){
    case Lt:
        head=list1;
        tail=list1;
        list1=next[list1];
        break;
    case Eq:
        head=list1;
        tail=list1;
        list1=next[list1];
        tmp=list2;
        list2=next[list2];
        next[tmp]=UNUSED;
        break;
    case Gt:
        head=list2;
        tail=list2;
        list2=next[list2];
        break;
    }
    for(;;){
        if (list1==NIL) {
            next[tail]=list2;
            return head;
        }
        if (list2==NIL){
            next[tail]=list1;
            return head;
        }
        switch(lex_cmp(mt,list1,list2)){
            case Lt:
                next[tail]=list1;
                tail=list1;
                list1=next[list1];
                break;
            case Eq:
                next[tail]=list1;
                tail=list1;
                list1=next[list1];
                tmp=list2;
                list2=next[list2];
                next[tmp]=UNUSED;
                break;
            case Gt:
                next[tail]=list2;
                tail=list2;
                list2=next[list2];
                break;
        }
    }
    Abort ("Unexpected");
    return 0;
}

void MTsimplify(matrix_table_t dst, matrix_table_t src){
    dst->count=0;
    if(dst->width!=src->width) Abort("different widths");
    if (src->count==0) return;
    uint32_t *next=RTmalloc(src->count*4);
    for(uint32_t i=0;i<src->count;i++){
        next[i]=NIL;
    }
    uint32_t list=MTsort(src,next,0,src->count);
    uint32_t len=0;
    uint32_t row[src->width];
    while(list!=NIL){
        MTgetRow(src,list,row);
        MTaddRow(dst,row);
        len++;
        list=next[list];
    }
    Warning(info,"length of simplified list is %u",len);
    RTfree(next);
}
