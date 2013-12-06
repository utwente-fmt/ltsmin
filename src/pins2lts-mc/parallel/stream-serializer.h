/**
 *
 */

#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <stdlib.h>

typedef int                *raw_data_t; // TODO: chars in stack / queue

typedef void (*action_f)     (void *ctx, void *ptr, raw_data_t data);

typedef enum stream_mode_e {
    SERIALIZE   = 0,
    DESERIALIZE = 1,
    NR_MODES    = 2,
} stream_mode_t;

typedef struct streamer_s streamer_t;

extern streamer_t *streamer_create ();

extern void streamer_add_simple (streamer_t *streamer, size_t size, void *ptr);

extern void streamer_add (streamer_t *streamer, action_f ser, action_f des,
                          size_t size, void *ptr);

extern void streamer_walk (streamer_t *streamer, void *ctx, raw_data_t data,
                           stream_mode_t MODE);

extern size_t streamer_get_size (streamer_t *streamer);

#endif // SERIALIZER_H
