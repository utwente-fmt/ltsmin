#ifndef CONFIG_H
#define CONFIG_H

#define _GNU_SOURCE
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE

#define _XOPEN_SOURCE 600

#if defined(__linux__)
#include <features.h>
#define HAVE_STRNDUP
#endif

#ifndef DEFFILEMODE
#define DEFFILEMODE 0666
#endif

#define NAME_MAX 1024

#endif

