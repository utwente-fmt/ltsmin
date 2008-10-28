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
    AC_MSG_CHECKING([for CADP architecture string])
    AC_SUBST(CADP_ARCH, ["$($CADP/com/arch)"])
    if test x"$CADP_ARCH" != x; then
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
AC_REQUIRE([ACX_CADP])
if test "x$acx_cadp" = xyes; then
    AC_LANG_SAVE
    AC_LANG_C

    acx_cadp_save_CPPFLAGS="$CPPFLAGS"
    acx_cadp_save_LIBS="$LIBS"
    acx_cadp_save_LDFLAGS="$LDFLAGS"
    CPPFLAGS="$CADP_CPPFLAGS $CPPFLAGS"
    LDFLAGS="$CADP_LDFLAGS $LDFLAGS"
    AC_CHECK_HEADERS([bcg_user.h])
    AC_CHECK_LIB([m], [cos],
      [CADP_LIBS="-lm $CADP_LIBS"])
    AC_CHECK_LIB([BCG], [BCG_INIT],
      [CADP_LIBS="-lBCG $CADP_LIBS"],
      [],
      [$CADP_LIBS])
    AC_CHECK_LIB([BCG_IO], [BCG_IO_WRITE_BCG_BEGIN],
      [CADP_LIBS="-lBCG_IO $CADP_LIBS"
       acx_cadp_have_bcg=yes],
      [acx_cadp_have_bcg=],
      [$CADP_LIBS])

    CPPFLAGS="$acx_cadp_save_CPPFLAGS"
    LDFLAGS="$acx_cadp_save_LDFLAGS"

    AC_LANG_RESTORE

    AC_SUBST(CADP_LIBS)

    if test x"$acx_cadp_have_bcg" = xyes; then
        $1
        :
    else
        $2
        :
    fi
fi
])
