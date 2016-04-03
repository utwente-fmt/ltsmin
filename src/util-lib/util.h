#ifndef UTIL_LTSMIN_H
#define UTIL_LTSMIN_H

#include <stdbool.h>
#include <stdint.h>
#include <hre/user.h>

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

/**
 * ci = count, integer
 */
typedef struct ci_list
{
    int count;
    int data[];
} ci_list;

extern ci_list *ci_create (size_t size);

extern void ci_free (ci_list *list);

extern void ci_print (ci_list *list);

extern void ci_debug (ci_list *list);

extern void ci_sort (ci_list *list);

static inline int
ci_get (ci_list *list, int index)
{
    return list->data[index];
}

/* begin iterator */
static inline int *
ci_begin (ci_list *list)
{
    return &list->data[0];
}

static inline int *
ci_end (ci_list *list)
{
    return &list->data[list->count];
}
/* end iterator */

static inline int
ci_top (ci_list *list)
{
    HREassert(list->count >= 0);
    return list->data[list->count - 1];
}

static inline int
ci_pop (ci_list *list)
{
    HREassert(list->count >= 0);
    return list->data[--list->count];
}

static inline int
ci_count (ci_list *list)
{
    return list->count;
}

static inline void
ci_clear (ci_list *list)
{
    list->count = 0;
}

static inline void
ci_add (ci_list *list, int num)
{
    list->data[list->count++] = num;
}

static inline int
ci_binary_search (ci_list *list, int key)
{
    int             imin = 0;
    int             imax = list->count - 1;
    while (imax >= imin) {
        int imid = imin + ((imax - imin) >> 1);
        if (list->data[imid] == key) {
            return imid;
        } else if (list->data[imid] < key) {
            imin = imid + 1;
        } else {
            imax = imid - 1;
        }
    }
    return -1;
}

static inline void
ci_add_if (ci_list *list, int num, int condition)
{
    list->data[list->count] = num;
    list->count += condition != 0;
}

static inline void
list_invert (ci_list *list)
{
    for (int i = 0; i < list->count / 2; i++) {
        swap (list->data[i], list->data[list->count - i - 1]);
    }
}



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

extern int long_mult_overflow(const long si_a, const long si_b);

#endif // UTIL_LTSMIN_H

