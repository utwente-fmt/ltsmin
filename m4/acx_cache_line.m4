#serial 1
# Author: Alfons Laarman <a.w.laarman@cs.utwente.nl>
#
# SYNOPSIS
# 
#   ACX_CLINE_DEF([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_CLINE_DEF],[
    AC_LANG_SAVE
    AC_LANG_C

    AC_RUN_IFELSE(
       [AC_LANG_SOURCE([[
/* BEGIN C-CODE */
// Author: Nick Strupat
// Date: October 29, 2010
// Returns the cache line size (in bytes) of the processor, or 0 on failure

#include <stddef.h>
size_t cache_line_size();

#if defined(__APPLE__)

#define _DARWIN_C_SOURCE
#include <sys/sysctl.h>

size_t cache_line_size() {
    size_t line_size = 0;
    size_t sizeof_line_size = sizeof(line_size);
    sysctlbyname("hw.cachelinesize", &line_size, &sizeof_line_size, 0, 0);
    return line_size;
}

#elif defined(_WIN32)

#include <stdlib.h>
#include <windows.h>
size_t cache_line_size() {
    size_t line_size = 0;
    DWORD buffer_size = 0;
    DWORD i = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION * buffer = 0;

    GetLogicalProcessorInformation(0, &buffer_size);
    buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(buffer_size);
    GetLogicalProcessorInformation(&buffer[0], &buffer_size);

    for (i = 0; i != buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i) {
        if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1) {
            line_size = buffer[i].Cache.LineSize;
            break;
        }
    }

    free(buffer);
    return line_size;
}

#elif defined(linux)

#include <math.h>
#include <stdio.h>

size_t cache_line_size() {
    FILE * p = 0;
    p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
    unsigned int i = 0;
    if (p) {
        fscanf(p, "%d", &i);
        fclose(p);
    }
    return i;
}

#else
size_t cache_line_size() {
    return 0;
}
#endif

/* return log2(cache_line_size) */
int main() {
    size_t cl = cache_line_size();
    size_t cl2 = log(cl)/log(2);
    if ((1UL << cl2) != cl)
        return 1;

    FILE *fp = fopen("conftest.data", "w");
    if (fp == NULL)
        return 1;
    
    fprintf(fp, "%zu\n", cl2);
    fclose(fp);
    return 0;
}
/* END C-CODE */
        ]])],
        [acx_cl2="`cat conftest.data`";echo "cache_line_size() returned: 2^$acx_cl2"],
        [acx_cl2=6;AC_MSG_WARN([Determining cache line size failed, using 2^6.])],
        [acx_cl2=6;AC_MSG_WARN([Cannot determine cache line size due to x-compilation, using 2^6.])]
    )

    acx_cl=$((1<<$acx_cl2))

    AC_DEFINE_UNQUOTED([CACHE_LINE], [$acx_cl2], [Log2 size of the machine's cache line])
    AC_DEFINE_UNQUOTED([CACHE_LINE_SIZE], [$acx_cl], [Size of the machine's cache line])
    AC_LANG_RESTORE

if test x"$acx_cl2" = x0; then
    $2
    :
else
    $1
    :
fi
])
