#serial 3
# Author: Alfons Laarman <a.w.laarman@ewi.utwente.nl>
#
# SYNOPSIS
#
#   ACX_DDD([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_DDD], [

AC_ARG_WITH([libddd],
  [AS_HELP_STRING([--with-libddd=<prefix>],[libDDD prefix directory])])

case "$with_libddd" in
  '') CHECK_DIR="/usr/local /usr /opt/local /opt/install" ;;
  no) CHECK_DIR="" ;;
   *) CHECK_DIR="$with_libddd" ;;
esac

  
acx_libddd=no
dnl libddd should be used via #include <ddd/DDD.h>, but this does not work
dnl currently because internal libddd header files expect the "ddd" directory
dnl in the include path.  Hence, some hoops to autodetect where libddd is
dnl installed.
for f in $CHECK_DIR; do
    if test -f "${f}/include/ddd/DDD.h"; then
        LIBDDD_INCLUDE="${f}"
        AC_SUBST([DDD_CPPFLAGS],["-I${f}/include/ddd"])
        AC_SUBST([LIBDDD],["${f}"])
        AC_SUBST([DDD_LDFLAGS],["-L${f}/lib"])
        acx_libddd=yes
        break
    fi
done
            
if test x"$acx_libddd"=xyes; then
    AC_SUBST([DDD_LIBS],[-lDDD])
    AC_LANG_PUSH([C++])
    AX_LET([CPPFLAGS], ["${DDD_CPPFLAGS} $CPPFLAGS"],
           [LDFLAGS], ["$DDD_LDFLAGS $LDFLAGS"],
           [LIBS], ["$DDD_LIBS $LIBS"], [
        AC_CHECK_HEADERS([DDD.h],acx_libddd=yes,acx_libddd=no)
        if test x"$acx_libddd" = xyes; then
            AX_CXX_CHECK_LIB([DDD],[GDDD::size() const], acx_libddd=yes, acx_libddd=no)
        fi
    ])
    AC_LANG_POP([C++])
    AC_SUBST(CPPFLAGS, ["${DDD_CPPFLAGS} $CPPFLAGS"])
    AC_SUBST(LDFLAGS,  ["${DDD_LDFLAGS} $LDFLAGS"])
    $1
else
    $2
    AC_SUBST([LIBDDD],[""])
fi
])
