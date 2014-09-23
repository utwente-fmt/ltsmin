#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   ACX_SPINS([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_SPINS], [
AC_ARG_WITH([spins],
  [AS_HELP_STRING([--with-spins],
    [Compile SpinS])],
  [],
  [with_spins=check])

case "$with_spins" in
  check)
        AC_CHECK_PROGS(JAVAC, [javac])
        #CHECK_JDK # Does not find OpenJDK
        #if test x$found_jdk != xyes       
        if test x"$JAVAC" = x 
        then
            AC_MSG_WARN([No JDK found.])
        else
            acx_spins=yes
        fi
        case "$acx_spins" in
          yes)
                AC_ARG_VAR([ANT], [ANT command (Java build tool)])
                AC_PATH_TOOL(ANT, ["${ANT:-ant}"])
                if test -x "`command -v "$ANT" 2>/dev/null 2>&1`"
                then :
                else
                    AC_MSG_WARN([Apache Ant (Java build tool) not found.])
                    acx_spins=no
                fi
             ;;
          *) : ;;
        esac
     ;;
  yes) acx_spins=yes ;;
  *)   : ;;
esac
case "$acx_spins" in
  yes) :
    $1 ;;
  *) :
    $2 ;;
esac
])
