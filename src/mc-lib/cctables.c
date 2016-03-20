#include <hre/config.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/atomics.h>
#include <mc-lib/cctables.h>
#include <mc-lib/set-ll.h>


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



struct cct_map_s {
    ticket_rwlock_t         rw_lock;
    bool                    shared;   // shared or private allocation
    value_table_t          *table;    // list of tables (pointers)
    set_ll_allocator_t     *set_allocator;
};


cct_map_t *
cct_create_map (bool shared)
{
    RTswitchAlloc (shared); // global shared-memory allocation?
    cct_map_t              *map = RTmalloc(sizeof(cct_map_t));
    map->shared = shared;
    map->table = RTmallocZero (sizeof(value_table_t[MAX_TABLES]));
    RTswitchAlloc (false);

    rwticket_init (&map->rw_lock);

    map->set_allocator = set_ll_init_allocator(shared);
    return map;
}


/**
\typedef a chunk table
use malloc
*/
struct value_table_s {
    set_ll_t               *string_set;
    cct_map_t              *map;
};

size_t
cct_print_stats (log_t log, log_t details, lts_type_t ltstype, cct_map_t *map)
{
    size_t                  i = 0;
    size_t                  total = 0;
    total += set_ll_print_alloc_stats(log, map->set_allocator);
    while (map->table[i] != NULL) {
        char                   *name = lts_type_get_type(ltstype, i);
        total += set_ll_print_stats(details, map->table[i]->string_set, name);
        i++;
    }
    Warning (log, "Total memory used for chunk indexing: %zuMB", total >> 20);
    return total;
}

static value_index_t
put_chunk (value_table_t vt,chunk item)
{
    return set_ll_put(vt->string_set, item.data, item.len);
}

static void
put_at_chunk (value_table_t vt,chunk item,value_index_t pos)
{
    rwticket_wrlock (&vt->map->rw_lock);
    set_ll_install(vt->string_set, item.data, item.len, pos);
    rwticket_wrunlock (&vt->map->rw_lock);
}

static chunk
get_chunk (value_table_t vt,value_index_t idx)
{
    int                 len;
    const char         *data = set_ll_get(vt->string_set, (int)idx, &len);
    return chunk_ld(len, (char *)data);
}

static int
get_count (value_table_t vt)
{
    return set_ll_count(vt->string_set);
}

struct table_iterator_s {
    set_ll_iterator_t     *it;
};

static chunk it_next (table_iterator_t it){
    chunk               c;
    c.data = set_ll_iterator_next (it->it, (int *)&c.len);
    return c;
}

static int it_has_next (table_iterator_t it){
    return set_ll_iterator_has_next (it->it);
}

static table_iterator_t
iterator_create (value_table_t vt) {
    table_iterator_t it = ITcreateBase (sizeof (struct table_iterator_s));
    it->it = set_ll_iterator (vt->string_set);
    ITnextSet (it, it_next);
    IThasNextSet (it, it_has_next);
    return it;
}


struct table_factory_s {
    size_t                  map_index;
    cct_map_t              *map;            //shared
};

static value_table_t
cct_create_vt (table_factory_t tf, int index)
{
    cct_map_t              *map = tf->map;
    RTswitchAlloc (map->shared);
    value_table_t           vt = VTcreateBase ("Concurrent chunk map",
                                               sizeof(struct value_table_s));
    RTswitchAlloc (false);
    VTdestroySet(vt,NULL);
    VTputChunkSet(vt,put_chunk);
    VTputAtChunkSet(vt,put_at_chunk);
    VTgetChunkSet(vt,get_chunk);
    VTgetCountSet(vt,get_count);
    VTiteratorSet(vt,iterator_create);
    vt->string_set = set_ll_create (map->set_allocator, index);
    vt->map = map;
    return vt;
}

static value_table_t
new_map (table_factory_t tf)
{
    cct_map_t              *map = tf->map;
    rwticket_rdlock (&map->rw_lock);
    if (tf->map_index >= MAX_TABLES)
        Abort("Chunk table limit reached: %zu.", MAX_TABLES);
    rwticket_rdunlock (&map->rw_lock);
    size_t                  index = tf->map_index++;
    if (!map->table[index]) {
        rwticket_wrlock (&map->rw_lock);
        if (!map->table[index]) {
            map->table[index] = cct_create_vt (tf, index);
        }
        rwticket_wrunlock (&map->rw_lock);
    }
    return map->table[index];
}


table_factory_t
cct_create_table_factory (cct_map_t *map)
{
    // can be locally allocated
    table_factory_t         factory = TFcreateBase (sizeof(struct table_factory_s));
    TFnewTableSet (factory, new_map);
    factory->map_index = 0;
    factory->map = map;
    return factory;
}

