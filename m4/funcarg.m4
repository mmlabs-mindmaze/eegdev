

# AC_SEARCH_LIBS_FNARG(FUNCTION, SEARCH-LIBS, [ARGS], [INCLUDES],
#                [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND],
#                [OTHER-LIBRARIES])
# --------------------------------------------------------
# Search for a library defining FUNC, if it's not already available.
AC_DEFUN([AC_SEARCH_LIBS_FNARG],
[AS_VAR_PUSHDEF([ac_Search], [ac_cv_search_$1])dnl
AC_CACHE_CHECK([for library containing $1], [ac_Search],
[ac_func_search_save_LIBS=$LIBS
codeline="$1($3);"
AC_LANG_CONFTEST([AC_LANG_PROGRAM([$4], [[$codeline]])])
for ac_lib in '' $2; do
  if test -z "$ac_lib"; then
    ac_res="none required"
  else
    ac_res=-l$ac_lib
    LIBS="-l$ac_lib $7 $ac_func_search_save_LIBS"
  fi
  AC_LINK_IFELSE([], [AS_VAR_SET([ac_Search], [$ac_res])])
  AS_VAR_SET_IF([ac_Search], [break])
done
AS_VAR_SET_IF([ac_Search], , [AS_VAR_SET([ac_Search], [no])])
rm conftest.$ac_ext
LIBS=$ac_func_search_save_LIBS])
AS_VAR_COPY([ac_res], [ac_Search])
AS_IF([test "$ac_res" != no],
  [test "$ac_res" = "none required" || LIBS="$ac_res $LIBS"
   $5],
      [$6])
AS_VAR_POPDEF([ac_Search])dnl
])

# AC_CHECK_FUNC_FNARG(FUNCTION, [ARGS], [INCLUDES], 
#                     [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
# -----------------------------------------------------------------
# Check whether FUNCTION links in the current language. Execute
# ACTION-IF-FOUND or ACTION-IF-NOT-FOUND.
AC_DEFUN([AC_CHECK_FUNC_FNARG],
[AC_MSG_CHECKING($1)
codeline="$1($2);"
AC_LINK_IFELSE(AC_LANG_PROGRAM([$3],[$codeline]), 
                            [ac_func_fnarg_found=yes], 
			    [ac_func_fnarg_found=no])
AC_MSG_RESULT([$ac_func_fnarg_found])
AS_IF([test "$ac_func_fnarg_found" != no],
      [AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_]$1),[1], [Define to 1 if $1 is present]) $4],
      [$5])
])


