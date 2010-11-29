AC_PREREQ(2.60)
AC_INIT([ltsmin],[1.6],[ltsmin-support@cs.utwente.nl])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autotools])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_SRCDIR([src/spec-reach.c])
AC_CONFIG_HEADERS([src/config.h])

AC_SUBST([pkglibdir], "\${libdir}/${PACKAGE_NAME}-${PACKAGE_VERSION}")

AC_USE_SYSTEM_EXTENSIONS
AH_VERBATIM([_XOPEN_SOURCE],[
/* Define if you have POSIX and XPG specifications */
#ifndef _XOPEN_SOURCE
# undef _XOPEN_SOURCE
#endif])
AC_DEFINE([_XOPEN_SOURCE], 600L)
AH_VERBATIM([_LARGEFILE_SOURCE],[
/* Define for large file support */
#ifndef _LARGEFILE_SOURCE
# undef _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
# undef _LARGEFILE64_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
# undef _FILE_OFFSET_BITS
#endif
])
AC_DEFINE([_LARGEFILE_SOURCE])
AC_DEFINE([_LARGEFILE64_SOURCE])
AC_DEFINE([_FILE_OFFSET_BITS], 64)

AC_DEFINE([LTSMIN_PATHNAME_MAX], 1024, [Define maximal length of pathnames])
AC_DEFINE([DEFFILEMODE], 0666, [Define default file mode])


# Checks for programs.
AC_SUBST(AM_CXXFLAGS,["-Wno-inline -ftemplate-depth-128 -finline-functions"])

AM_PROG_CC_C_O
AC_SUBST([CC_FOR_BUILD], [${CC_FOR_BUILD-$CC}])
AC_PROG_CXX
ACX_FINK
ACX_MACPORTS
ACX_PKGSRC
ACX_PTHREAD(
  [LIBS="$PTHREAD_LIBS $LIBS"
   CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
   CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"
   CC="$PTHREAD_CC"],
  [AC_MSG_FAILURE([no pthread support found.])])

dnl Newer libtool allows this instead:
dnl LT_PREREQ(2.2)
dnl LT_INIT([disable-shared])
AC_DISABLE_SHARED
AC_PROG_LIBTOOL
AC_PROG_RANLIB
dnl automake 1.8 <= x <= 1.9.6 has broken AM_PROG_MKDIR_P
AC_PROG_MKDIR_P
AC_SUBST([MKDIR_P])

AX_CHECK_FLEX([
%option prefix="ltsmin_"
%option header-file="conftest.h"
%option outfile="conftest.c"
%option nodefault noyywrap reentrant noyyalloc noyyrealloc noyyfree batch noinput
%option extra-type="ltsmin_parse_env_t"
%%
],,
[AC_MSG_FAILURE([no compatible lexer generator found.])])

AC_PROG_MAKE_SET
AC_PROG_INSTALL

ACX_MPI(
  [CC="$MPICC"
   LIBS="$MPILIBS $LIBS"
   AC_ARG_VAR(MPICXX, [MPI C++ compiler command])
   AC_CHECK_PROGS(MPICXX, [mpic++ mpicxx mpiCC hcp mpxlC_r mpxlC mpCC cmpic++, $CXX])
   AC_SUBST(MPICXX)
   ltsmin_save_CXX="$CXX"
   CXX="$MPICXX"
   AC_LANG_PUSH([C++])
   AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <mpi.h>]], [[MPI::Init();]])],
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
if test x"$ASCIIDOC" = x && test -d "${srcdir}/.git"; then
  AC_MSG_FAILURE([Asciidoc is needed unless building from a release tarball.])
fi
AC_CHECK_PROGS(XMLTO, [xmlto])
if test x"$XMLTO" = x && test -d "${srcdir}/.git"; then
  AC_MSG_FAILURE([xmlto is needed unless building from a release tarball.])
fi
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
AC_CHECK_LIB([dl], [dlsym],,[AC_MSG_FAILURE([libdl not found.])])
AC_CHECK_LIB([popt],[poptGetContext],,[AC_MSG_FAILURE([popt not found.])])
AC_CHECK_LIB([z], [inflateEnd],,
  [AC_MSG_FAILURE([zlib not found.])])

AC_ARG_WITH([bignum],
  [AS_HELP_STRING([--with-bignum=<type>], 
    [enable support for bignums (type=yes|gmp|tommath|no) @<:@default=yes@:>@])],
  [], [with_bignum=yes])
AS_CASE(["$with_bignum"],
  [no],[],
  [yes|gmp|tommath],
    [AS_IF([test x"$with_bignum" = xyes || test x"$with_bignum" = xgmp],
           [AC_CHECK_LIB([gmp], [__gmpz_init], 
              [with_bignum=gmp; have_libgmp=yes],
              [AS_IF([test x"$with_bignum" = xgmp],
                     [AC_MSG_WARN([cannot use libgmp as bignum library.])])])])
     AS_IF([test x"$with_bignum" = xyes || test x"$with_bignum" = xtommath],
           [AC_CHECK_LIB([tommath], [mp_init], 
              [with_bignum=tommath; have_libtommath=yes],
              [AS_IF([test x"$with_bignum" = xtommath],
                     [AC_MSG_WARN([cannot use libtommath as bignum library.])])])])],
  [AC_MSG_WARN([unknown bignum library request (--with-bignum=$with_bignum) ignored.])])

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
ACX_DVEC_LIBS([:],
  [have_divine=no
   AC_MSG_WARN([DiVinE libraries not found.])])
ACX_DVEC2_LIBS([:],
  [have_divine2=no
   AC_MSG_WARN([DiVinE 2.2 libraries not found.])])
AC_SUBST([NIPS_LIBS],['${top_builddir}/nips-vm/libnips_vm.la'])
AC_SUBST([NIPS_CPPFLAGS],['-I${top_srcdir}'])
AC_SUBST([NIPS_LDFLAGS])

AC_SUBST([BUDDY_LIBS],['-lbdd -lm'])
AC_SUBST([BUDDY_CPPFLAGS],['-I${top_srcdir}/buddy/src'])
AC_SUBST([BUDDY_LDFLAGS],['-L${top_builddir}/buddy/src'])

AC_CHECK_LIB([rt], [aio_error])
ACX_CADP_BCG_WRITE([have_bcg=yes],
  [have_bcg=no
   AC_MSG_WARN([no acceptable BCG libraries found; building without.])])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stdint.h stdlib.h string.h strings.h dnl
  unistd.h execinfo.h])
AS_IF([test x"$have_libgmp" = xyes], 
  [AC_CHECK_HEADERS([gmp.h],,[AC_MSG_FAILURE([gmp.h not found.])])])
AS_IF([test x"$have_libtommath" = xyes], 
  [AC_CHECK_HEADERS([tommath.h],,[AC_MSG_FAILURE([tommath.h not found.])])])

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
AC_CACHE_SAVE

# Checks for library functions.
dnl LTSmin has its own *alloc wrappers
dnl AC_FUNC_MALLOC
dnl AC_FUNC_REALLOC
AC_FUNC_STRERROR_R
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([atexit bzero ftruncate strdup strndup strchr strstr strtol dnl
  asprintf memset mkdir pow strcasecmp strrchr backtrace backtrace_symbols dnl
  strsep mkdtemp])
AC_CHECK_DECLS([strdup, strndup, strchr, strrchr, strstr, asprintf,dnl
mkdtemp, strsep])

AM_CONDITIONAL([HAVE_MPI], [test x"$have_mpi" != "x"])
AM_CONDITIONAL([HAVE_LIBGMP], 
  [test x"$with_bignum" != xno && test x"$have_libgmp" = xyes])
AM_CONDITIONAL([HAVE_LIBTOMMATH], 
  [test x"$with_bignum" != xno && test x"$have_libtommath" = xyes])
AM_CONDITIONAL([HAVE_LIBMCRL], [test x"$acx_mcrl" = xyes])
AM_CONDITIONAL([HAVE_LIBMCRL2], [test x"$acx_mcrl2" = xyes])
AM_CONDITIONAL([HAVE_LIBSTEP_INFO], [test x"$have_libstep_info" = xyes])
AM_CONDITIONAL([HAVE_ASCIIDOC], [test x"$ASCIIDOC" != x])
AM_CONDITIONAL([HAVE_XMLTO], [test x"$XMLTO" != x])
AM_CONDITIONAL([HAVE_LIBBCG], [test x"$have_bcg" = xyes])
AM_CONDITIONAL([HAVE_CADP], [test x"$acx_cadp" = xyes])
AM_CONDITIONAL([HAVE_DVEC], [test x"$acx_dvec_libs" = xyes])
AM_CONDITIONAL([HAVE_DVEC2], [test x"$acx_dvec2_libs" = xyes])

AC_CONFIG_COMMANDS_POST(
  [ac_configure_args="$ac_configure_args --enable-keep-terminated-procs dnl
                                         --disable-shared dnl
                                         --libdir='$(eval echo "$pkglibdir")'"])

if test x"$acx_cadp" = xyes; then
   AC_CONFIG_FILES([src/pins_open])
fi

AC_CONFIG_FILES([
       Makefile
       src/Makefile
       src/dm/Makefile
       src/bignum/Makefile
       src/ce/Makefile
       src/torx/Makefile
       doc/Makefile
       lemon/Makefile
])

AC_CONFIG_LINKS([src/lempar.c:lemon/lempar.c])

AC_CONFIG_SUBDIRS([nips-vm buddy ltl2ba])

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
    DiVinE   = ${with_divine:-NOT FOUND, building without DiVinE support}
    DiVinE2  = ${with_divine2:-NOT FOUND, building without DiVinE 2.2 support}

    ASCIIDOC = ${ASCIIDOC:-NOT FOUND, not building documentation}
    XMLTO    = ${XMLTO:-NOT FOUND, not building manpages}
======================================================================
EOM
