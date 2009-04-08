AC_PREREQ(2.60)
AC_INIT([ltsmin], [1.2.1],
        [ltsmin-support@cs.utwente.nl])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autotools])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_SRCDIR([src/ltsmin-mpi.c])
AC_CONFIG_HEADERS([src/amconfig.h])

# Checks for programs.
CFLAGS="-g -O2 $CFLAGS"
CXXFLAGS="-g -O2 -Wno-inline -ftemplate-depth-128 -finline-functions $CXXFLAGS"
AM_PROG_CC_C_O
AC_PROG_CXX
ACX_FINK
ACX_MACPORTS
ACX_PTHREAD(
  [LIBS="$PTHREAD_LIBS $LIBS"
   CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
   CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"
   CC="$PTHREAD_CC"],
  [AC_MSG_FAILURE([no pthread support found.])])
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AM_PROG_LIBTOOL

ACX_MPI(
  [CC="$MPICC"
   LIBS="$MPILIBS $LIBS"
   AC_ARG_VAR(MPICXX, [MPI C++ compiler command])
   AC_CHECK_PROGS(MPICXX, [mpic++ mpicxx mpiCC hcp mpxlC_r mpxlC mpCC cmpic++, $CXX])
   AC_SUBST(MPICXX)
   ltsmin_save_CXX="$CXX"
   CXX="$MPICXX"
   AC_LANG_PUSH([C++])
   AC_TRY_LINK(
     [#include <mpi.h>],
     [MPI::Init();],
     [have_mpi=yes],
     [have_mpi=
      CXX="$ltsmin_save_CXX"
      AC_MSG_WARN([no acceptable MPI C++ support found.])])
   AC_LANG_POP([C++])],
  [have_mpi=
   AC_MSG_WARN([no acceptable MPI support found.])])

AC_PROG_CC_C99
AS_IF([test x"$ac_cv_prog_cc_c99" = xno],
  [AC_MSG_FAILURE([no acceptable C99 compiler found.])])
AX_TRY_CFLAGS_IFELSE([-W -Wall])
AX_TRY_CFLAGS_IFELSE([-pipe])
AX_TRY_CXXFLAGS_IFELSE([-W -Wall])
AX_TRY_CXXFLAGS_IFELSE([-pipe])

AX_CC_COMPILE_CXX_LINK

ACX_MCRL([:],
  [AC_MSG_WARN([no acceptable muCRL installation found.])])
ACX_MCRL2([:],
  [AC_MSG_WARN([no acceptable mCRL2 installation found.])])
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
AC_CHECK_LIB([popt],[poptGetContext],,[AC_MSG_FAILURE([popt not found.])])
AC_CHECK_LIB([z], [inflateEnd],,
  [AC_MSG_FAILURE([zlib not found.])])
if test "x$acx_mcrl" = xyes; then
  ACX_MCRL_LIBS(
    [AX_LET([LIBS], ["$MCRL_LIBS $LIBS"],
            [LDFLAGS], ["$MCRL_LDFLAGS $LDFLAGS"],
       [AC_CHECK_FUNCS([STgetSummandCount STgetProjection],
          [have_libstep_info=yes],
          [AC_MSG_WARN([muCRL library too old; some tools will not be built.])
           have_libstep_info=no
           break])])],
    [acx_mcrl=no
     AC_MSG_WARN([some muCRL libraries not found.])])
fi
if test "x$acx_mcrl2" = xyes; then
  ACX_MCRL2_LIBS(
    [:],
    [acx_mcrl2=no
     AC_MSG_WARN([mcrl2 library not found.])])
fi
AC_SUBST([NIPS_LIBS],['${top_builddir}/nips-vm/libnips_vm.la'])
AC_SUBST([NIPS_CPPFLAGS],['-I${top_srcdir}'])
AC_SUBST([NIPS_LDFLAGS])

AC_SUBST([BUDDY_LIBS],['${top_builddir}/buddy/src/libbdd.la -lm'])
AC_SUBST([BUDDY_CPPFLAGS],['-I${top_srcdir}/buddy/src'])
AC_SUBST([BUDDY_LDFLAGS])

AC_CHECK_LIB([rt], [aio_error])
ACX_CADP_BCG_WRITE([have_bcg=yes],
  [have_bcg=no
   AC_MSG_WARN([no acceptable BCG libraries found; building without.])])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stdint.h stdlib.h string.h strings.h unistd.h])
AC_CHECK_HEADER([popt.h],[], [AC_MSG_FAILURE([popt.h not found.])])
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
AC_CHECK_FUNCS([atexit bzero ftruncate strdup strndup strchr strstr strtol])
AC_CHECK_DECLS([strdup, strndup, strchr, strstr],,,[
#include "src/config.h"
AC_INCLUDES_DEFAULT])

AM_CONDITIONAL([HAVE_MPI], [test x"$have_mpi" != "x"])
AM_CONDITIONAL([HAVE_LIBMCRL], [test x"$acx_mcrl" = xyes])
AM_CONDITIONAL([HAVE_LIBMCRL2], [test x"$acx_mcrl2" = xyes])
AM_CONDITIONAL([HAVE_LIBSTEP_INFO], [test x"$have_libstep_info" = xyes])
AM_CONDITIONAL([HAVE_ASCIIDOC], [test x"$ASCIIDOC" != x])
AM_CONDITIONAL([HAVE_XMLTO], [test x"$XMLTO" != x])
AM_CONDITIONAL([HAVE_LIBBCG], [test x"$have_bcg" = xyes])

AC_CONFIG_FILES([
       Makefile
       src/Makefile
       doc/Makefile
])
ac_configure_args="$ac_configure_args --enable-keep-terminated-procs \
                                      --disable-shared"
AC_CONFIG_SUBDIRS([nips-vm buddy])
AC_OUTPUT

cat <<EOM
Configuration:
======================================================================
    CC       = ${CC:-NOT FOUND}
    MPICC    = ${MPICC:-NOT FOUND, building without MPI support}
    CXX      = ${CXX:-NOT FOUND}
    MPICXX   = ${MPICXX:-NOT FOUND, building without MPI support}
    MCRL     = ${with_mcrl:-NOT FOUND, building without muCRL support}
    MCRL2    = ${with_mcrl2:-NOT FOUND, building without mCRL2 support}
    CADP     = ${CADP:-NOT FOUND, building without CADP support}

    ASCIIDOC = ${ASCIIDOC:-NOT FOUND, not building documentation}
    XMLTO    = ${XMLTO:-NOT FOUND, not building manpages}
======================================================================
EOM
