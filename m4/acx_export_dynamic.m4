#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
#
# SYNOPSIS
#
#   ACX_EXPORT_DYNAMIC
#
AC_DEFUN([ACX_EXPORT_DYNAMIC], [
AC_REQUIRE([AC_PROG_LIBTOOL])
LT_OUTPUT
AC_CACHE_CHECK([whether -export-dynamic is supported], [acx_cv_cc_export_dynamic],
[acx_cv_cc_export_dynamic="$((./libtool --config; echo eval echo \\$export_dynamic_flag_spec) | sh)"
])])
