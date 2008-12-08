#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
# Modified by Stefan Blom.
#
# SYNOPSIS
#
#   ACX_MCRL2([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_MCRL2], [
AC_ARG_WITH([mcrl2],
  [AS_HELP_STRING([--with-mcrl2=<prefix>],[mCRL2 prefix directory])])
AC_ARG_VAR([MCRL2], [some mCRL2 command])
case "$with_mcrl2" in
  no) acx_mcrl2=no ;;
  '') AC_PATH_TOOL(MCRL2, ["${MCRL2:-mcrl22lps}"], [""])
      if test x"$MCRL2" != x; then
        acx_mcrl2=yes
        with_mcrl2="$(dirname "$MCRL2")/.."
      fi
      ;;
   *) acx_mcrl2=yes;;
esac

if test x"$acx_mcrl2" = xyes; then
    AC_LANG_PUSH([C++])
    AC_CHECK_SIZEOF([void *])
    if test x"$ac_cv_sizeof_void_p" = x8; then
        MCRL2_CPPFLAGS="-DAT_64BIT"
    fi
    AC_SUBST(MCRL2_CPPFLAGS, ["$MCRL2_CPPFLAGS -DBOOST_MPL_CFG_NO_PREPROCESSED_HEADERS=1 -I$with_mcrl2/include -I$with_mcrl2/include/mcrl2/aterm"])
    AC_SUBST(MCRL2_LDFLAGS,  ["-L${with_mcrl2}/lib/mcrl2"])
    AX_LET([CPPFLAGS], ["$MCRL2_CPPFLAGS $CPPFLAGS"],
      [AC_CHECK_HEADER([mcrl2/atermpp/set.h],,
         [AC_MSG_FAILURE([cannot find mCRL2 Boost headers,
see README on how to install mCRL2 properly (--install-boost-headers).])]
         )])
    AC_LANG_POP([C++])
    $1
else
    $2
    :
fi
])

#
# SYNOPSIS
#
#   ACX_MCRL2_LIBS([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_MCRL2_LIBS],[
AC_REQUIRE([ACX_MCRL2])dnl
if test x"$acx_mcrl2" = xyes; then
    AC_LANG_PUSH([C++])
    AX_LET([CPPFLAGS], ["$MCRL2_CPPFLAGS $CPPFLAGS"],
           [LIBS], ["$LIBS"],
           [LDFLAGS], ["$MCRL2_LDFLAGS $LDFLAGS"],
      [acx_mcrl2_libs=yes
       AC_CHECK_LIB([mcrl2], [ATinit],
         [MCRL2_LIBS="-lmcrl2 $MCRL2_LIBS"],
         [acx_mcrl2_libs=no],
         [$MCRL2_LIBS])])
    AC_LANG_POP([C++])
    AC_SUBST(MCRL2_LIBS)
fi
if test x"$acx_mcrl2_libs" = xyes; then
  ifelse([$1],,
         [AC_SUBST(CPPFLAGS, ["$MCRL2_CPPFLAGS $CPPFLAGS"])
          AC_SUBST(LDFLAGS,  ["$MCRL2_LDFLAGS $LDFLAGS"])
          AC_SUBST(LIBS,     ["$MCRL2_LIBS $LIBS"])],
         [$1])
  :
else
  $2
  :
fi
])



