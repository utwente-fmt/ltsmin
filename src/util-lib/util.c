#include <hre/config.h>

#include <ctype.h>
#include <limits.h>
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
has_suffix(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
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
    srand (seed);
    for (int i=0; i<n; i++)
        perm[i] = i;
    for (int i=0; i<n; i++) {
        int                 j = rand()%(n-i)+i;
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
compint (const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}

void
ci_sort (ci_list *list)
{
    qsort (list->data, list->count, sizeof(int), compint);
}

int long_mult_overflow(const long si_a, const long si_b) {
    if (si_a > 0) { /* si_a is positive */
        if (si_b > 0) { /* si_a and si_b are positive */
            if (si_a > (LONG_MAX / si_b)) return 1;
        } else { /* si_a positive, si_b nonpositive */
            if (si_b < (LONG_MIN / si_a)) return 1;
        } /* si_a positive, si_b nonpositive */
    } else { /* si_a is nonpositive */
        if (si_b > 0) { /* si_a is nonpositive, si_b is positive */
            if (si_a < (LONG_MIN / si_b)) return 1;
        } else { /* si_a and si_b are nonpositive */
            if ((si_a != 0) && (si_b < (LONG_MAX / si_a))) return 1;
        } /* End if si_a and si_b are nonpositive */
    } /* End if si_a is nonpositive */
    
    return 0;
}
