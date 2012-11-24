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
