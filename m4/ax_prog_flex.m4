#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   AX_CHECK_FLEX([LEX-SPEC, [ACTION-IF-OK[, ACTION-IF-NOT-OK]]])
#
AC_DEFUN([AX_CHECK_FLEX],
[AM_PROG_LEX
AC_CACHE_CHECK([whether lexer generator is compatible], [ax_cv_check_flex_compat],
[
cat > conftest.lex <<AXEOF
ifelse([$1],,[
%%],[$1])
AXEOF
ac_try='"$LEX" --stdout conftest.lex >/dev/null 2>&AS_MESSAGE_LOG_FD'
eval ac_try_echo="\":$LINENO: $ac_try\""
echo "$ac_try_echo" >&AS_MESSAGE_LOG_FD
(eval "$ac_try") 2>&AS_MESSAGE_LOG_FD
if test $? == 0; then
    ax_cv_check_flex_compat=yes
else
    ax_cv_check_flex_compat=no
    echo "failed lexer spec was:" >&AS_MESSAGE_LOG_FD
    sed 's/^/| /' conftest.lex >&AS_MESSAGE_LOG_FD
fi
])
if test x"$ax_cv_check_flex_compat" = xyes; then
    $2
    :
else
    $3
    :
fi
])
