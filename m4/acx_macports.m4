#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   ACX_MACPORTS([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_MACPORTS], [
AC_ARG_WITH([macports],
  [AS_HELP_STRING([--with-macports=<prefix>],
    [MacPorts prefix directory @<:@default=/opt/local@:>@])],
  [],
  [with_macports=/opt/local
   acx_macports=check])

case "$with_macports" in
  no|disable) acx_macports=no ;;
  *) if test -f "$with_macports/etc/macports/macports.conf"; then
        acx_macports=yes
     else
        if test x"$acx_macports" != xcheck; then
            AC_MSG_WARN(
                [--with-macports was given, but test for macports.conf failed])
        fi
        acx_macports=no
     fi
     ;;
esac

if test x"$acx_macports" = xyes; then
    AC_SUBST([MACPORTS_PREFIX], ["$with_macports"])
    ifelse([$1],,
           [AC_SUBST(CPPFLAGS, ["-I${MACPORTS_PREFIX}/include $CPPFLAGS"])
            AC_SUBST(LDFLAGS,  ["-L${MACPORTS_PREFIX}/lib $LDFLAGS"])],
           [$1])
else
    $2
    :
fi
])
