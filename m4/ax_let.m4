#serial 1
# Author: Michael Weber <michaelw@cs.utwente.nl>
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
ax_let_$2_[]AX_LET_counter_[]_tmp_=$3
AX_LET_AUX([m4_if([$1],[],[],[$1
])dnl
ax_let_$2_[]AX_LET_counter_="[$]$2"
$2="[$]ax_let_$2_[]AX_LET_counter_[]_tmp_"], m4_shiftn(3,$@))
$2="[$]ax_let_$2_[]AX_LET_counter_"])])dnl

AC_DEFUN([AX_LET],
[m4_define([AX_LET_counter_], m4_incr(AX_LET_counter_))dnl
# AX_LET
AX_LET_AUX([],$@)])
