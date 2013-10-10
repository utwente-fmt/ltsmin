#ifndef UTIL_LTSMIN_H
#define UTIL_LTSMIN_H

#include <stdbool.h>
#include <stdint.h>

#define max(a,b) ({ \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _a > _b ? _a : _b; \
})

#define min(a,b) ({ \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _a < _b ? _a : _b; \
})

#define swap(a,b) ({ \
    typeof(a) tmp = a; \
    a = b; \
    b = tmp; \
})

extern char *gnu_basename (char *path);

extern bool has_prefix (const char *name, const char *prefix);

extern void randperm (int *perm, int n, uint32_t seed);

extern int char_array_search (char *args[], int length, char *key);

extern void strtoupper (char *str, char *out, size_t outlen);

extern char *strupper(char *str);

static inline size_t
INT_SIZE (size_t size)
{
    return (size + 3) / 4;
}

#endif // UTIL_LTSMIN_H

