AC_PREREQ(2.60)
AC_INIT([ltsmin],[3.0],[ltsmin-support@lists.utwente.nl])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autotools])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/pins-lib/pins.c])
AC_CONFIG_HEADERS([src/hre/config.h])

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
AC_DEFINE([LTSMIN_CONFIG_INCLUDED], 1, [Controls where headers are included from])
AC_DEFINE([DEFFILEMODE], 0666, [Define default file mode])

#DejaGNU
if test x"$DEJAGNU" = x
then
  DEJAGNU="\$(top_srcdir)/testsuite/ltsmin.exp"
fi
AC_SUBST(DEJAGNU)
  
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

dnl Checks for programs.
ACX_SPINS([:], [AC_MSG_WARN([SpinS will not be compiled.])])

AC_ARG_ENABLE([dist],
              [AS_HELP_STRING([--disable-dist],
               [do not build distributed tools, that require MPI])],,
              enable_dist=yes)
AS_IF([test x"$enable_dist" = "xyes"],[
AC_LANG_PUSH([C])
AX_MPI([have_mpi=yes],[AC_MSG_WARN([unable to find working MPI C complier wrapper])])
AC_LANG_POP([C])
AC_LANG_PUSH([C++])
AX_MPI([have_mpi=yes],[AC_MSG_WARN([unable to find working MPI C++ complier wrapper])])
AC_LANG_POP([C++])
])

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
AC_CHECK_LIB([popt],[poptGetContext],,[AC_MSG_FAILURE([popt not found.])])
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

AC_SUBST([BUDDY_LIBS],['${top_builddir}/buddy/src/libbdd.la -lm'])
AC_SUBST([BUDDY_CPPFLAGS],['-I${top_srcdir}/buddy/src'])
AC_SUBST([BUDDY_LDFLAGS])

ACX_DDD([:],
  [AC_MSG_WARN([No libDDD installation found.])])

ACX_SPOT([:],
  [AC_MSG_WARN([No Spot installation found.])])

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

have_opaal="yes"
#AC_CHECK_LIB([tbbmalloc_proxy], [malloc],
#    [have_tbbmalloc="TBB malloc library was found"]
#    AC_SUBST([MALLOC_LIBS], ["-ltbbmalloc_proxy"])
#    [have_opaal="yes"],
#    AC_MSG_WARN([TBB malloc library not found.]))

AC_ARG_WITH([sylvan],[AS_HELP_STRING([--with-sylvan],[Compile sylvan])],
            [],[AC_RUN_IFELSE([AC_LANG_PROGRAM([], [dnl
return sizeof(void*) == 8 ? 0 : 1;
                              ])],
                              [with_sylvan=yes],
                              [with_sylvan=no])])

if test "x$with_sylvan" == xyes; then
    AC_CONFIG_SUBDIRS([sylvan])
    [have_sylvan="yes"]
fi

if test "x$have_libspg" == xyes; then
    AC_DEFINE([HAVE_LIBSPG], [], [Defined if the SPG lib has been compiled.])
fi

AC_SUBST(AM_CPPFLAGS,['-I${top_srcdir}/src -I${top_builddir}/src'])

AC_SUBST([SYLVAN_LIBS],['${top_builddir}/sylvan/src/libsylvan.la'])
AC_SUBST([SYLVAN_CPPFLAGS],['-I${top_srcdir}/sylvan/src'])
AC_SUBST([SYLVAN_LDFLAGS])

AC_CHECK_PROGS(OPAAL_LTSMIN,[opaal_ltsmin])

AC_ARG_WITH([scoop],[AS_HELP_STRING([--with-scoop],[Compile scoop])],[],[
    AC_CHECK_PROG(GHC, ghc, ghc)
    AC_CHECK_PROG(HAPPY, happy, happy)
    if test -z "$GHC" -o -z "$HAPPY"; then    
        [with_scoop=no]
    else
        [with_scoop=yes]
    fi
])

if test "x$with_scoop" == xyes; then
    AC_CONFIG_SUBDIRS([scoop])
    [have_scoop="yes"]
fi

AX_BOOST_BASE([1.46],[have_boost="yes"],)
AX_PATH_VIENNACL(1.5.1, no)
if (test x$HAVE_VIENNACL = x1); then
    have_viennacl="yes"
else
    AC_MSG_WARN([no acceptable ViennaCL installation found.])
fi

AM_PATH_XML2(,[have_xml2="yes"],[AC_MSG_WARN([No acceptable libxml2 installation found.])])

AC_ARG_WITH([prob],[AS_HELP_STRING([--with-prob],[Compile ProB front-end])],[],[with_prob=yes])
AS_IF([test x"$with_prob" = "xyes"],[AX_ZMQ([3.0.0])])
AS_IF([test x"$HAVE_ZMQ" = "x1"],[AX_CZMQ(,[
    acx_prob=yes
    PROB_CPPFLAGS="$CZMQ_CPPFLAGS $ZMQ_CPPFLAGS"
    PROB_LDFLAGS="$CZMQ_LDFLAGS $ZMQ_LDFLAGS"
    PROB_LIBS="$CZMQ_LIBS $ZMQ_LIBS"
    AC_SUBST(PROB_CPPFLAGS)
    AC_SUBST(PROB_LDFLAGS)
    AC_SUBST(PROB_LIBS)
])])

AC_ARG_ENABLE([werror],
    [AS_HELP_STRING([--enable-werror], [Use -Werror @<:@no@:>@])],
    [:],
    [enable_werror=no])
AS_IF([test x"$enable_werror" = "xyes"], [AC_SUBST([AM_CFLAGS], [-Werror])])

#AM_CONDITIONAL([HAVE_POSIX], [test x"$POSIX_SHARED" = "xyes"])
AM_CONDITIONAL([HAVE_SCOOP], [test x"$have_scoop" = "xyes"])
AM_CONDITIONAL([HAVE_SYLVAN], [test x"$have_sylvan" = "xyes"])
AS_IF([test x"$have_sylvan" = "xyes"], [AC_DEFINE([HAVE_SYLVAN], [1], [Define if using sylvan.])])
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
AM_CONDITIONAL([HAVE_PNML], [test x"$have_xml2" = xyes])
AM_CONDITIONAL([HAVE_PROB], [test x"$acx_prob" = xyes])
AM_CONDITIONAL([HAVE_SPOT], [test x"$acx_spot" = xyes])
AS_IF([test x"$acx_spot" = "xyes"], [AC_DEFINE([HAVE_SPOT], [1], [Define if using Spot.])])

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
AC_ARG_ENABLE([test-OPPAAL], [AS_HELP_STRING([--disable-test-OPPAAL],
               [do not test the OPPAAL front-end])],, enable_test_OPPAAL=$enable_test_all)
AM_CONDITIONAL([TEST_OPPAAL], [test x"$enable_test_OPPAAL" = xyes])
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
AC_ARG_ENABLE([test-LTS-generation], [AS_HELP_STRING([--disable-test-LTS-generation],
               [do not test LTS generation])],, enable_test_LTS_generation=$enable_test_all)
AM_CONDITIONAL([TEST_LTS_GENERATION], [test x"$enable_test_LTS_generation" = xyes])

AC_CONFIG_COMMANDS_POST(
  [ac_configure_args="$ac_configure_args --enable-keep-terminated-procs dnl
                                         --disable-shared dnl
                                         --libdir='$(eval echo "$pkglibdir")'"])

AC_CONFIG_FILES([
       Makefile
       examples/Makefile
       src/Makefile
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
       src/etf-convert/Makefile
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

AC_CONFIG_SUBDIRS([buddy ltl2ba])

AC_OUTPUT

cat <<EOM
Configuration:
======================================================================
    ASCIIDOC = ${ASCIIDOC:-NOT FOUND, not building documentation}
    BIGNUM   = ${with_bignum}
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
    SPOT     = ${acx_spot:-NO, Spot not found (override with --with-spot)}
    SYLVAN   = ${have_sylvan:-NO, building without Sylvan/LDD-MC support}
    XMLTO    = ${XMLTO:-NOT FOUND, not building manpages}
    BOOST    = ${have_boost:-NOT FOUND, not including advanced variable reordering algorithms}
    VIENNACL = ${have_viennacl:-NO, not including advanced variable reordering algorithms}
    PNML     = ${have_xml2:-NO, not building pnml (Petri net) front-end}
    PROB     = ${acx_prob:-NO, either ProB was disabled or ZMQ/CZMQ could not be found}
======================================================================
EOM
