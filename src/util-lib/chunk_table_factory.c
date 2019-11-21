#include <hre/config.h>

#include <hre/stringindex.h>
#include <hre/user.h>
#include <util-lib/tables.h>
#include <util-lib/chunk_table_factory.h>


struct table_factory_s {
    size_t              user_size;
    tf_new_map_t        new_map;
};

static const size_t system_size = ((sizeof(struct table_factory_s)+7)/8)*8;
#define SYS2USR(var) ((table_factory_t)(((char*)(var))+system_size))
#define USR2SYS(var) ((table_factory_t)(((char*)(var))-system_size))


value_table_t
TFnewTable (table_factory_t tf)
{
    table_factory_t object = (table_factory_t) USR2SYS(tf);
    return object->new_map (tf);
}

void
TFnewTableSet (table_factory_t tf, tf_new_map_t method)
{
    table_factory_t object = (table_factory_t) USR2SYS(tf);
    object->new_map = method;
}

table_factory_t
TFcreateBase (size_t user_size)
{
    table_factory_t object=(table_factory_t) RTmallocZero(system_size+user_size);
    object->user_size = user_size;
    return SYS2USR(object);
}

static void
destroy(value_table_t vt)
{
    SIdestroy((string_index_t*)vt);
}

static value_index_t
put_chunk(value_table_t vt,chunk item)
{
    string_index_t si=*((string_index_t*)vt);
    return (value_index_t)SIputC(si,item.data,item.len);
}

static void
put_at_chunk(value_table_t vt,chunk item, value_index_t pos)
{
    string_index_t si=*((string_index_t*)vt);
    SIputCAt(si,item.data,item.len,pos);
}

static chunk
get_chunk(value_table_t vt,value_index_t idx)
{
    string_index_t si=*((string_index_t*)vt);
    int len;
    char*data=SIgetC(si,(int)idx,&len);
    return chunk_ld(len,data);
}

static int
get_count (value_table_t vt)
{
    string_index_t si=*((string_index_t*)vt);
    return SIgetCount(si);
}


struct table_iterator_s {
    int                 index;
    int                 count;
    value_table_t       vt;
};

static chunk
simple_next (table_iterator_t it)
{
    HREassert (it->index < it->count, "Table iterator out of bounds %d >= %d", it->index, it->count);
    return VTgetChunk (it->vt, it->index++);
}

static int
simple_has_next (table_iterator_t it)
{
    return it->index < it->count;
}

table_iterator_t
simple_iterator_create (value_table_t vt)
{
    table_iterator_t it = ITcreateBase (sizeof (struct table_iterator_s));
    it->vt = vt;
    it->index = 0;
    it->count = VTgetCount (vt);
    ITnextSet (it, simple_next);
    IThasNextSet (it, simple_has_next);
    return it;
}


struct value_table_s {
    string_index_t      si;
};

value_table_t
simple_chunk_table_create (void *context, char *type_name)
{
    (void)context;
    value_table_t vt = VTcreateBase(type_name, sizeof(struct value_table_s));
    vt->si = SIcreate();
    VTdestroySet(vt,destroy);
    VTputChunkSet(vt,put_chunk);
    VTputAtChunkSet(vt,put_at_chunk);
    VTgetChunkSet(vt,get_chunk);
    VTgetCountSet(vt,get_count);
    VTiteratorSet(vt,simple_iterator_create);
    return vt;
}

static value_table_t
create (table_factory_t context)
{
    return simple_chunk_table_create (context, "Simple table");
}

table_factory_t
simple_table_factory_create ()
{
    table_factory_t factory = TFcreateBase (0);
    TFnewTableSet (factory, create);
    return factory;
}

