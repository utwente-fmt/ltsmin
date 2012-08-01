#include <config.h>

#include <stdint.h>

#include <cctables.h>
#include <chunk_support.h>
#include <hre/user.h>
#include <stringindex.h>


static const size_t MAX_TABLES = 500;


/**
\typedef a chunk table
use malloc
*/
typedef struct table_s table_t;


struct cct_map_s {
    pthread_rwlock_t        map_lock;
    table_t                *table;
};

struct table_s {
    pthread_rwlock_t        table_lock;
    void                   *string_index;
};

struct cct_cont_s {
    size_t                  map_index;
    cct_map_t              *tables;
};


cct_map_t *
cct_create_map()
{
    MC_ALLOC_GLOBAL
    cct_map_t *map = RTmallocZero(sizeof(cct_map_t));
    map->table = RTmallocZero(sizeof(table_t[MAX_TABLES]));
    for ( size_t i = 0; i < MAX_TABLES; i++ )
        map->table[i].string_index = NULL;
    pthread_rwlockattr_t    lock_attr;
    pthread_rwlockattr_init(&lock_attr);
    if (pthread_rwlockattr_setpshared(&lock_attr, MC_MUTEX_SHARED_ATTR)){
        AbortCall("pthread_rwlockattr_setpshared");
    }
    pthread_rwlock_init(&map->map_lock, &lock_attr);
    MC_ALLOC_LOCAL
    return map;
}

cct_cont_t *
cct_create_cont(cct_map_t *tables)
{ 
    cct_cont_t *container = RTmalloc(sizeof(cct_cont_t));
    container->map_index = 0;
    container->tables = tables;
    return container;
}

static value_index_t put_chunk(value_table_t vt,chunk item){
    void *t=*((void **)vt);
    return (value_index_t)cct_map_put(t,item.data,item.len);
}

static chunk get_chunk(value_table_t vt,value_index_t idx){
    void *t=*((void **)vt);
    int len;
    char*data=cct_map_get(t,(int)idx,&len);
    return chunk_ld(len,data);
}

static int get_count(value_table_t vt){
    void *t=*((void **)vt);
    return cct_map_count(t);
}

value_table_t
cct_create_vt(cct_cont_t *ctx)
{
    cct_cont_t         *map = ctx;
    value_table_t vt = VTcreateBase("CCT map", sizeof(table_t *));
    VTdestroySet(vt,NULL);
    VTputChunkSet(vt,put_chunk);
    VTgetChunkSet(vt,get_chunk);
    VTgetCountSet(vt,get_count);
    *((void **)vt) = cct_new_map(map);
    return vt;
}


void *
cct_new_map(void* context) 
{
    cct_cont_t *cont = context;
    cct_map_t *map = cont->tables;
    pthread_rwlock_rdlock(&map->map_lock);
    if (cont->map_index >= MAX_TABLES)
        Abort("Chunktable limit reached: %d.", MAX_TABLES);
    table_t *t = &map->table[cont->map_index++];
    pthread_rwlock_unlock(&map->map_lock);
    if (!t->string_index) {
        pthread_rwlock_wrlock(&map->map_lock);
        if (!t->string_index) {
            MC_ALLOC_GLOBAL
            pthread_rwlockattr_t attr; pthread_rwlockattr_init(&attr);
            if (pthread_rwlockattr_setpshared(&attr, MC_MUTEX_SHARED_ATTR)){
                AbortCall("pthread_rwlockattr_setpshared");
            }
            pthread_rwlock_init(&t->table_lock, &attr);
            t->string_index = SIcreate();
            MC_ALLOC_LOCAL
        }
        pthread_rwlock_unlock(&map->map_lock);
    }
    return t;
}

inline void *
cct_map_get(void*ctx,int idx,int*len) 
{
    table_t *table = ctx;
    pthread_rwlock_rdlock(&table->table_lock);
    char *res = SIgetC(table->string_index, idx, len);
    pthread_rwlock_unlock(&table->table_lock);
    return res;
}

inline int 
cct_map_put(void*ctx,void *chunk,int len) 
{
    table_t        *table = ctx;
    pthread_rwlock_rdlock(&table->table_lock);
    int             idx = SIlookupC(table->string_index,chunk,len);
    pthread_rwlock_unlock(&table->table_lock);
    if (idx==SI_INDEX_FAILED) {
        pthread_rwlock_wrlock(&table->table_lock);
        MC_ALLOC_GLOBAL
        idx = SIputC(table->string_index,chunk,len);
        MC_ALLOC_LOCAL
        pthread_rwlock_unlock(&table->table_lock);
    }
    return idx;
}

int 
cct_map_count(void* ctx) 
{
    table_t *table = ctx;
    pthread_rwlock_rdlock(&table->table_lock);
    int res = SIgetCount(table->string_index);
    pthread_rwlock_unlock(&table->table_lock);
    return res;
}
