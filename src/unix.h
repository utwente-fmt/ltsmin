#ifndef UNIX_H
#define UNIX_H

#include "config.h"
#include <stdlib.h>
#include "amconfig.h"

#if defined(HAVE_DECL_STRNDUP) && !HAVE_DECL_STRNDUP
extern char *strndup(const char *str, size_t n);
#endif

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16 OSSwapInt16
#define bswap_32 OSSwapInt32
#define bswap_64 OSSwapInt64
#elif defined(__linux__)
#include <byteswap.h>
#else
#error "Don't know how to deal with endianness on this platform."
#endif

#endif

