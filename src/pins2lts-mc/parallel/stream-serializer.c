/**
 *
 * Serializer
 *
 */


#include <hre/config.h>

#include <string.h>

#include <hre/user.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/stream-serializer.h>

#define         MAX_SERIALIZERS 20


typedef struct serializer_s serializer_t;

struct serializer_s {
    action_f            action; // serializer/deserializer/initializer
    size_t              size;   // size of info in stream
    void               *ptr;    // info location
};


typedef struct simple_ctx_s {
    void           *ptr;
    size_t          size;
} simple_ctx_t;

void
simple_ser (void *ctx, void *ptr, raw_data_t data)
{
    simple_ctx_t       *sctx = (simple_ctx_t *) ptr;
    memcpy (data, sctx->ptr, sctx->size);
    (void) ctx;
}

void
simple_des (void *ctx, void *ptr, raw_data_t data)
{
    simple_ctx_t       *sctx = (simple_ctx_t *) ptr;
    memcpy (sctx->ptr, data, sctx->size);
    (void) ctx;
}

struct streamer_s {
    serializer_t        list[NR_MODES][MAX_SERIALIZERS];
    size_t              length[NR_MODES];
    size_t              total_size;
};

static void
stream_list_add (streamer_t *s, stream_mode_t MODE,
                 action_f a, size_t size, void *ptr)
{
    if (a == NULL)
        return;
    size_t              len = s->length[MODE];
    HREassert (len < MAX_SERIALIZERS);
    s->list[MODE][len].action = a;
    s->list[MODE][len].size = size / SLOT_SIZE;
    s->list[MODE][len].ptr = ptr;
    s->length[MODE]++;
}

streamer_t *
streamer_create ()
{
    streamer_t         *streamer = RTmallocZero (sizeof(streamer_t));
    for (size_t i = 0; i < NR_MODES; i++) {
        streamer->length[i] = 0;
    }
    streamer->total_size = 0;
    return streamer;
}

void
streamer_add (streamer_t *streamer, action_f ser, action_f des,
              size_t size, void *ptr)
{
    stream_list_add (streamer, SERIALIZE, ser, size, ptr);
    stream_list_add (streamer, DESERIALIZE, des, size, ptr);
    streamer->total_size += size;
}

void
streamer_add_simple (streamer_t *streamer, size_t size, void *ptr)
{
    simple_ctx_t *ctx = RTmalloc (sizeof (simple_ctx_t));
    ctx->ptr = ptr;
    ctx->size = size;
    streamer_add (streamer, simple_ser, simple_des, size, ctx);
}


void
streamer_walk (streamer_t *streamer, void *ctx, raw_data_t data,
               stream_mode_t MODE)
{
    size_t              len = streamer->length[MODE];
    for (size_t i = 0; i < len; i++) {
        serializer_t       *serializer = &streamer->list[MODE][i];
        serializer->action (ctx, serializer->ptr, data);
        data += serializer->size;
    }
}

size_t
streamer_get_size (streamer_t *streamer)
{
    return streamer->total_size;
}
