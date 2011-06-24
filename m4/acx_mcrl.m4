#serial 3
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   ACX_MCRL([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_MCRL], [
AC_REQUIRE([ACX_EXPORT_DYNAMIC])
AC_ARG_WITH([mcrl],
  [AS_HELP_STRING([--with-mcrl=<prefix>],[muCRL prefix directory])])
AC_ARG_VAR([MCRL], [muCRL command])
case "$with_mcrl" in
  no) acx_mcrl=no ;;
  '') AC_PATH_TOOL(MCRL, ["${MCRL:-mcrl}"], [""])
      if test x"$MCRL" != x; then
        acx_mcrl=yes
        with_mcrl="$(dirname $MCRL)/../mCRL"
      fi
      ;;
   *) acx_mcrl=yes
      dnl be backwards compatible with including the
      dnl /mCRL/ subdir in the prefix
      if test "$with_mcrl/mCRL"; then
        with_mcrl="$with_mcrl/mCRL"
      fi
      ;;
esac

if test x"$acx_mcrl" = xyes; then
    AC_CHECK_SIZEOF([void *])
    if test x"$ac_cv_sizeof_void_p" = x8; then
        MCRL_CPPFLAGS="-DAT_64BIT"
    fi

    AC_SUBST(MCRL_CPPFLAGS, ["$MCRL_CPPFLAGS -I$with_mcrl/include"])
    AC_SUBST(MCRL_LDFLAGS,  ["$acx_cv_cc_export_dynamic -L$with_mcrl/lib"])
    AX_LET([CPPFLAGS], ["$MCRL_CPPFLAGS $CPPFLAGS"],
      [AC_CHECK_HEADERS([aterm2.h])])
    $1
else
    $2
    :
fi
])

#
# SYNOPSIS
#
#   ACX_MCRL_LIBS([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_MCRL_LIBS],[
AC_REQUIRE([ACX_MCRL])dnl
if test x"$acx_mcrl" = xyes; then
    AC_LANG_PUSH([C])

    AX_LET([CPPFLAGS], ["$MCRL_CPPFLAGS $CPPFLAGS"],
           [LIBS], ["$LIBS"],
           [LDFLAGS], ["$MCRL_LDFLAGS $LDFLAGS"],
      [acx_mcrl_libs=yes
       AC_CHECK_LIB([dl], [dlopen],
         [MCRL_LIBS="-ldl $MCRL_LIBS"],
         [],
         [$MCRL_LIBS])
       AC_CHECK_LIB([ATerm], [ATinit],
         [MCRL_LIBS="-lATerm $MCRL_LIBS"],
         [],
         [$MCRL_LIBS])
       AC_CHECK_LIB([mcrlunix], [CreateDir],
         [MCRL_LIBS="-lmcrlunix $MCRL_LIBS"],
         [],
         [$MCRL_LIBS])
       AC_CHECK_LIB([mcrl], [MCRLsetArguments],
         [MCRL_LIBS="-lmcrl $MCRL_LIBS"],
         [acx_mcrl_libs=no],
         [$MCRL_LIBS])
       AC_CHECK_LIB([step], [STsetArguments],
         [MCRL_LIBS="-lstep $MCRL_LIBS"],
         [acx_mcrl_libs=no],
         [$MCRL_LIBS])])

    AC_LANG_POP([C])

    AC_SUBST(MCRL_LIBS)
fi
if test x"$acx_mcrl_libs" = xyes; then
  ifelse([$1],,
         [AC_SUBST(CPPFLAGS, ["$MCRL_CPPFLAGS $CPPFLAGS"])
          AC_SUBST(LDFLAGS,  ["$MCRL_LDFLAGS $LDFLAGS"])
          AC_SUBST(LIBS,     ["$MCRL_LIBS $LIBS"])],
         [$1])
  :
else
  $2
  :
fi
])
