AC_PREREQ(2.60)
AC_INIT([ltsmin], [1.0], 
        [ltsmin-support@cs.utwente.nl])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/ltsmin-mpi.c])
AC_CONFIG_HEADERS([src/amconfig.h])

# Checks for programs.
CFLAGS="-g -W -Wall -O2 -std=c99 $CFLAGS"
AM_PROG_CC_C_O
ACX_PTHREAD(
  [LIBS="$PTHREAD_LIBS $LIBS"
   CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
   CC="$PTHREAD_CC"],
  [AC_MSG_FAILURE([no pthread support found.])])
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_PROG_INSTALL
ACX_MPI(
  [CC="$MPICC"
   LIBS="$MPILIBS $LIBS"
   have_mpi=yes],
  [have_mpi=
   AC_MSG_WARN([no acceptable MPI support found.])])
AC_ARG_VAR([MCRL], [muCRL command])
AC_PATH_TOOL(MCRL, [${MCRL:-mcrl}], [""])
if test "x$MCRL" != x; then
  CPPFLAGS="-I$(dirname "$MCRL")/../mCRL/include $CPPFLAGS"
  LDFLAGS="-L$(dirname "$MCRL")/../mCRL/lib $LDFLAGS"
else
  AC_MSG_WARN([no acceptable mCRL installation found in \$PATH.])
fi
AC_CHECK_PROGS(ASCIIDOC, [asciidoc])
AC_CHECK_PROGS(XMLTO, [xmlto])
DX_DOXYGEN_FEATURE(ON)
DX_HTML_FEATURE(ON)
DX_CHM_FEATURE(OFF)
DX_CHI_FEATURE(OFF)
DX_MAN_FEATURE(OFF)
DX_RTF_FEATURE(OFF)
DX_XML_FEATURE(OFF)
DX_PDF_FEATURE(ON)
DX_PS_FEATURE(OFF)
DX_DOT_FEATURE(OFF)
DX_INIT_DOXYGEN([$PACKAGE_NAME])
AC_CACHE_SAVE

# Checks for libraries.
AC_CHECK_LIB([z], [inflateEnd],,
  [AC_MSG_FAILURE([zlib not found.])])
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_LIB([ATerm], [ATinit])
AC_CHECK_LIB([mcrlunix], [CreateDir])
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
