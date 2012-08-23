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
  '') TRY_LIBDDD_CPPFLAGS="$CPPFLAGS -I/usr/local/include" ;;
  no) TRY_LIBDDD_CPPFLAGS="" ;;
   *) TRY_LIBDDD_CPPFLAGS="-I$with_libddd/include" ;;
esac

  
acx_libddd=no
dnl libddd should be used via #include <ddd/DDD.h>, but this does not work
dnl currently because internal libddd header files expect the "ddd" directory
dnl in the include path.  Hence, some hoops to autodetect where libddd is
dnl installed.
for f in $TRY_LIBDDD_CPPFLAGS; do
    case "$f" in
      -I*) if test -f "${f#-I}/ddd/DDD.h"; then
             LIBDDD_INCLUDE="${f#-I}"
             AC_SUBST([DDD_CPPFLAGS],[$f/ddd])
             AC_SUBST([LIBDDD],[${LIBDDD_INCLUDE/\/include/}])
             AC_SUBST([DDD_LDFLAGS],[-L${LIBDDD}/lib])
             acx_libddd=yes
             break
           fi
           ;;
    esac
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
fi

if test x"$acx_libddd" = xyes; then
    $1
else
    $2
    AC_SUBST([LIBDDD],[""])
fi
])
