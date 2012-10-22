#ifndef UTIL_SMALL_H
#define UTIL_SMALL_H

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

#endif // UTIL_SMALL_H

