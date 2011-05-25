#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   AX_CHECK_FUNC_INCLUDE(INCLUDES,FUNCTION,[ACTION-IF-TRUE,[ACTION-IF-FALSE]])
#
AC_DEFUN([AX_CHECK_FUNC_INCLUDE],
[AC_CACHE_CHECK([for $2],[ax_cv_fn_$2],[
 ax_cv_fn_$2=no
 AC_TRY_LINK([$1],[$2],[ax_cv_fn_$2=yes])])
 if test x"[$]ax_cv_fn_$2" = xyes; then :
   $3
 else :
   $4
 fi
])
