#serial 2
# Author: Alfons Laarman <a.w.laarman@cs.utwente.nl>
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   ACX_DIVINE2([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_DIVINE2], [
AC_ARG_WITH([divine2],
  [AS_HELP_STRING([--with-divine2=<prefix>],[DiVinE 2.2 prefix directory])])
AC_ARG_VAR([DIVINE2_PRECOMPILE], [path to DiVinE 2.2 'divine' command])
case "$with_divine2" in
  no) acx_divine2=no ;;
  '') AC_PATH_TOOL(DIVINE2_PRECOMPILE, ["${DIVINE2_PRECOMPILE:-divine}"], [""])
      if test x"$DIVINE2_PRECOMPILE" != x; then
        acx_divine2=yes
        with_divine2="$(dirname "$DIVINE2_PRECOMPILE")/.."
      fi
      ;;
   *) acx_divine2=yes;;
esac

if test x"$acx_divine2" = xyes; then
    AC_SUBST(DIVINE2_CPPFLAGS, 
      ["$DIVINE2_CPPFLAGS -I${with_divine2}/include"])
    AC_SUBST(DIVINE2_LDFLAGS,  ["$DIVINE2_LDFLAGS -L${with_divine2}/lib"])
    $1
else
    $2
    :
fi
])

#
# SYNOPSIS
#
#   ACX_DVEC2_LIBS([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_DVEC2_LIBS], [
AC_REQUIRE([ACX_DIVINE2])dnl
if test x"$acx_divine2" = xyes; then
    AC_LANG_PUSH([C])
    AX_LET([CPPFLAGS], ["$DIVINE2_CPPFLAGS $CPPFLAGS"],
           [LIBS], ["$LIBS"],
           [LDFLAGS], ["$DIVINE2_LDFLAGS $LDFLAGS"],
      [acx_dvec2_libs=yes
       AC_CHECK_LIB([dl], [dlopen],
         [MCRL_LIBS="-ldl $MCRL_LIBS"],
         [acx_dvec2_libs=no],
         [$DVEC2_LIBS])
       ])
    AC_LANG_POP([C])
    AC_SUBST(DVEC2_LIBS)
fi
if test x"$acx_dvec2_libs" = xyes; then
  ifelse([$1],,
         [AC_SUBST(CPPFLAGS, ["$DIVINE2_CPPFLAGS $CPPFLAGS"])
          AC_SUBST(LDFLAGS,  ["$DIVINE2_LDFLAGS $LDFLAGS"])
          AC_SUBST(LIBS,     ["$DVEC2_LIBS $LIBS"])],
         [$1])
  :
else
  $2
  :
fi
])



