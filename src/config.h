
#ifndef CONFIG_H
#define CONFIG_H

#define _GNU_SOURCE
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE

#define _XOPEN_SOURCE 600

#ifdef __linux__
#include <features.h>
#endif

#ifndef DEFFILEMODE
#define DEFFILEMODE 0666
#endif

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16 OSSwapInt16
#define bswap_32 OSSwapInt32
#define bswap_64 OSSwapInt64
#elif defined(__linux__)
#include <byteswap.h>
#define HAVE_STRNDUP
#else
#error "Don't know how to deal with endianness on this platform."
#endif

#define NAME_MAX 1024

#endif

