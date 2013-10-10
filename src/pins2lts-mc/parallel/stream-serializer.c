/**
 *
 * Serializer
 *
 */


#include <hre/config.h>

#include <string.h>

#include <hre/user.h>
#include <pins2lts-mc/parallel/stream-serializer.h>

typedef struct serializer_s {
    action_f            action[NR_MODES]; // serializer/deserializer/initializer
    size_t              size;   // size of info in stream
    void               *ptr;    // info location
} serializer_t;

void
dummy_action (void *ctx, void *ptr, raw_data_t data)
{
    (void) ctx;
    (void) ptr;
    (void) data;
}

serializer_t *
serializer_create (action_f ser, action_f des, size_t size, void *ptr)
{
    serializer_t      *serializer = RTmalloc (sizeof(serializer_t));
    serializer->action[SERIALIZE] = ser;
    serializer->action[DESERIALIZE] = des;
    serializer->size = size;
    serializer->ptr = ptr;
    return serializer;
}

typedef struct simple_ctx_s {
    void           *ptr;
    size_t          size;
} simple_ctx_t;

static void
simple_ser (void *ctx, void *ptr, raw_data_t data)
{
    simple_ctx_t       *sctx = (simple_ctx_t *) ptr;
    memcpy (data, sctx->ptr, sctx->size);
    (void) ctx;
}

static void
simple_des (void *ctx, void *ptr, raw_data_t data)
{
    simple_ctx_t       *sctx = (simple_ctx_t *) ptr;
    memcpy (sctx->ptr, data, sctx->size);
    (void) ctx;
}

serializer_t *
serializer_simple (size_t size, void *ptr)
{
    serializer_t       *serializer = RTmalloc (sizeof(serializer_t));
    serializer->action[SERIALIZE] = simple_ser;
    serializer->action[DESERIALIZE] = simple_des;
    simple_ctx_t       *sctx = RTmalloc (sizeof(simple_ctx_t));
    sctx->ptr = ptr;
    sctx->size = size;
    serializer->size = size;
    serializer->ptr = sctx;
    return serializer;
}

typedef struct stream_item_s stream_item_t;

typedef void (*stream_recusife_f) (stream_item_t *stream, void *ctx,
                                   raw_data_t data, stream_mode_t index);

typedef struct stream_item_s {
    serializer_t       *serial;
    stream_item_t      *rec;
    stream_recusife_f   rec_f;
} stream_item_t;

static stream_item_t *
stream_item_create (serializer_t *serial, stream_recusife_f f)
{
    stream_item_t      *item = RTmalloc (sizeof(stream_item_t));
    item->serial = serial;
    item->rec = NULL;
    item->rec_f = f;
    return item;
}

static void
stream_walk_dummy (stream_item_t *item, void *extra,
                   raw_data_t data, stream_mode_t MODE)
{
    (void) item;
    (void) extra;
    (void) data;
    (void) MODE;
}

void
stream_list_walk (stream_item_t *item, void *ctx,
                  raw_data_t data, stream_mode_t MODE)
{
    item->serial->action[MODE] (ctx, item->serial->ptr, data);
    item->rec->rec_f (item->rec, ctx, ((char*)data) + item->serial->size, MODE);
}

typedef struct stream_list_s {
    stream_item_t     *head;
    stream_item_t     *tail;
    size_t             total_size;
} stream_list_t;

static stream_list_t *
stream_list_create ()
{
    stream_list_t      *list = RTmalloc (sizeof(stream_list_t));
    list->tail = stream_item_create (NULL, stream_walk_dummy);
    list->head = list->tail;
    list->total_size = 0;
    return list;
}

static void
stream_list_add (stream_list_t *list, serializer_t *serializer)
{
    stream_item_t       *item = stream_item_create (serializer, stream_list_walk);
    item->rec = list->head;
    list->head = item;
    list->total_size += serializer->size;
}

struct streamer_s {
    stream_list_t     *list;
};

streamer_t *
streamer_create ()
{
    streamer_t         *streamer = RTmalloc (sizeof(streamer_t));
    streamer->list = stream_list_create();
    return streamer;
}

void
streamer_add (streamer_t *streamer, serializer_t *serializer)
{
    stream_list_add (streamer->list, serializer);
}

void
streamer_walk (streamer_t *streamer, void *ctx, raw_data_t data,
               stream_mode_t MODE)
{
    stream_item_t* item = streamer->list->head;
    item->rec_f (item, ctx, data, MODE);
}

size_t
streamer_get_size (streamer_t *streamer)
{
    return streamer->list->total_size;
}
