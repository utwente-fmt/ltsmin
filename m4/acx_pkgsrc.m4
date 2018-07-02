#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   ACX_PKGSRC([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_PKGSRC], [
AC_ARG_WITH([pkgsrc],
  [AS_HELP_STRING([--with-pkgsrc=<prefix>],
    [NetBSD pkgsrc prefix directory @<:@default=/usr/pkg@:>@])],
  [],
  [with_pkgsrc=/usr/pkg
   acx_pkgsrc=check])

case "$with_pkgsrc" in
  no|disable) acx_pkgsrc=no ;;
  *) if test -f "$with_pkgsrc/etc/mk.conf"; then
        acx_pkgsrc=yes
     else
        if test x"$acx_pkgsrc" != xcheck; then
            AC_MSG_WARN(
                [--with-pkgsrc was given, but test for $with_pkgsrc/etc/mk.conf failed])
        fi
        acx_pkgsrc=no
     fi
     ;;
esac

if test x"$acx_pkgsrc" = xyes; then
    AC_SUBST([PKGSRC_PREFIX], ["$with_pkgsrc"])
    ifelse([$1],,
           [AC_SUBST(CPPFLAGS, ["-I${PKGSRC_PREFIX}/include $CPPFLAGS"])
            AC_SUBST(LDFLAGS,  ["-L${PKGSRC_PREFIX}/lib $LDFLAGS"])],
           [$1])
else
    $2
    :
fi
])
