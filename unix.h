#ifndef UNIX_H
#define UNIX_H

#include <stdlib.h>
#include "config.h"

#if !defined(HAVE_STRNDUP)
extern char *strndup(const char *str, size_t n);
#endif

#endif
