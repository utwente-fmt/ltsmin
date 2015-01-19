#include <hre/config.h>

#include <pthread.h>
#include <stdint.h>

#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/cctables.h>
#include <mc-lib/set-ll.h>
#include <util-lib/chunk_support.h>


/**
 * Class diagram:
 *
 * (singleton)
 *    /---\ 1     *  /-----\
 *    |Map|----------|Table|
 *    \---/          \-----/
 *      |1
 *      |
 *      |
 * /---------\ 1  1 /------\
 * |Container|------|Worker|
 * \---------/      \------/
 *
 * A worker (PThread or process) has a local container which maintains allocated
 * tables. Tables are allocated in fixed order because all workers execute the
 * same deterministic code.
 * Before a table is allocated, the container checks the global Map structure
 * whether or not it the table with this index was already allocated (remember
 * the fixed order). If not allocates it, adds it to the map and the container.
 * Otherwise, it reads the existing table from the Map and adds it to the
 * local container.
 *
 * A function pointer in allocator switches the allocator to a inter-process
 * allocator when tables are create or when values are added to the tables.
 * An rw mutex takes care of mutual exclusion (it is initialized as
 * inter-process mutex depending on the value of shared).
 */

static const size_t MAX_TABLES = 500;


/**
\typedef a chunk table
use malloc
*/
typedef struct table_s table_t;

struct cct_map_s {
    ticket_rwlock_t         rw_lock;
    bool                    shared;   // shared or private allocation
    table_t                *table;
    set_ll_allocator_t     *set_allocator;
};

struct table_s {
    void                   *string_set;
    cct_map_t              *map;
};

struct cct_cont_s {
    size_t                  map_index;
    cct_map_t              *map;
};

size_t
cct_print_stats(log_t log, log_t details, lts_type_t ltstype, cct_map_t *map)
{
    size_t                  i = 0;
    size_t                  total = 0;
    total += set_ll_print_alloc_stats(log, map->set_allocator);
    while (map->table[i].string_set != NULL) {
        char                   *name = lts_type_get_type(ltstype, i);
        total += set_ll_print_stats(details, map->table[i].string_set, name);
        i++;
    }
    Warning (log, "Total memory used for chunk indexing: %zuMB", total >> 20);
    return total;
}

double
cct_finalize(cct_map_t *map, char *bogus)
{
    size_t                  i = 0;
    double                  underwater = 0;
    while (map->table[i].string_set != NULL) {
        underwater += set_ll_finalize (map->table[i].string_set, bogus);
        i++;
    }
    return underwater / i;
}

cct_map_t *
cct_create_map(bool shared)
{
    RTswitchAlloc (shared); // global shared-memory allocation?
    cct_map_t              *map = RTmalloc(sizeof(cct_map_t));
    map->shared = shared;
    map->table = RTmalloc(sizeof(table_t[MAX_TABLES]));
    RTswitchAlloc (false);

    for (size_t i = 0; i < MAX_TABLES; i++)
        map->table[i].string_set = NULL;

    rwticket_init (&map->rw_lock);

    map->set_allocator = set_ll_init_allocator(shared);
    return map;
}

cct_cont_t *
cct_create_cont(cct_map_t *tables)
{ 
    // can be locally allocated
    cct_cont_t *container = RTmalloc(sizeof(cct_cont_t));
    container->map_index = 0;
    container->map = tables;
    return container;
}

static value_index_t
put_chunk(value_table_t vt,chunk item)
{
    table_t            *table = *((table_t **)vt);
    return set_ll_put(table->string_set, item.data, item.len);
}

static void
put_at_chunk(value_table_t vt,chunk item,value_index_t pos)
{
    table_t            *table = *((table_t **)vt);
    rwticket_wrlock (&table->map->rw_lock);
    set_ll_install(table->string_set, item.data, item.len, pos);
    rwticket_wrunlock (&table->map->rw_lock);
}

static chunk
get_chunk(value_table_t vt,value_index_t idx)
{
    table_t            *table = *((table_t **)vt);
    int                 len;
    const char         *data = set_ll_get(table->string_set, (int)idx, &len);
    return chunk_ld(len, (char *)data);
}

static int
get_count(value_table_t vt)
{
    table_t            *table = *((table_t **)vt);
    return set_ll_count(table->string_set);
}

static void *
new_map(void* context)
{
    cct_cont_t *cont = context;
    cct_map_t *map = cont->map;
    rwticket_rdlock (&map->rw_lock);
    if (cont->map_index >= MAX_TABLES)
        Abort("Chunk table limit reached: %zu.", MAX_TABLES);
    table_t *table = &map->table[cont->map_index++];
    rwticket_rdunlock (&map->rw_lock);
    if (!table->string_set) {
        rwticket_wrlock (&map->rw_lock);
        if (!table->string_set) {
            table->string_set = set_ll_create(map->set_allocator);
            table->map = map;
        }
        rwticket_wrunlock (&map->rw_lock);
    }
    return table;
}

value_table_t
cct_create_vt(cct_cont_t *ctx)
{
    cct_cont_t         *cont = ctx;
    value_table_t vt = VTcreateBase("CCT map", sizeof(table_t *));
    VTdestroySet(vt,NULL);
    VTputChunkSet(vt,put_chunk);
    VTputAtChunkSet(vt,put_at_chunk);
    VTgetChunkSet(vt,get_chunk);
    VTgetCountSet(vt,get_count);
    *((void **)vt) = new_map(cont);
    return vt;
}
