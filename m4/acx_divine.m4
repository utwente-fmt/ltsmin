#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   ACX_DIVINE([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_DIVINE], [
AC_ARG_WITH([divine],
  [AS_HELP_STRING([--with-divine=<prefix>],[DiVinE prefix directory])])
AC_ARG_VAR([DIVINE_PRECOMPILE], [path to DiVinE 'precompile' command])
case "$with_divine" in
  no) acx_divine=no ;;
  '') AC_PATH_TOOL(DIVINE_PRECOMPILE, ["${DIVINE_PRECOMPILE:-divine.precompile}"], [""])
      if test x"$DIVINE_PRECOMPILE" != x; then
        acx_divine=yes
        with_divine="$(dirname "$DIVINE_PRECOMPILE")/.."
      fi
      ;;
   *) acx_divine=yes;;
esac

if test x"$acx_divine" = xyes; then
    AC_SUBST(DIVINE_CPPFLAGS, 
      ["$DIVINE_CPPFLAGS -I${with_divine}/include/divine-cluster"])
    AC_SUBST(DIVINE_LDFLAGS,  ["$DIVINE_LDFLAGS -L${with_divine}/lib"])
    $1
else
    $2
    :
fi
])

#
# SYNOPSIS
#
#   ACX_DVEC_LIBS([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_DVEC_LIBS], [
AC_REQUIRE([ACX_DIVINE])dnl
if test x"$acx_divine" = xyes; then
    AC_LANG_PUSH([C++])
    AX_LET([CPPFLAGS], ["$DIVINE_CPPFLAGS $CPPFLAGS"],
           [LIBS], ["$LIBS"],
           [LDFLAGS], ["$DIVINE_LDFLAGS $LDFLAGS"],
      [acx_dvec_libs=yes
       AC_CHECK_LIB([sevine], [main],
         [DVEC_LIBS="-lsevine $DVEC_LIBS"],
         [acx_dvec_libs=no],
         [$DVEC_LIBS])])
    AC_LANG_POP([C++])
    AC_SUBST(DVEC_LIBS)
fi
if test x"$acx_dvec_libs" = xyes; then
  ifelse([$1],,
         [AC_SUBST(CPPFLAGS, ["$DIVINE_CPPFLAGS $CPPFLAGS"])
          AC_SUBST(LDFLAGS,  ["$DIVINE_LDFLAGS $LDFLAGS"])
          AC_SUBST(LIBS,     ["$DVEC_LIBS $LIBS"])],
         [$1])
  :
else
  $2
  :
fi
])



