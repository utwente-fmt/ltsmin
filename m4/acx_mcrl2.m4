#serial 3
# Author: Michael Weber <michaelw@cs.utwente.nl>
# Modified by Stefan Blom and Jeroen Ketema.
#
# SYNOPSIS
#
#   ACX_MCRL2([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_MCRL2], [
AC_ARG_WITH([mcrl2],
  [AS_HELP_STRING([--with-mcrl2=<prefix>],[mCRL2 prefix directory])])
AC_ARG_VAR([MCRL2], [some mCRL2 command])
case "$with_mcrl2" in
  no) acx_mcrl2=no ;;
  '') AC_PATH_TOOL(MCRL2, ["${MCRL2:-mcrl22lps}"], [""])
      if test x"$MCRL2" != x; then
        acx_mcrl2=yes
        with_mcrl2="$(dirname "$MCRL2")/.."
      fi
      ;;
   *) acx_mcrl2=yes;;
esac

AS_IF([test "x$acx_mcrl2" = "xyes"], [
    AC_LANG_PUSH([C++])

    MCRL2_PINS_CXXFLAGS=""
    AC_CHECK_SIZEOF([void *])
    case "$ac_cv_sizeof_void_p" in
      4) MCRL2_PINS_CPPFLAGS="-DAT_32BIT" ;;
      8) MCRL2_PINS_CPPFLAGS="-DAT_64BIT" ;;
      *) AC_MSG_FAILURE([can only compile mCRL2 on 32- and 64-bit machines.]) ;;
    esac

    AC_ARG_ENABLE([mcrl2-jittyc], [AS_HELP_STRING([--disable-mcrl2-jittyc], [
        disable the mCRL2 jittyc rewriter, making jitty the default rewriter])])
    AS_IF([test "x$enable_mcrl2_jittyc" = "xno"], [
        AC_SUBST(MCRL2_PINS_CPPFLAGS, ["$MCRL2_PINS_CPPFLAGS -DDISABLE_JITTYC"])
        AC_MSG_NOTICE([disabling mCRL2 jittyc rewriter])
    ])

    mcrl2_lib_dir=""
    if test -d ${with_mcrl2}/lib/mcrl2; then
        mcrl2_lib_dir="${with_mcrl2}/lib/mcrl2"
    else
        mcrl2_lib_dir="${with_mcrl2}/lib"
    fi

    #AX_CHECK_COMPILE_FLAG([-std=c++0x], [CXXFLAGS="$CXXFLAGS -std=c++0x"])
    AC_SUBST(MCRL2_PINS_CPPFLAGS, ["$MCRL2_PINS_CPPFLAGS -isystem$with_mcrl2/include"])
    AC_SUBST(MCRL2_PINS_LDFLAGS,  ["-L${mcrl2_lib_dir}"])
    AC_SUBST(MCRL2_LIBS, [""])
    AC_SUBST(MCRL2_LDFLAGS, ["$acx_cv_cc_export_dynamic -Wl,-rpath,${mcrl2_lib_dir}"])
    AC_SUBST(MCRL2, ["${with_mcrl2}/bin/mcrl22lps"])
    AC_SUBST(PBES, ["${with_mcrl2}/bin/lps2pbes"])

    mcrl2_pins_header=mcrl2/lps/ltsmin.h
    mcrl2_pins_cache_var=AS_TR_SH([ac_cv_header_$mcrl2_pins_header])
    pbes_header=mcrl2/pbes/pbes_explorer.h
    pbes_cache_var=AS_TR_SH([ac_cv_header_$pbes_header])
    # Check for C++ 11 support; needed for newer mCRL2
    save_CXXFLAGS="$CXXFLAGS"
    AX_CXX_COMPILE_STDCXX_11([noext], [optional])
    ac_compile_cxx11=$ac_success
    CXX11CXXFLAGS="$CXXFLAGS"
    CXXFLAGS="$save_CXXFLAGS"

    if test x"$ac_compile_cxx11" = xyes; then
      AC_SUBST(MCRL2_PINS_CXXFLAGS, ["$MCRL2_PINS_CXXFLAGS $CXX11CXXFLAGS"])
      $as_unset $mcrl2_pins_cache_var
      $as_unset $pbes_cache_var
      AX_LET([CPPFLAGS], ["$MCRL2_PINS_CPPFLAGS $CPPFLAGS"],
             [CXXFLAGS], ["$MCRL2_PINS_CXXFLAGS $CXXFLAGS"],
        [AC_CHECK_HEADER([$mcrl2_pins_header],[acx_mcrl2=yes],[acx_mcrl2=no]
         )])

    AX_LET([CPPFLAGS], ["$MCRL2_PINS_CPPFLAGS $CPPFLAGS"],
             [CXXFLAGS], ["$MCRL2_PINS_CXXFLAGS $CXXFLAGS"],
        [AC_CHECK_HEADER([$pbes_header],[acx_pbes=yes],[acx_pbes=no]
         )])
    fi

    if test x"$acx_mcrl2" = xno; then
	AC_MSG_FAILURE([cannot find mCRL2 headers, see README on how to install mCRL2 properly.])
    fi
    if test x"$acx_pbes" = xno; then
	AC_MSG_WARN([cannot find headers for the PBES greybox, installing without PBES support.])
    fi

    AC_LANG_POP([C++])

    $1
], [
    $2
    :
])
])

#
# SYNOPSIS
#
#   ACX_MCRL2_LIBS([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
#
AC_DEFUN([ACX_MCRL2_LIBS],[
AC_REQUIRE([ACX_MCRL2])dnl
if test x"$acx_mcrl2" = xyes; then
    AC_LANG_PUSH([C])
    AX_LET([LIBS], ["$LIBS"],
           [LDFLAGS], ["$MCRL2_PINS_LDFLAGS $LDFLAGS"],
      [acx_mcrl2_libs=yes
      ])
    AC_LANG_POP([C])

    AC_LANG_PUSH([C++])
    AX_LET([LIBS], ["$MCRL2_PINS_LIBS $LIBS"],
           [LDFLAGS], ["$MCRL2_PINS_LDFLAGS $LDFLAGS"],
           [CXXFLAGS], ["$MCRL2_PINS_CXXFLAGS $CXXFLAGS"],
       [AX_CXX_CHECK_LIB([mcrl2_syntax], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_syntax $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_syntax $LIBS"])
       AX_CXX_CHECK_LIB([mcrl2_utilities], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_utilities $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_utilities $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_atermpp], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_atermpp $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_atermpp $LIBS"])
       AX_CXX_CHECK_LIB([mcrl2_core], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_core $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_core $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_data], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_data $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_data $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_process], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_process $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_process $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_lps], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_lps $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_lps $LIBS"],
         [acx_mcrl2_libs=no])
       AX_CXX_CHECK_LIB([mcrl2_modal_formula], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_modal_formula $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_modal_formula $LIBS"])
       AX_CXX_CHECK_LIB([mcrl2_bes], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_bes $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_bes $LIBS"])
       AX_CXX_CHECK_LIB([mcrl2_pbes], [main],
         [MCRL2_PINS_LIBS="-lmcrl2_pbes $MCRL2_PINS_LIBS"
          LIBS="-lmcrl2_pbes $LIBS"],
         [acx_mcrl2_libs=no])
      ])
    AC_LANG_POP([C++])

    AC_SUBST(MCRL2_PINS_LIBS)
fi
if test x"$acx_mcrl2_libs" = xyes; then :
  ifelse([$1],,
         [AC_SUBST(CPPFLAGS, ["$MCRL2_PINS_CPPFLAGS $CPPFLAGS"])
          AC_SUBST(CXXFLAGS, ["$MCRL2_PINS_CXXFLAGS $CXXFLAGS"])
          AC_SUBST(LDFLAGS,  ["$MCRL2_PINS_LDFLAGS $LDFLAGS"])
          AC_SUBST(LIBS,     ["$MCRL2_PINS_LIBS $LIBS"])
          AC_SUBST(LDFLAGS,  ["$MCRL2_LDFLAGS $LDFLAGS"])
          AC_SUBST(LIBS,     ["$MCRL2_LIBS $LIBS"])],
         [$1])
else :
  $2
fi
])
