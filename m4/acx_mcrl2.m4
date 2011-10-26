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
    AX_LET([LIBS], ["$LIBS"],
           [LDFLAGS], ["$MCRL2_LDFLAGS $LDFLAGS"],
      [acx_mcrl2_libs=yes
       AX_CXX_CHECK_LIB([mcrl2_aterm], [main],
         [MCRL2_LIBS="-lmcrl2_aterm $MCRL2_LIBS"
          LIBS="-lmcrl2_aterm $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_utilities], [main],
         [MCRL2_LIBS="-lmcrl2_utilities $MCRL2_LIBS"
          LIBS="-lmcrl2_utilities $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_core], [main],
         [MCRL2_LIBS="-lmcrl2_core $MCRL2_LIBS"
          LIBS="-lmcrl2_core $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_data], [main],
         [MCRL2_LIBS="-lmcrl2_data $MCRL2_LIBS"
          LIBS="-lmcrl2_data $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_process], [main],
         [MCRL2_LIBS="-lmcrl2_process $MCRL2_LIBS"
          LIBS="-lmcrl2_process $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_lps], [main],
         [MCRL2_LIBS="-lmcrl2_lps $MCRL2_LIBS"
          LIBS="-lmcrl2_lps $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_syntax], [main],
         [MCRL2_LIBS="-lmcrl2_syntax $MCRL2_LIBS"
          LIBS="-lmcrl2_syntax $LIBS"])
      ])
    AC_LANG_POP([C++])

    AC_LANG_PUSH([C])
        AX_LET([CFLAGS], ["$MCRL2_CFLAGS $CFLAGS"],
           [LIBS], ["$LIBS"],
           [LDFLAGS], ["$MCRL2_LDFLAGS $LDFLAGS"],
      [AC_CHECK_LIB([dparser], [main],
         [MCRL2_LIBS="-ldparser $MCRL2_LIBS"
          LIBS="-ldparser $LIBS"])
      ])
    AC_LANG_POP([C])
    AC_SUBST(MCRL2_LIBS)
fi
if test x"$acx_mcrl2_libs" = xyes; then :
  ifelse([$1],,
         [AC_SUBST(CPPFLAGS, ["$MCRL2_CPPFLAGS $CPPFLAGS"])
          AC_SUBST(LDFLAGS,  ["$MCRL2_LDFLAGS $LDFLAGS"])
          AC_SUBST(LIBS,     ["$MCRL2_LIBS $LIBS"])],
         [$1])
else :
  $2
fi
])
