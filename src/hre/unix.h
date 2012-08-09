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

extern void qsortr(void *base, size_t num, size_t width,
                   int (*comp)(const void *, const void *,void *ExtraArgs),
                   void *ExtraArgs);

#endif

