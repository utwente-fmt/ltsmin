# SYNOPSIS
#
#   Test for ViennaCL
#
#   AX_PATH_VIENNACL( <Minimum Required Version>, <package-required=yes/no> )
#
# DESCRIPTION
#
#   Provides a --with-viennacl=DIR option. Searches --with-viennacl,
#   $VIENNACL_DIR, and the usual places for VIENNACL headers and libraries.
#
#   On success, sets VIENNACL_CPPFLAGS, VIENNACL_LIBS, and #defines HAVE_VIENNACL.
#   Also defines automake conditional VIENNACL_ENABLED.  Assumes package
#   is optional unless overridden with $2=yes.
#
# LAST MODIFICATION
#
#   $Id: viennacl.m4 -1   $
#
# COPYLEFT
#
#   Copyright (c) 2013 Roy H. Stogner <roystgnr@ices.utexas.edu>
#   Copyright (c) 2012 Paul T. Bauman <pbauman@ices.utexas.edu>
#   Copyright (c) 2010 Karl W. Schulz <karl@ices.utexas.edu>
#   Copyright (c) 2009 Rhys Ulerich <rhys.ulerich@gmail.com>
#   Copyright (c) 2008 Thomas Porschberg <thomas@randspringer.de>
#   Copyright (c) 2008 Caolan McNamara <caolan@skynet.ie>
#   Copyright (c) 2008 Alexandre Duret-Lutz <adl@gnu.org>
#   Copyright (c) 2008 Matthew Mueller <donut@azstarnet.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AX_PATH_VIENNACL],
[

AC_ARG_VAR(VIENNACL_DIR,[root directory of ViennaCL installation])

AC_ARG_WITH(viennacl,
  [AS_HELP_STRING([--with-viennacl[=DIR]],[root directory of ViennaCL installation (default = VIENNACL_DIR)])],
  [with_viennacl=$withval
if test "${with_viennacl}" != yes; then
    VIENNACL_PREFIX=$withval
elif test "x${VIENNACL_DIR}" != "x"; then
   VIENNACL_PREFIX=${VIENNACL_DIR}
else
    VIENNACL_PREFIX=/usr
fi
],[
with_viennacl=$withval
if test "x${VIENNACL_DIR}" != "x"; then
   VIENNACL_PREFIX=${VIENNACL_DIR}
fi
])

# package requirement; if not specified, the default is to assume that
# the package is optional

is_package_required=ifelse([$2], ,no, $2 )

HAVE_VIENNACL=0

# logic change: if the user called the macro, check for package,
# decide what to do based on whether the package is required or not.

# if test "${with_viennacl}" != no ; then

    # If we can see the ViennaCL headers, then we know where to get them
    # and we'll need C++11 to compile them
    if test -e "${VIENNACL_PREFIX}/viennacl/vector.hpp" ; then
       VIENNACL_CPPFLAGS="-isystem${VIENNACL_PREFIX}"
       AX_CXX_COMPILE_STDCXX_11(noext)
    fi

    ac_VIENNACL_save_CPPFLAGS="$CPPFLAGS"

    CPPFLAGS="${VIENNACL_CPPFLAGS} ${CPPFLAGS}"

    AC_LANG_PUSH([C++])
    AC_CHECK_HEADER([viennacl/vector.hpp],[found_header=yes],[found_header=no])
    AC_LANG_POP([C++])

    #-----------------------
    # Minimum version check skipped - there's no versioning
    # information in Viennacl headers as of 1.4.2
    #----------------------

    CPPFLAGS="$ac_VIENNACL_save_CPPFLAGS"

    succeeded=yes
    if test "$found_header" != yes; then
       succeeded=no
    fi
    if test "$no_cl" = yes; then
       succeeded=no
    else
       VIENNACL_CPPFLAGS="$VIENNACL_CPPFLAGS $CL_CFLAGS"
       VIENNACL_LIBS="$VIENNACL_LIBS $CL_LIBS"
    fi

    if test "$succeeded" = no; then
       if test "$is_package_required" = yes; then
          AC_MSG_ERROR([ViennaCL not found.  Try either --with-viennacl or setting VIENNACL_DIR.])
       else
          AC_MSG_NOTICE([optional ViennaCL library not found])
          VIENNACL_CPPFLAGS="" # VIENNACL_CPPFLAGS empty on failure
          VIENNACL_LDFLAGS="" # VIENNACL_LDFLAGS empty on failure
          VIENNACL_LIBS="" # VIENNACL_LIBS empty on failure
       fi
    else
        HAVE_VIENNACL=1
        AC_DEFINE(HAVE_VIENNACL,1,[Define if VIENNACL is available])
        AC_SUBST(VIENNACL_CPPFLAGS)
        AC_SUBST(VIENNACL_LDFLAGS)
        AC_SUBST(VIENNACL_LIBS)
        AC_SUBST(VIENNACL_PREFIX)
    fi

    AC_SUBST(HAVE_VIENNACL)

# fi

AM_CONDITIONAL(VIENNACL_ENABLED,test x$HAVE_VIENNACL = x1)

])