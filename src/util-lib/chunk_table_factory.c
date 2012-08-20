#include <hre/config.h>

#include <hre/stringindex.h>
#include <util-lib/tables.h>

static void destroy(value_table_t vt){
    SIdestroy((string_index_t*)vt);
}

static value_index_t put_chunk(value_table_t vt,chunk item){
    string_index_t si=*((string_index_t*)vt);
    return (value_index_t)SIputC(si,item.data,item.len);
}

static chunk get_chunk(value_table_t vt,value_index_t idx){
    string_index_t si=*((string_index_t*)vt);
    int len;
    char*data=SIgetC(si,(int)idx,&len);
    return chunk_ld(len,data);
}

static int get_count(value_table_t vt){
    string_index_t si=*((string_index_t*)vt);
    return SIgetCount(si);
}

value_table_t chunk_table_create(void* context,char*type_name){
    (void)context;
    value_table_t vt=VTcreateBase(type_name,sizeof(string_index_t));
    *((string_index_t*)vt)=SIcreate();
    VTdestroySet(vt,destroy);
    VTputChunkSet(vt,put_chunk);
    VTgetChunkSet(vt,get_chunk);
    VTgetCountSet(vt,get_count);
    return vt;
}



