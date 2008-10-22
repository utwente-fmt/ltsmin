AC_PREREQ(2.61)
AC_INIT([LTSmin], [1.0], 
        [ltsmin-support@cs.utwente.nl],
        [ltsmin])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/ltsmin-mpi.c])
AC_CONFIG_HEADERS([src/config.h])

# Checks for programs.
AM_PROG_CC_C_O
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_PROG_INSTALL
ACX_MPI()
AC_ARG_VAR([MCRL], [muCRL command])
AC_PATH_TOOL(MCRL, [mcrl], [])
if test "$MCRL" == ""; then
  AC_MSG_ERROR(mCRL installation not found.)
fi
AC_PATH_PROG(ASCIIDOC, [asciidoc])
AC_PATH_PROG(XMLTO, [xmlto])
AC_CACHE_SAVE

# Checks for libraries.
CFLAGS="-W -Wall -pthread -std=c99"
CPPFLAGS="-I$(dirname $MCRL)/../mCRL/include $CPPFLAGS"
LDFLAGS="-L$(dirname $MCRL)/../mCRL/lib $LDFLAGS"
AC_CHECK_LIB([ATerm], [ATinit],,
  [AC_MSG_FAILURE([libATerm not found.  Remember to use the same
  library that mcrl is linked against.])])
AC_CHECK_LIB([z], [inflateEnd],,
  [AC_MSG_FAILURE([zlib not found.])])
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_LIB([mcrlunix], [main],,
  [AC_MSG_FAILURE([libmcrlunix not found.])])
AC_CHECK_LIB([mcrl], [MCRLsetArguments],,
  [AC_MSG_FAILURE([libmcrl not found.])])
AC_CHECK_LIB([step], [STsetArguments],,
  [AC_MSG_FAILURE([libstep not found.])])
AC_CHECK_LIB([rt], [aio_error])

# Checks for header files.
AH_TOP(
[#ifndef LTSMIN_CONFIG_H
#define LTSMIN_CONFIG_H

#define _GNU_SOURCE
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE

#define _XOPEN_SOURCE 600

#if defined(__linux__)
#include <features.h>
#endif

#ifndef DEFFILEMODE
#define DEFFILEMODE 0666
#endif

#define NAME_MAX 1024
])
AH_BOTTOM([#endif /* LTSMIN_CONFIG_H */])

AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stdint.h stdlib.h string.h strings.h unistd.h])
AC_CACHE_SAVE

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRERROR_R
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([atexit bzero ftruncate strchr strdup strndup strstr strtol])

AC_CONFIG_FILES([
       Makefile
       src/Makefile
       doc/Makefile
])
AC_OUTPUT
