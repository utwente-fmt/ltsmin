#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   AX_CC_COMPILE_CXX_LINK()
#
AC_DEFUN([AX_CC_COMPILE_CXX_LINK],
[AC_CACHE_CHECK([whether C++ linker can link C objects], [ax_cv_cc_compile_cxx_link],
[AC_LANG_PUSH(C)
AC_LANG_CONFTEST([AC_LANG_SOURCE([[extern void conftest() { return; }]])])
$CC $CFLAGS -c conftest.c && mv conftest.${OBJEXT-o} conftest$$.${OBJEXT-o}
AC_LANG_PUSH(C++)
AX_LET([LIBS],["conftest$$.${OBJEXT-o} $LIBS"],
  [AC_LINK_IFELSE([AC_LANG_PROGRAM(
       [extern "C" {
        extern void conftest();
        }], [conftest();])],
     [rm -f conftest$$.${OBJEXT-o}
      ax_cv_cc_compile_cxx_link=yes],
     [rm -f conftest$$.${OBJEXT-o}
      AC_MSG_FAILURE([cannot link C objects with C++ linker.])])])
AC_LANG_POP(C++)
AC_LANG_POP(C)
])])
