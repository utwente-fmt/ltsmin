#serial 3
# Author: Lieuwe Vinkhuijzen <l.t.vinkhuijzen@liacs.leidenuniv.nl>
#
# SYNOPSIS
#
#   ACX_SDD([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_SDD], [

AC_ARG_WITH([libsdd],
    [AS_HELP_STRING([--with-libsdd=<prefix>],[libSDD prefix directory])])

AC_ARG_WITH([libsdd],
    [AS_HELP_STRING([--without-libsdd], [do not include SDD (Sentential Decision Diagram) package])])

if test $with_libsdd = no; then
	CHECK_DIR=""
elif test $with_libsdd=""; then
	CHECK_DIR="/usr/local /opt/local /opt/install"
else
	CHECK_DIR=$with_libsdd
fi


acx_libsdd=no
for f in $CHECK_DIR; do
    if ((test -f "${f}/include/sddapi.h") && (test -f "${f}/lib/libsdd.a")); then
        LIBDDD_INCLUDE="${f}"
        AC_SUBST([SDD_CFLAGS],["-I${f}/include"])
        AC_SUBST([LIBSDD],["${f}"])
        AC_SUBST([SDD_LDFLAGS],["-L${f}/lib"])
        acx_libsdd=yes
        break
    fi
done

if test $acx_libsdd = yes; then
    AC_SUBST([SDD_LIBS],[-lSDD])
    AX_LET([CFLAGS], ["${SDD_CFLAGS} $CFLAGS"],
           [LDFLAGS], ["$SDD_LDFLAGS $LDFLAGS"],
           [LIBS], ["$SDD_LIBS $LIBS"], [
       AC_CHECK_HEADERS([sddapi.h],acx_libsdd=yes,acx_libsdd=no)
    ])
#    AC_SUBST([CFLAGS],["${SDD_CFLAGS} $CFLAGS"])
#    AC_SUBST([LDFLAGS],["$SDD_LDFLAGS $LDFLAGS"])
#    AC_SUBST([LIBS],["$SDD_LIBS $LIBS"])
    AC_SUBST(CPPFLAGS, ["${SDD_CFLAGS} $CPPFLAGS"])
    AC_SUBST(LDFLAGS,  ["${SDD_LDFLAGS} $LDFLAGS"])
    $1
else
    $2
    AC_SUBST([LIBSDD],[""])
fi
])
