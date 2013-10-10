/**
 *
 */

#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <stdlib.h>

typedef int                *raw_data_t; // TODO: chars in stack / queue

typedef void (*action_f)     (void *ctx, void *ptr, raw_data_t data);


typedef struct serializer_s serializer_t;

extern void dummy_action (void *ctx, void *ptr, raw_data_t data);

extern serializer_t *serializer_create (action_f ser, action_f des,
                                        size_t size, void *ptr);

extern serializer_t *serializer_simple (size_t size, void *ptr);

typedef enum stream_mode_e {
    SERIALIZE   = 0,
    DESERIALIZE = 1,
    NR_MODES    = 2,
} stream_mode_t;

typedef struct streamer_s streamer_t;

extern streamer_t *streamer_create ();

extern void streamer_add (streamer_t *streamer, serializer_t *serializer);

extern void streamer_walk (streamer_t *streamer, void *ctx, raw_data_t data,
                           stream_mode_t MODE);

extern size_t streamer_get_size (streamer_t *streamer);

#endif // SERIALIZER_H
