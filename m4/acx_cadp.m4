#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
# 
#   ACX_CAPD([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_CADP], [
AC_ARG_WITH([cadp],
  [AS_HELP_STRING([--with-cadp=<prefix>],[CADP prefix directory])])
AC_SUBST(CADP,[$CADP])
case "$with_cadp" in
  no|disable) CADP= ;;
  '') ;;
  *) CADP="$with_cadp"; export CADP ;;
esac
AC_MSG_CHECKING([for CADP])
if test x"$CADP" != x && test -f "$CADP/com/arch"; then
    AC_MSG_RESULT([$CADP])
    AC_CHECK_SIZEOF([void *])
    if test x"$ac_cv_sizeof_void_p" = x8; then
       AC_MSG_CHECKING([for 64 bit CADP architecture string])
       AC_SUBST(CADP_ARCH, ["$(CADP_BITS=64 $CADP/com/arch)"])
    else
        AC_MSG_CHECKING([for 32 bit CADP architecture string])
        AC_SUBST(CADP_ARCH, ["$(CADP_BITS=32 $CADP/com/arch)"])
    fi
    if test x"$CADP_ARCH" != x && \
       test x"$CADP_ARCH" != x"UNKNOWN ARCHITECTURE"; then
        AC_MSG_RESULT([$CADP_ARCH])
        AC_SUBST(CADP_LDFLAGS,  ["-L$CADP/bin.$CADP_ARCH"])
        AC_SUBST(CADP_CPPFLAGS, ["-I$CADP/incl"])
        acx_cadp=yes
    else
        AC_MSG_RESULT([unknown])
        acx_cadp=no
        CADP=
    fi
else
    AC_MSG_RESULT([no])
fi

if test x"$acx_cadp" = xyes; then
    $1
    :
else
    $2
    :
fi
])

#
# SYNOPSIS
# 
#   ACX_CAPD_BCG_WRITE([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_CADP_BCG_WRITE],[
AC_REQUIRE([ACX_CADP])dnl
if test "x$acx_cadp" = xyes; then
    AC_LANG_SAVE
    AC_LANG_C

    AX_LET([CPPFLAGS], ["$CADP_CPPFLAGS $CPPFLAGS"],
           [LIBS], ["$LIBS"],
           [LDFLAGS], ["$CADP_LDFLAGS $LDFLAGS"],
      [AC_CHECK_HEADER([bcg_user.h],
         [acx_cadp_have_bcg=yes],
         [acx_cadp_have_bcg=no])
       AC_CHECK_LIB([m], [cos],
         [CADP_LIBS="-lm $CADP_LIBS"],
         [],
         [$CADP_LIBS])
       AC_CHECK_LIB([BCG], [BCG_INIT],
         [CADP_LIBS="-lBCG $CADP_LIBS"],
         [acx_cadp_have_bcg=no],
         [$CADP_LIBS])
       AC_CHECK_LIB([BCG_IO], [BCG_IO_WRITE_BCG_BEGIN],
         [CADP_LIBS="-lBCG_IO $CADP_LIBS"],
         [acx_cadp_have_bcg=no],
         [$CADP_LIBS])])

    AS_IF([test x"$acx_cadp_have_bcg" = xyes],
      [AC_DEFINE([HAVE_BCG_USER_H], [1],
         [have <bcg_user.h> and can link against the BCG libraries])])
    AC_LANG_RESTORE

    AC_SUBST(CADP_LIBS)
fi
if test x"$acx_cadp_have_bcg" = xyes; then
    $1
    :
else
    $2
    :
fi
])
