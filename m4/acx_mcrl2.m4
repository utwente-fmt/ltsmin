#serial 2
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
    AC_SUBST(MCRL2_CPPFLAGS, ["$MCRL2_CPPFLAGS -I$with_mcrl2/include"])
    AC_SUBST(MCRL2_LDFLAGS,  ["-L${with_mcrl2}/lib/mcrl2"])
    AX_LET([CPPFLAGS], ["$MCRL2_CPPFLAGS $CPPFLAGS"],
      [AC_CHECK_HEADER([mcrl2/lps/ltsmin.h],,
         [AC_MSG_FAILURE([cannot find mCRL2 headers,
see README on how to install mCRL2 properly.])]
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
           [LIBS], ["-lmcrl2_utilities -lmcrl2_data -lmcrl2_core -lmcrl2_aterm $LIBS"],
           [LDFLAGS], ["$MCRL2_LDFLAGS $LDFLAGS"],
      [acx_mcrl2_libs=yes
       AX_CXX_CHECK_LIB([mcrl2_lps], [main], dnl XXX
         [MCRL2_LIBS="-lmcrl2_lps -lmcrl2_utilities -lmcrl2_process -lmcrl2_data -lmcrl2_core -lmcrl2_aterm"
          LIBS="-lmcrl2_lps $LIBS"],
         [acx_mcrl2_libs=no])
      ])
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
