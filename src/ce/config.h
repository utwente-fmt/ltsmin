#ifndef CONFIG_H
#define CONFIG_H

#if defined(SUN)
#define u_int32_t uint32_t
#define u_int64_t uint64_t
#define isblank isspace
#endif

#ifdef SGI
#define isblank __isblank
#define u_int64_t uint64_t
#endif

#define MAX_TERM_LEN 5000

#endif

