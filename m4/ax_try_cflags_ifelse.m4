#serial 2
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   AX_TRY_CFLAGS_IFELSE(FLAGS...[, ACTION-IF-TRUE[, ACTION-IF-FALSE]])
#
AC_DEFUN([AX_TRY_CFLAGS_IFELSE],
[AX_LET([CFLAGS],["$CFLAGS $1"],
   [AC_MSG_CHECKING([whether compiler accepts $1])
    AC_LANG_PUSH(C)
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[return 0;])],
      [ax_try_cflags_ifelse_res=yes],
      [ax_try_cflags_ifelse_res=no])])
    AC_LANG_POP(C)
 AC_MSG_RESULT([$ax_try_cflags_ifelse_res])
 AS_IF([test x"$ax_try_cflags_ifelse_res" = xyes],
   [ifelse([$2],,
      [AC_SUBST(CFLAGS, ["$CFLAGS $1"])],
      [$2])
    :],
   [$3
    :])
])

# SYNOPSIS
#
#   AX_TRY_CXXFLAGS_IFELSE(FLAGS...[, ACTION-IF-TRUE[, ACTION-IF-FALSE]])
#
AC_DEFUN([AX_TRY_CXXFLAGS_IFELSE],
[AX_LET([CXXFLAGS],["$CXXFLAGS $1"],
   [AC_MSG_CHECKING([whether compiler accepts $1])
    AC_LANG_PUSH(C++)
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[return 0;])],
      [ax_try_cxxflags_ifelse_res=yes],
      [ax_try_cxxflags_ifelse_res=no])
    AC_LANG_POP(C++)])
 AC_MSG_RESULT([$ax_try_cxxflags_ifelse_res])
 AS_IF([test x"$ax_try_cxxflags_ifelse_res" = xyes],
   [ifelse([$2],,
      [AC_SUBST(CXXFLAGS, ["$CXXFLAGS $1"])],
      [$2])
    :],
   [$3
    :])
])
