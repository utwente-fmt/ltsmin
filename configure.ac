AC_PREREQ(2.61)
AC_INIT([LTSmin], [1.0], 
        [ltsmin-support@cs.utwente.nl],
        [ltsmin])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/ltsmin-mpi.c])
AC_CONFIG_HEADERS([src/amconfig.h])

# Checks for programs.
AM_PROG_CC_C_O
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_PROG_INSTALL
ACX_MPI(
  [CC="$MPICC"
   have_mpi=yes],
  [have_mpi=])
AC_ARG_VAR([MCRL], [muCRL command])
AC_PATH_TOOL(MCRL, [mcrl])
if test "x$MCRL" = "x"; then
  AC_MSG_WARN([mCRL installation not found.])
fi
AC_PATH_PROG(ASCIIDOC, [asciidoc])
AC_PATH_PROG(XMLTO, [xmlto])
AC_CACHE_SAVE

# Checks for libraries.
CFLAGS="-W -Wall -O2 -pthread -std=c99 $CFLAGS"
CPPFLAGS="-I$(dirname $MCRL)/../mCRL/include $CPPFLAGS"
LDFLAGS="-L$(dirname $MCRL)/../mCRL/lib $LDFLAGS"
LIBS="$MPILIBS $LIBS"
AC_CHECK_LIB([z], [inflateEnd],,
  [AC_MSG_FAILURE([zlib not found.])])
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_LIB([ATerm], [ATinit])
AC_CHECK_LIB([mcrlunix], [main])
AC_CHECK_LIB([mcrl], [MCRLsetArguments],
  [LIBS="-lmcrl $LIBS"
   have_mcrl=yes],
  [have_mcrl=])
AC_CHECK_LIB([step], [STsetArguments])
AC_CHECK_LIB([rt], [aio_error])

# Checks for header files.
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

AM_CONDITIONAL([HAVE_MPI], [test "x$have_mpi" != "x"])
AM_CONDITIONAL([HAVE_LIBMCRL], [test "x$have_mcrl" != x])
AM_CONDITIONAL([HAVE_ASCIIDOC], [test "x$ASCIIDOC" != x])
AM_CONDITIONAL([HAVE_XMLTO], [test "x$XMLTO" != x])

AC_CONFIG_FILES([
       Makefile
       src/Makefile
       doc/Makefile
])
AC_OUTPUT

cat <<EOM
Configuration:
======================================================================
    CC       = ${CC:-NOT FOUND}
    MPICC    = ${MPICC:-NOT FOUND, building without MPI support}
    MCRL     = ${MCRL:-NOT FOUND, building without mCRL support}

    ASCIIDOC = ${ASCIIDOC:-NOT FOUND, not building documentation}
    XMLTO    = ${XMLTO:-NOT FOUND, not building manpages}
======================================================================
EOM
