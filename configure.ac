AC_INIT([librtmpcast], [1.0.0], [kennedy.greg@gmail.com])

AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC

# librtmp is mandatory
PKG_CHECK_MODULES([RTMP],[librtmp])

# libx264 support
AC_ARG_WITH([libx264],
    AS_HELP_STRING([--without-libx264], [Ignore presence of libx264 and disable it]))

AS_IF([test "x$with_libx264" != "xno"],
      [PKG_CHECK_MODULES([X264], [x264], [have_libx264=yes], [have_libx264=no])],
      [have_libx264=no])

AS_IF([test "x$have_libx264" = "xno" && test "x$with_libx264" = "xyes"],
      [AC_MSG_ERROR([libx264 requested but not found])])

AM_CONDITIONAL([X264], [test "x$have_libx264" = "xyes"])

# libfdk_aac support
AC_ARG_WITH([libfdk_aac],
    AS_HELP_STRING([--without-libfdk_aac], [Ignore presence of libfdk_aac and disable it]))

AS_IF([test "x$with_libfdk_aac" != "xno"],
      [PKG_CHECK_MODULES([FDK_AAC], [fdk-aac], [have_libfdk_aac=yes], [have_libfdk_aac=no])],
      [have_libfdk_aac=no])

AS_IF([test "x$have_libfdk_aac" = "xno" && test "x$with_libfdk_aac" = "xyes"],
      [AC_MSG_ERROR([libfdk_aac requested but not found])])

AM_CONDITIONAL([FDK_AAC], [test "x$have_libfdk_aac" = "xyes"])


##

AC_PROG_LIBTOOL

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
