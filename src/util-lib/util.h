#ifndef UTIL_LTSMIN_H
#define UTIL_LTSMIN_H

#include <stdbool.h>

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

#endif // UTIL_LTSMIN_H

