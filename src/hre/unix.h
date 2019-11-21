// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef UNIX_H
#define UNIX_H

#include <stdlib.h>

#if defined(HAVE_DECL_STRNDUP) && !HAVE_DECL_STRNDUP
extern char *strndup(const char *str, size_t n);
#endif
#if defined(HAVE_DECL_ASPRINTF) && !HAVE_DECL_ASPRINTF
extern int asprintf(char **ret, const char *format, ...);
#endif
#if defined(HAVE_DECL_STRSEP) && !HAVE_DECL_STRSEP
extern char *strsep(char **stringp, const char *delim);
#endif
#if defined(HAVE_DECL_MKDTEMP) && !HAVE_DECL_MKDTEMP
extern char *mkdtemp(char *);
#endif

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16 OSSwapInt16
#define bswap_32 OSSwapInt32
#define bswap_64 OSSwapInt64
#define hton_16(x) OSSwapHostToBigInt16(x)
#define ntoh_16(x) OSSwapBigToHostInt16(x)
#define hton_32(x) OSSwapHostToBigInt32(x)
#define ntoh_32(x) OSSwapBigToHostInt32(x)
#define hton_64(x) OSSwapHostToBigInt64(x)
#define ntoh_64(x) OSSwapBigToHostInt64(x)
#elif defined(__linux__) || defined(__CYGWIN__)
#include <endian.h>
#include <byteswap.h>
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define hton_16(x) bswap_16(x)
#  define ntoh_16(x) bswap_16(x)
#  define hton_32(x) bswap_32(x)
#  define ntoh_32(x) bswap_32(x)
#  define hton_64(x) bswap_64(x)
#  define ntoh_64(x) bswap_64(x)
# else
#  define hton_16(x) (x)
#  define ntoh_16(x) (x)
#  define hton_32(x) (x)
#  define ntoh_32(x) (x)
#  define hton_64(x) (x)
#  define ntoh_64(x) (x)
# endif
#elif defined(_WIN32)
#define bswap_16 __builtin_bswap16
#define bswap_32 __builtin_bswap32
#define bswap_64 __builtin_bswap64
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define hton_16(x) __builtin_bswap16(x)
#  define ntoh_16(x) __builtin_bswap16(x)
#  define hton_32(x) __builtin_bswap32(x)
#  define ntoh_32(x) __builtin_bswap32(x)
#  define hton_64(x) __builtin_bswap64(x)
#  define ntoh_64(x) __builtin_bswap64(x)
# else
#  define hton_16(x) (x)
#  define ntoh_16(x) (x)
#  define hton_32(x) (x)
#  define ntoh_32(x) (x)
#  define hton_64(x) (x)
#  define ntoh_64(x) (x)
# endif
#elif defined(__NetBSD__)
#define bswap_16 bswap16
#define bswap_32 bswap32
#define bswap_64 bswap64
#define hton_16(x) htobe16(x)
#define ntoh_16(x) be16toh(x)
#define hton_32(x) htobe32(x)
#define ntoh_32(x) be32toh(x)
#define hton_64(x) htobe64(x)
#define ntoh_64(x) be64toh(x)
#else
#error "Don't know how to deal with endianness on this platform."
#endif

/**
 * Spin lock implementation (for incomplete pthread libraries as on OSX)
 */

#if defined(__APPLE__)
#ifndef PTHREAD_SPIN_LOCK_SHIM
#define PTHREAD_SPIN_LOCK_SHIM


#include <mc-lib/atomics.h>


typedef int pthread_spinlock_t;

#ifndef PTHREAD_PROCESS_SHARED
# define PTHREAD_PROCESS_SHARED 1
#endif
#ifndef PTHREAD_PROCESS_PRIVATE
# define PTHREAD_PROCESS_PRIVATE 2
#endif

static inline int
pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
    (void) pshared;
    compile_barrier ();
    *lock = 0;
    return 0;
}

static inline int
pthread_spin_destroy (pthread_spinlock_t *lock)
{
    (void) lock;
    return 0;
}

static inline int
pthread_spin_lock (pthread_spinlock_t *lock)
{
    while (1) {
        int i;
        for (i=0; i < 10000; i++) {
            if (atomic_read(lock) == 0) {
                if (cas(lock, 0, 1)) {
                    return 0;
                }
            }
        }

        mfence ();
    }
}

static inline int
pthread_spin_trylock (pthread_spinlock_t *lock)
{
    if (cas(lock, 0, 1)) {
        return 0;
    }
    return EBUSY;
}

static inline int
pthread_spin_unlock (pthread_spinlock_t *lock)
{
    compile_barrier ();
    *lock = 0;
    return 0;
}

#endif
#endif

#endif

