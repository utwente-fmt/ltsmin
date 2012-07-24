#include <config.h>

#include <pthread.h>
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

/**
\typedef a map of concurrent chunctables
*/
typedef struct cct_map_s cct_map_t;


struct cct_map_s {
    pthread_rwlock_t    lock;
    table_t            *table;
};

struct table_s {
    pthread_rwlock_t rwlock;
    void *string_index;
};

struct cct_cont_s {
    size_t                  map_index;
    cct_map_t              *tables;
};


static cct_map_t       *tables = NULL;

static cct_map_t *
cct_create_map()
{
    cct_map_t *map = RTmallocZero(sizeof(cct_map_t));
    map->table = RTmallocZero(sizeof(table_t[MAX_TABLES]));
    for ( size_t i = 0; i < MAX_TABLES; i++ )
        map->table[i].string_index = NULL;
    return map;
}

cct_cont_t *
cct_create_cont(size_t start_index)
{ 
    cct_cont_t *container = RTmalloc(sizeof(cct_cont_t));
    container->map_index = start_index;
    if (tables == NULL) //TODO: serialize
        tables = cct_create_map ();
    container->tables = tables;
    pthread_rwlock_init(&tables->lock, NULL);
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
    pthread_rwlock_rdlock(&map->lock);
    if (cont->map_index >= MAX_TABLES)
        Abort("Chunktable limit reached: %d.", MAX_TABLES);
    table_t *t = &map->table[cont->map_index++];
    pthread_rwlock_unlock(&map->lock);
    if (!t->string_index) {
        pthread_rwlock_wrlock(&map->lock);
        if (!t->string_index) {
            pthread_rwlock_init(&t->rwlock, NULL);
            t->string_index = SIcreate();
        }
        pthread_rwlock_unlock(&map->lock);
    }
    return t;
}

inline void *
cct_map_get(void*ctx,int idx,int*len) 
{
    table_t *table = ctx;
    pthread_rwlock_rdlock(&table->rwlock);
    char *res = SIgetC(table->string_index, idx, len);
    pthread_rwlock_unlock(&table->rwlock);
    return res;
}

inline int 
cct_map_put(void*ctx,void *chunk,int len) 
{
    table_t        *table = ctx;
    pthread_rwlock_rdlock(&table->rwlock);
    int             idx = SIlookupC(table->string_index,chunk,len);
    pthread_rwlock_unlock(&table->rwlock);
    if (idx==SI_INDEX_FAILED) {
        pthread_rwlock_wrlock(&table->rwlock);
        idx = SIputC(table->string_index,chunk,len);
        pthread_rwlock_unlock(&table->rwlock);
    }
    return idx;
}

int 
cct_map_count(void* ctx) 
{
    table_t *table = ctx;
    pthread_rwlock_rdlock(&table->rwlock);
    int res = SIgetCount(table->string_index);
    pthread_rwlock_unlock(&table->rwlock);
    return res;
}
