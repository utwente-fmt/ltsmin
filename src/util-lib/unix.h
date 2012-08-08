#ifndef UNIX_H
#define UNIX_H

#include <stdlib.h>
#include <stdio.h>

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
#elif defined(__linux__) || defined(__CYGWIN__)
#include <byteswap.h>
#elif defined(__NetBSD__)
#define bswap_16 bswap16
#define bswap_32 bswap32
#define bswap_64 bswap64
#else
#error "Don't know how to deal with endianness on this platform."
#endif

extern void qsortr(void *base, size_t num, size_t width,
                   int (*comp)(const void *, const void *,void *ExtraArgs),
                   void *ExtraArgs);

#endif

