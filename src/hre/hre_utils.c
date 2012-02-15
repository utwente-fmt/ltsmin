// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>

#include <stdlib.h>
#include <strings.h>

#include <hre/user.h>

char* HREstrdup(const char *str){
    if (str == NULL) return NULL;
    char *tmp = strdup (str);
    if (tmp == NULL) Abort("out of memory trying to get %d", strlen (str)+1);
    return tmp;
}


