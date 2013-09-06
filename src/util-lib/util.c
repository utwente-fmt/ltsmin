#include <hre/config.h>

#include <stdlib.h>
#include <string.h>

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
