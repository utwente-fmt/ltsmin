#serial 0
# Author: Vincent Bloemen <v.bloemen@utwente.nl>
#
# SYNOPSIS
# 
#   ACX_SPOT([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_SPOT], [

AC_ARG_WITH([spot],
  [AS_HELP_STRING([--with-spot=<prefix>],[Spot prefix directory])])

case "$with_spot" in
  '') CHECK_DIR="/usr/local /usr /opt/local /opt/install" ;;
  no) CHECK_DIR="" ;;
   *) CHECK_DIR="$with_spot" ;;
esac

# search for bddx.h (for bddx) and parse.hh (for Spot)
# NB: multiple header files are used from Spot, we only test one
acx_spot=no
for f in $CHECK_DIR; do
    if test -f "${f}/include/spot/tl/parse.hh" -a -f "${f}/include/bddx.h"; then
        LIBDDD_INCLUDE="${f}"
        AC_SUBST([SPOT_CPPFLAGS],["-I${f}/include -I${f}/include/spot -std=c++11"])
        AC_SUBST([SPOT_LIBS],["-lspot -lbddx"])
        AC_SUBST([SPOT_LDFLAGS],["-L${f}/lib"])
        acx_spot=yes
        break
    fi
done

])
