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
        
        # check if the version of Spot is correct
        version="2.0"          
        AC_MSG_NOTICE([found Spot installation at ${f}])
        AC_MSG_CHECKING([for Spot version == $version])
        spot_conf="${f}/include/spot/misc/_config.h"
        spot_v="SPOT_VERSION \"$version\""
        version_check=""

        # check if spot_conf can be found 
        if test -f "$spot_conf"; then
          version_check=`grep "$spot_v" "$spot_conf"`
        fi

        # check if the version is correct
        if test -n "$version_check"; then

          AC_MSG_RESULT([yes])
          LIBDDD_INCLUDE="${f}"
          AC_SUBST([SPOT_CPPFLAGS],["-I${f}/include -I${f}/include/spot -std=c++11"])
          AC_SUBST([SPOT_LIBS],["-lspot -lbddx"])
          AC_SUBST([SPOT_LDFLAGS],["-L${f}/lib"])
          acx_spot=yes
          break
        else
          AC_MSG_RESULT([no valid version of Spot])
        fi
    fi
done

])
