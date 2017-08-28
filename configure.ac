AC_PREREQ(2.65)
AC_INIT([ltsmin],[3.0.0],[ltsmin-support@lists.utwente.nl])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autotools])
AM_INIT_AUTOMAKE([1.14 foreign subdir-objects])
AC_CONFIG_SRCDIR([src/pins-lib/pins.c])
AC_CONFIG_HEADERS([src/hre/config.h])

AC_SUBST([pkglibdir], "\${libdir}/${PACKAGE_NAME}-${PACKAGE_VERSION}")

AC_USE_SYSTEM_EXTENSIONS

AC_DEFINE([LTSMIN_PATHNAME_MAX], 1024, [Define maximal length of pathnames])
AC_DEFINE([LTSMIN_CONFIG_INCLUDED], 1, [Controls where headers are included from])
AC_DEFINE([DEFFILEMODE], 0666, [Define default file mode])

m4_divert_text([DEFAULTS], [: "${ARFLAGS=crs} ${AR_FLAGS=crs}"])
#DejaGNU
if test x"$DEJAGNU" = x
then
  DEJAGNU="\$(top_srcdir)/testsuite/ltsmin.exp"
fi
AC_SUBST(DEJAGNU)
  

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

# explicitly link if necessary  
AC_CHECK_LIB(pthread, pthread_create, [LIBS="$LIBS -lpthread"])

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

AC_LANG_SAVE
AC_LANG_C
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
int main()
{
    pthread_mutex_t mutex;
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr))
        exit(1);
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED))
        exit(2);
    if (pthread_mutex_init(&mutex, &attr))
        exit(3);
    if (pthread_mutexattr_destroy(&attr))
        exit(4);
    if (pthread_mutex_destroy(&mutex))
        exit(5);
    exit(0);
}
]])], [POSIX_SHARED=yes], [POSIX_SHARED=no
    AC_MSG_FAILURE([No POSIX support found.])])
AC_LANG_RESTORE

PKG_PROG_PKG_CONFIG
m4_pattern_forbid([^PKG_[A-Z_]+$], [missing some pkgconf macros (pkgconf package)])

AS_IF([test x"$PKG_CONFIG" = x], [AC_MSG_FAILURE([Please install pkgconf])])

AC_ARG_ENABLE([pkgconf_static],
    [AS_HELP_STRING([--enable-pkgconf-static], [
        Configure pkgconf to search for private libs too.
        This is necessary when you want to statically link binaries.])])
AS_IF([test "x$enable_pkgconf_static" = "xyes"], [PKG_CONFIG="$PKG_CONFIG --static"])

AX_PROG_BISON([BISON="$YACC"],[AC_MSG_FAILURE([Please install Bison])])
AX_PROG_BISON_VERSION([3.0.2],,[AC_MSG_FAILURE([Please install Bison >= 3.0.2])])

dnl Checks for programs.
ACX_SPINS([:], [AC_MSG_WARN([SpinS will not be compiled.])])

AC_ARG_ENABLE([dist],
              [AS_HELP_STRING([--disable-dist],
               [do not build distributed tools, that require MPI])])
AS_IF([test "x$enable_dist" != "xno"],[
    AC_LANG_PUSH([C])
    AX_MPI([have_mpi=yes],[AC_MSG_WARN([unable to find working MPI C complier wrapper])])
    AC_LANG_POP([C])
    AC_LANG_PUSH([C++])
    AX_MPI([have_mpi=yes],[AC_MSG_WARN([unable to find working MPI C++ complier wrapper])])
    AC_LANG_POP([C++])
])

AC_PROG_CC_C99
AX_CXX_COMPILE_STDCXX_11
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
  AC_MSG_WARN([Asciidoc is needed for building documentation.])
fi
AC_CHECK_PROGS(XMLTO, [xmlto])
if test x"$XMLTO" = x && test -d "${srcdir}/.git"; then
  AC_MSG_WARN([xmlto is needed for building man pages.])
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
dnl workaround:
dnl some autotools/libtool versions appear to lose dependencies to -lm
dnl when linking .la files with CXXLD, hence add it here
AC_SEARCH_LIBS([pow], [m])
AC_CHECK_LIB([dl], [dlsym],,[AC_MSG_FAILURE([libdl not found.])])
AC_CHECK_LIB([z], [inflateEnd],,[AC_MSG_FAILURE([zlib not found.])])

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
                     [with_bignum=no; AC_MSG_WARN([cannot use libgmp as bignum library.])])])])
     AS_IF([text x"$with_bignum" = xyes || test x"$with_bignum" = xtommath],
           [AC_CHECK_LIB([tommath], [mp_init], 
              [with_bignum=tommath; have_libtommath=yes],
              [AS_IF([test x"$with_bignum" = xtommath],
                     [with_bignum=no; AC_MSG_WARN([cannot use libtommath as bignum library.])])])])],
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

ACX_DDD([:],
  [AC_MSG_WARN([No libDDD installation found.])])

AC_ARG_WITH([spot],
    AS_HELP_STRING([--without-spot], [do not include the Spot library for LTL model checking]))
acx_spot="no"
AS_IF([test "x$with_spot" != "xno"], [
    PKG_CHECK_MODULES([SPOT], [libspot >= 2.3], [
        acx_spot=yes
    ], [
        AC_MSG_WARN(No valid Spot version found!)
    ])
])

acx_buddy=""
AS_IF([test "x$acx_spot" = "xyes"], [
    PKG_CHECK_MODULES([BDDX], [libbddx >= 2.3],[
        AC_MSG_NOTICE([Because Spot was found, the Spot Buddy implementation will be used!])
        acx_buddy="Spot version (because Spot was found)"
        AC_SUBST([BUDDY_LIBS],["$BDDX_LIBS"])
        AC_SUBST([BUDDY_CFLAGS],["$BDDX_CFLAGS"])
    ],[
       AC_MSG_ERROR(Could not find libbddx!
                    Because both Spot and LTSmin distribute BuDDy,
                    we have to choose between BUDDY versions,
                    if Spot is found, then the Spot version will be used,
                    meaning libbddx must be installed.)])
],[
    AC_ARG_WITH([buddy],
        AS_HELP_STRING([--without-buddy], [do not include BuDDy for symbolic state storage]))
    AS_IF([test "x$with_buddy" != "xno"], [
        PKG_CHECK_MODULES([BDD], [libbdd >= 2.4], [
            acx_buddy="LTSmin version (because Spot was not found or disabled)"
            AC_SUBST([BUDDY_LIBS],["$BDD_LIBS"])
            AC_SUBST([BUDDY_CFLAGS],["$BDD_CFLAGS"])
        ], [
            AC_MSG_WARN(No valid BuDDy version found!)
        ])
    ])
])

ACX_CLINE_DEF([], [])

AC_CHECK_LIB([rt], [aio_error])
ACX_CADP_BCG_WRITE([have_bcg=yes],
  [have_bcg=no
   AC_MSG_WARN([no acceptable BCG libraries found; building without.])])

AC_CHECK_LIB([zip],[zip_open],
  [AC_SUBST([LIBZIP_LIBS],[-lzip])
   AX_LET([CPPFLAGS],["$CPPFLAGS"],
    [dnl Version 0.10 of libzip needs a zipconf.h in the lib directory.
     dnl The assumption here is that this directory is known relative
     dnl to the include directory. Note that this is not foolproof, if
     dnl e.g. zipconf.h is in some default location (other than /usr
     dnl /lib/libzip/include); in which case the user has to add the
     dnl path explicitly.
     for f in $CPPFLAGS; do
       case "$f" in
         -I*) if test -f "${f#-I}/zip.h"; then
                if test -f "${f#-I}/../lib/libzip/include/zipconf.h"; then
                  AC_SUBST([LIBZIP_CPPFLAGS],[$f/../lib/libzip/include])
                  CPPFLAGS="$LIBZIP_CPPFLAGS $CPPFLAGS"
                  break
                fi
              fi
              ;;
       esac
     done
     if test -f "/usr/lib/ziplib/zipconf.h"; then
         AC_SUBST([LIBZIP_CPPFLAGS],[-I/usr/lib/libzip/include])
         CPPFLAGS="$LIBZIP_CPPFLAGS $CPPFLAGS"
         break
     fi
     AC_CHECK_HEADERS([zip.h],,[AC_MSG_FAILURE([zip.h not found.])])])
     acx_libzip=yes
  ],
  [acx_libzip=no
   AC_MSG_WARN([no acceptable zip library found; building without.])
  ])

# Checks for gperftools profiler
AC_ARG_WITH([profiler],
  [AS_HELP_STRING([--with-profiler], 
    [enable support for gperftools in spgsolver @<:@default=no@:>@])],
  [with_profiler=yes], [with_profiler=no])
AS_IF([test x"$with_profiler" = xyes],
  [AC_CHECK_LIB([profiler],[ProfilerStart],
      [AC_SUBST([PROFILER_LDFLAGS],['-lprofiler -ltcmalloc'])
       AC_SUBST([PROFILER_CPPFLAGS],[-DHAVE_PROFILER])
       acx_profiler=yes
      ],
      [acx_profiler=no
       AC_MSG_WARN([no acceptable profiler library found; building without.])
      ])
   ])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stdint.h stdlib.h string.h strings.h dnl
  unistd.h execinfo.h])
AS_IF([test x"$have_libgmp" = xyes], 
  [AC_CHECK_HEADERS([gmp.h],,[AC_MSG_FAILURE([gmp.h not found.])])
   AC_LANG_PUSH([C++])
   AC_CHECK_HEADERS([gmpxx.h],,[AC_MSG_FAILURE([gmpxx.h not found.])])
   AC_LANG_POP([C++])
  ])
AS_IF([test x"$have_libtommath" = xyes], 
  [AC_CHECK_HEADERS([tommath.h],,[AC_MSG_FAILURE([tommath.h not found.])])])

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


# check for fine-grained multi-core synchronization primitives
acx_atomics=yes
AX_CHECK_FUNC_INCLUDE([],[__sync_bool_compare_and_swap],
  [int x = 0;
   __sync_bool_compare_and_swap(&x,0,1)],
  [],[acx_atomics=no])
AX_CHECK_FUNC_INCLUDE([],[__sync_fetch_and_add],
  [int x = 0;
   __sync_fetch_and_add(&x,1)],
  [],[acx_atomics=no])
AX_CHECK_FUNC_INCLUDE([],[__sync_fetch_and_and],
  [int x = 0;
   __sync_fetch_and_and(&x,1)],
  [],[acx_atomics=no])
AX_CHECK_FUNC_INCLUDE([],[__sync_fetch_and_or],
  [int x = 0;
   __sync_fetch_and_or(&x,1)],
  [],[acx_atomics=no])
AX_CHECK_FUNC_INCLUDE([],[__sync_synchronize],
  [__sync_synchronize()],
  [],[acx_atomics=no])
if test x"$acx_atomics" != "xyes"; then
  AC_MSG_FAILURE([no acceptable atomic operations found.])
fi

if test "x$have_libspg" == xyes; then
    AC_DEFINE([HAVE_LIBSPG], [], [Defined if the SPG lib has been compiled.])
fi

AC_SUBST(AM_CPPFLAGS,['-I${top_srcdir}/src -I${top_builddir}/src'])

AC_ARG_ENABLE([opaal], [AS_HELP_STRING([--disable-opaal], [Do not compile OPAAL front-end])])
AS_IF([test "x$enable_opaal" != "xno"], [
    AC_CHECK_PROGS(OPAAL_LTSMIN,[opaal_ltsmin])
    have_opaal="yes"
])

AC_ARG_ENABLE([scoop],[AS_HELP_STRING([--disable-scoop],[Do not compile scoop front-end])])
AS_IF([test "x$enable_scoop" != "xno"], [
    AC_CHECK_PROG(GHC, ghc, ghc)
    AC_CHECK_PROG(HAPPY, happy, happy)
    AS_IF([test -n "$GHC" -a -n "$HAPPY"], [
        AC_CONFIG_SUBDIRS([scoop])
        [have_scoop="yes"]
    ])
])

AX_BOOST_BASE([1.46],[have_boost="yes"],)
AX_PATH_VIENNACL(1.5.1, no)
if (test x$HAVE_VIENNACL = x1); then
    have_viennacl="yes"
else
    AC_MSG_WARN([no acceptable ViennaCL installation found.])
fi

AC_ARG_ENABLE([pnml], [AS_HELP_STRING([--disable-pnml], [Do not compile PNML front-end])])
AS_IF([test "x$enable_pnml" != "xno"], [
    PKG_CHECK_MODULES([XML2], [libxml-2.0], [
        acx_pnml=yes
        PNML_CFLAGS="$XML2_CFLAGS"
        PNML_LIBS="$XML2_LIBS"
        AC_SUBST(PNML_CFLAGS)
        AC_SUBST(PNML_LIBS)],
        AC_MSG_WARN(libxml2 is not installed. Not building PNML front-end)
    )
])

PKG_CHECK_MODULES([POPT], [popt],,[
    AC_MSG_ERROR(Missing required package popt please install popt.)
])

AC_ARG_ENABLE([prob], [AS_HELP_STRING([--disable-prob], [Do not compile ProB front-end])])
AS_IF([test "x$enable_prob" != "xno"], [
    PKG_CHECK_MODULES([CZMQ], [libczmq],[
        acx_prob=yes
        PROB_CFLAGS="$CZMQ_CFLAGS"
        PROB_LIBS="$CZMQ_LIBS"
        AC_SUBST(PROB_CFLAGS)
        AC_SUBST(PROB_LIBS)],
        AC_MSG_WARN(libczmq is not installed. Not building ProB front-end)
    )
])

AC_ARG_ENABLE([werror],
    [AS_HELP_STRING([--enable-werror], [Use -Werror @<:@no@:>@])])
AS_IF([test "x$enable_werror" = "xyes"], [
    AC_SUBST([AM_CFLAGS], [-Werror])
    AC_SUBST([AM_CXXFLAGS], [-Werror])
])

AC_ARG_WITH([sylvan],
    [AS_HELP_STRING([--without-sylvan], [do not include Sylvan multi-core decision diagram packages])])

AS_IF([test "x$with_sylvan" != "xno"], [
    PKG_CHECK_MODULES([SYLVAN], [sylvan >= 1.1 sylvan < 1.2],[
        have_sylvan=yes
    ], [
        AC_MSG_WARN([Sylvan >= 1.1, < 1.2 is not installed.])
    ])
])

AC_DEFINE(
    [__assume(cond)],
    [do { if (!(cond)) __builtin_unreachable(); } while (0)],
    [Provide a hint to the optimizer]
)

#AM_CONDITIONAL([HAVE_POSIX], [test x"$POSIX_SHARED" = "xyes"])
AM_CONDITIONAL([HAVE_SCOOP], [test x"$have_scoop" = "xyes"])
AM_CONDITIONAL([HAVE_SYLVAN], [test x"$have_sylvan" = "xyes"])
AM_COND_IF([HAVE_SYLVAN], [AC_DEFINE([HAVE_SYLVAN],, [Define if using Sylvan.])])
AM_CONDITIONAL([HAVE_OPAAL], [test x"$have_opaal" = "xyes"])
AM_CONDITIONAL([HAVE_OPAAL_COMPILER], [test x"$OPAAL_LTSMIN" != x])
AM_CONDITIONAL([HAVE_MPI], [test x"$have_mpi" != "x"])
AM_CONDITIONAL([HAVE_LIBGMP], 
  [test x"$with_bignum" != xno && test x"$have_libgmp" = xyes])
AM_CONDITIONAL([HAVE_LIBTOMMATH], 
  [test x"$with_bignum" != xno && test x"$have_libtommath" = xyes])
AM_CONDITIONAL([HAVE_LIBMCRL], [test x"$acx_mcrl" = xyes])
AM_CONDITIONAL([HAVE_LIBMCRL2], [test x"$acx_mcrl2" = xyes])
AM_CONDITIONAL([HAVE_LIBPBES], [test x"$acx_pbes" = xyes])
AM_CONDITIONAL([HAVE_LIBSTEP_INFO], [test x"$have_libstep_info" = xyes])
AM_CONDITIONAL([HAVE_LIBDDD], [test x"$acx_libddd" = xyes])
AM_CONDITIONAL([HAVE_ASCIIDOC], [test x"$ASCIIDOC" != x])
AM_CONDITIONAL([HAVE_XMLTO], [test x"$XMLTO" != x])
AM_CONDITIONAL([HAVE_LIBBCG], [test x"$have_bcg" = xyes])
AM_CONDITIONAL([HAVE_CADP], [test x"$acx_cadp" = xyes])
AM_CONDITIONAL([HAVE_DVE], [true])    dnl can be built unconditionally
AM_CONDITIONAL([HAVE_SPINS], [true])
AM_CONDITIONAL([HAVE_SPINS_COMPILER], [test x"$acx_spins" = "xyes"])
#AM_CONDITIONAL([HAVE_ATOMICS], [test x"$acx_atomics" = xyes])
AM_CONDITIONAL([HAVE_VERSION_CONTROL], [test -d "${srcdir}/.git"])
AM_CONDITIONAL([HAVE_LIBZIP], [test x"$acx_libzip" = xyes])
AM_CONDITIONAL([HAVE_PROFILER], [test x"$acx_profiler" = xyes])
AM_CONDITIONAL([HAVE_BOOST], [test x"$have_boost" = xyes])
AM_CONDITIONAL([HAVE_VIENNACL], [test x"$have_viennacl" = xyes])
AM_CONDITIONAL([HAVE_PNML], [test x"$acx_pnml" = xyes])
AM_CONDITIONAL([HAVE_PROB], [test x"$acx_prob" = xyes])
AM_CONDITIONAL([HAVE_SPOT], [test x"$acx_spot" = xyes])
AM_COND_IF([HAVE_SPOT], [AC_DEFINE([HAVE_SPOT],, [Define if using Spot.])])
AM_CONDITIONAL([HAVE_BUDDY], [test x"$acx_buddy" != x])
AM_COND_IF([HAVE_BUDDY], [AC_DEFINE([HAVE_BUDDY],, [Define if using BuDDy.])])

AM_CONDITIONAL(BUILD_OS_IS_DARWIN,
[case "${build_os}" in
  darwin*)  true ;;
  *)        false ;;
esac])

# all tests
AC_ARG_ENABLE([test-all], [AS_HELP_STRING([--disable-test-all],
               [do not run any tests (so specific tests can be enabled)])],, enable_test_all=yes)

# unit tests
AC_ARG_ENABLE([test-unit], [AS_HELP_STRING([--disable-test-unit],
               [do not run the unit tests])],, enable_test_unit=$enable_test_all)
AM_CONDITIONAL([TEST_UNIT], [test x"$enable_test_unit" = xyes])

# front-ends
AC_ARG_ENABLE([test-DVE], [AS_HELP_STRING([--disable-test-DVE],
               [do not test the DVE front-end])],, enable_test_DVE=$enable_test_all)
AM_CONDITIONAL([TEST_DVE], [test x"$enable_test_DVE" = xyes])
AC_ARG_ENABLE([test-ETF], [AS_HELP_STRING([--disable-test-ETF],
               [do not test the ETF front-end])],, enable_test_ETF=$enable_test_all)
AM_CONDITIONAL([TEST_ETF], [test x"$enable_test_ETF" = xyes])
AC_ARG_ENABLE([test-Promela], [AS_HELP_STRING([--disable-test-Promela],
               [do not test the Promela front-end])],, enable_test_Promela=$enable_test_all)
AM_CONDITIONAL([TEST_PROMELA], [test x"$enable_test_Promela" = xyes])
AC_ARG_ENABLE([test-LPS], [AS_HELP_STRING([--disable-test-LPS],
               [do not test the LPS front-end])],, enable_test_LPS=$enable_test_all)
AM_CONDITIONAL([TEST_LPS], [test x"$enable_test_LPS" = xyes])
AC_ARG_ENABLE([test-PBES], [AS_HELP_STRING([--disable-test-PBES],
               [do not test the PBES front-end])],, enable_test_PBES=$enable_test_all)
AM_CONDITIONAL([TEST_PBES], [test x"$enable_test_PBES" = xyes])
AC_ARG_ENABLE([test-OPAAL], [AS_HELP_STRING([--disable-test-OPAAL],
               [do not test the OPAAL front-end])],, enable_test_OPAAL=$enable_test_all)
AM_CONDITIONAL([TEST_OPAAL], [test x"$enable_test_OPAAL" = xyes])
AC_ARG_ENABLE([test-PNML], [AS_HELP_STRING([--disable-test-PNML],
               [do not test the PNML front-end])],, enable_test_PNML=$enable_test_all)
AM_CONDITIONAL([TEST_PNML], [test x"$enable_test_PNML" = xyes])
AC_ARG_ENABLE([test-ProB], [AS_HELP_STRING([--disable-test-ProB],
               [do not test the ProB front-end])],, enable_test_ProB=$enable_test_all)
AM_CONDITIONAL([TEST_PROB], [test x"$enable_test_ProB" = xyes])
AC_ARG_ENABLE([test-MAPA], [AS_HELP_STRING([--disable-test-MAPA],
               [do not test the MAPA front-end])],, enable_test_MAPA=$enable_test_all)
AM_CONDITIONAL([TEST_MAPA], [test x"$enable_test_MAPA" = xyes])

# other algorithms
AC_ARG_ENABLE([test-LTL], [AS_HELP_STRING([--disable-test-LTL],
               [do not test LTL model checking])],, enable_test_LTL=$enable_test_all)
AM_CONDITIONAL([TEST_LTL], [test x"$enable_test_LTL" = xyes])
AC_ARG_ENABLE([test-safety], [AS_HELP_STRING([--disable-test-safety],
               [do not test safety model checking])],, enable_test_safety=$enable_test_all)
AM_CONDITIONAL([TEST_SAFETY], [test x"$enable_test_safety" = xyes])
AC_ARG_ENABLE([test-POR], [AS_HELP_STRING([--disable-test-POR],
               [do not test Partial Order Reduction])],, enable_test_POR=$enable_test_all)
AM_CONDITIONAL([TEST_POR], [test x"$enable_test_POR" = xyes])
AC_ARG_ENABLE([test-DFS_FIFO], [AS_HELP_STRING([--disable-test-DFS-FIFO],
               [do not test the DFS_FIFO algorithm])],, enable_test_DFS_FIFO=$enable_test_all)
AM_CONDITIONAL([TEST_DFS_FIFO], [test x"$enable_test_DFS_FIFO" = xyes])
AC_ARG_ENABLE([test-SCC], [AS_HELP_STRING([--disable-test-SCC],
               [do not test the SCC algorithms])],, enable_test_SCC=$enable_test_all)
AM_CONDITIONAL([TEST_SCC], [test x"$enable_test_SCC" = xyes])
AC_ARG_ENABLE([test-MU], [AS_HELP_STRING([--disable-test-MU],
               [do not test the mu-calculus algorithms])],, enable_test_MU=$enable_test_all)
AM_CONDITIONAL([TEST_MU], [test x"$enable_test_MU" = xyes])
AC_ARG_ENABLE([test-LTS-generation], [AS_HELP_STRING([--disable-test-LTS-generation],
               [do not test LTS generation])],, enable_test_LTS_generation=$enable_test_all)
AM_CONDITIONAL([TEST_LTS_GENERATION], [test x"$enable_test_LTS_generation" = xyes])

AC_CONFIG_COMMANDS_POST(
  [ac_configure_args="$ac_configure_args --enable-keep-terminated-procs dnl
                                         --disable-shared dnl
                                         --libdir='$(eval echo "$pkglibdir")'"])

AC_CONFIG_FILES([
       www/_config.yml
       Makefile
       examples/Makefile
       src/Makefile
       src/andl-lib/Makefile
       src/hre-io/Makefile
       src/hre/Makefile
       src/hre-mpi/Makefile
       src/lts-io/Makefile
       src/lts-lib/Makefile
       src/ltsmin-lib/Makefile
       src/mc-lib/Makefile
       src/pins-lib/Makefile
       src/prob-lib/Makefile
       src/pins-lib/por/Makefile
       src/util-lib/Makefile
       src/vset-lib/Makefile
       src/spg-lib/Makefile
       src/gcf-tool/Makefile
       src/ldd2bdd/Makefile
       src/ltsmin-printtrace/Makefile
       src/ltsmin-compare/Makefile
       src/ltsmin-convert/Makefile
       src/ltsmin-reduce/Makefile
       src/ltsmin-reduce-dist/Makefile
       src/pins2lts-dist/Makefile
       src/pins2lts-dist/gcc/Makefile
       src/pins2lts-dist/mpicc/Makefile
       src/pins2lts-mc/parallel/Makefile
       src/pins2lts-mc/algorithm/Makefile
       src/pins2lts-mc/Makefile
       src/pins2lts-seq/Makefile
       src/pins2lts-sym/Makefile
       src/pins2lts-sym/maxsum/Makefile
       src/pins-open/Makefile
       src/spg/Makefile
       src/dm/Makefile
       src/bignum/Makefile
       src/ce/Makefile
       src/torx/Makefile
       src/scripts/Makefile
       src/tests/Makefile
       src/tests/gcc/Makefile
       src/tests/mpicc/Makefile
       doc/Makefile
       lemon/Makefile
       testsuite/Makefile
])

AC_CONFIG_LINKS([src/ltsmin-lib/lempar.c:lemon/lempar.c])

AC_CONFIG_SUBDIRS([ltl2ba])

AC_OUTPUT

cat <<EOM
Configuration:
======================================================================
    ASCIIDOC = ${ASCIIDOC:-NOT FOUND, not building documentation}
    BIGNUM   = ${with_bignum}
    BUDDY    = ${acx_buddy:-NOT FOUND, building without BuDDy support}
    CADP     = ${CADP:-NOT FOUND, building without CADP support}
    CC       = ${CC:-NOT FOUND}
    CXX      = ${CXX:-NOT FOUND}
    LIBDDD   = ${LIBDDD:-NOT FOUND, building without DDD support}
    MCRL     = ${with_mcrl:-NOT FOUND, building without muCRL support}
    MCRL2    = ${with_mcrl2:-NOT FOUND, building without mCRL2 support}
    MPICC    = ${MPICC:-NOT FOUND, building without MPI support}
    MPICXX   = ${MPICXX:-NOT FOUND, building without MPI support}
    OPAAL    = ${have_opaal:-TBB malloc NOT FOUND, building without opaal support}
    PBES     = ${acx_pbes:-NOT FOUND, building without PBES support}
    SCOOP    = ${have_scoop:-NO, either scoop was disabled or ghc/happy/ghc-pkg/hashmap/deepseq could not be found}
    SPINS    = ${acx_spins:-NO, JDK or Ant not found (override with --with-spins)}
    SPOT     = ${acx_spot:-NO, Spot not found, or disabled}
    XMLTO    = ${XMLTO:-NOT FOUND, not building manpages}
    BOOST    = ${have_boost:-NOT FOUND, not including advanced variable reordering algorithms}
    VIENNACL = ${have_viennacl:-NO, not including advanced variable reordering algorithms}
    PNML     = ${acx_pnml:-NO, not building pnml (Petri net) front-end}
    PROB     = ${acx_prob:-NO, either ProB was disabled or CZMQ could not be found}
    Sylvan   = ${have_sylvan:-NOT FOUND, not including parallel symbolic algorithms}
======================================================================
EOM
