#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   ACX_FINK([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_FINK], [
AC_ARG_WITH([fink],
  [AS_HELP_STRING([--with-fink=<prefix>],
    [Fink prefix directory @<:@default=/sw@:>@])],
  [],
  [AC_PATH_TOOL([FINK], [fink], ["/sw/bin/fink"])
   with_fink="$(dirname "$(dirname "$FINK")")"
   acx_fink=check])

case "$with_fink" in
  no|disable) acx_fink=no ;;
  *) if test -f "$with_fink/etc/fink.conf"; then
        acx_fink=yes
     else
        if test x"$acx_fink" != xcheck; then
            AC_MSG_WARN([--with-fink was given, but test for fink.conf failed])
        fi
        acx_fink=no
     fi
     ;;
esac

if test x"$acx_fink" = xyes; then
    AC_SUBST([FINK_PREFIX], ["$with_fink"])
    ifelse([$1],,
           [AC_SUBST(CPPFLAGS, ["-I${FINK_PREFIX}/include $CPPFLAGS"])
            AC_SUBST(LDFLAGS,  ["-L${FINK_PREFIX}/lib $LDFLAGS"])],
           [$1])
else
    $2
    :
fi
])
