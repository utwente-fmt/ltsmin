#include <hre/config.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <hre/user.h>
#include <hre/unix.h>
#include <util-lib/util.h>

char *
gnu_basename (char *path)
{
    char *base = strrchr(path, '/');
    return base ? base+1 : path;
}

bool
has_prefix (const char *name, const char *prefix)
{
    return 0 == strncmp(name, prefix, strlen(prefix));
}

/** Fisher / Yates GenRandPerm*/
void
randperm (int *perm, int n, uint32_t seed)
{
    srandom (seed);
    for (int i=0; i<n; i++)
        perm[i] = i;
    for (int i=0; i<n; i++) {
        int                 j = random()%(n-i)+i;
        int                 t = perm[j];
        perm[j] = perm[i];
        perm[i] = t;
    }
}

int
char_array_search(char *args[], int length, char *key)
{
    for (int i = 0; i < length; i++) {
        if (strcmp(args[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

void
strtoupper(char *str, char *out, size_t outlen)
{
    for (size_t i = 0; i < outlen && str[i] != '\0'; i++) {
        out[i] = toupper (str[i]);
    }
}

char *
strupper(char *str)
{
    str = strdup(str);
    while( (*str=toupper(*str)) ) { str++; }
    return str;
}

ci_list *
ci_create (size_t size)
{
    return RTmallocZero (sizeof(int[size + 1]));
}

void
ci_free (ci_list *list)
{
    RTfree (list);
}

static inline void
ci_print_int (log_t log, ci_list *list)
{
    if (list->count > 0)
        Printf (log, "%d", ci_get(list, 0));
    for (int g = 1; g < list->count; g++) {
        Printf (log, ", %d", ci_get(list, g));
    }
}

void
ci_debug (ci_list *list)
{
    if (debug == NULL) return;
    ci_print_int (debug, list);
}

void
ci_print (ci_list *list)
{
    ci_print_int (info, list);
}

static int
compint (const void *a, const void *b, void *ctx)
{
    return *(int *)a - *(int *)b;
    (void) ctx;
}

void
ci_sort (ci_list *list)
{
    qsortr (list->data, list->count, sizeof(int), compint, NULL);
}
