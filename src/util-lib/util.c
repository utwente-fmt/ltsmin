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
