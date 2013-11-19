#serial 2
#
# SYNOPSIS
#
#   AX_LET(VAR1, EXPR1,
#          [VAR2, EXPR2,]...
#     BODY)
#
m4_define([AX_LET_counter_],0)
m4_define([AX_LET_AUX], [dnl
m4_if([$#], 2, [$1
$2], [dnl
AS_VAR_PUSHDEF([tmpvar], [ax_let_$2_[]AX_LET_counter_[]_tmp_])dnl
AS_VAR_SET([tmpvar],[$3])
AX_LET_AUX([m4_if([$1],[],[],[$1
])dnl
AS_VAR_PUSHDEF([var],[ax_let_$2_[]AX_LET_counter_])dnl
AS_VAR_SET([var],[[$]$2])
AS_VAR_SET([$2],[$ax_let_$2_[]AX_LET_counter_[]_tmp_])], m4_shiftn(3,$@))
AS_VAR_SET([$2],[$var])
AS_VAR_POPDEF([var])dnl
AS_VAR_POPDEF([tmpvar])])])dnl

AC_DEFUN([AX_LET],
[m4_define([AX_LET_counter_], m4_incr(AX_LET_counter_))dnl
# AX_LET
AX_LET_AUX([],$@)])
