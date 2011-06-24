#include <config.h>
#include <stdint.h>
#include <pthread.h>

#include "runtime.h"
#include "cctables.h"
#include "stringindex.h"

static const size_t MAX_TABLES = 500;

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

cct_map_t *
cct_create_map()
{
    cct_map_t *map = RTmallocZero(sizeof(cct_map_t));
    map->table = RTmallocZero(sizeof(table_t[MAX_TABLES]));
    for ( size_t i = 0; i < MAX_TABLES; i++ )
        map->table[i].string_index = NULL;
    return map;
}

cct_cont_t *
cct_create_cont(cct_map_t *tables, size_t start_index)
{ 
    cct_cont_t *container = RTmalloc(sizeof(cct_cont_t));
    container->map_index = start_index;
    container->tables = tables;
    pthread_rwlock_init(&tables->lock, NULL);
    return container;
}

void *
cct_new_map(void* context) 
{
    cct_cont_t *cont = context;
    cct_map_t *map = cont->tables;
    pthread_rwlock_rdlock(&map->lock);
    if (cont->map_index >= MAX_TABLES)
        Fatal(1, error, "Chunktable limit reached: %d.", MAX_TABLES);
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
