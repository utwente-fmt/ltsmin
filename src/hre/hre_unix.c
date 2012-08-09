#include <hre/config.h>
#include <string.h>

#include <hre/unix.h>

#if !defined(HAVE_STRNDUP) && !(defined(HAVE_DECL_STRNDUP) && HAVE_DECL_STRNDUP)
char *
strndup(const char *str, size_t n)
{
       size_t len;
       char *copy;

       if (!str)
               return (NULL);

       for (len = 0; len < n && str[len]; len++)
               continue;

       if (!(copy = RTmalloc(len + 1)))
               return (NULL);
       memcpy(copy, str, len);
       copy[len] = '\0';
       return copy;
}
#endif

