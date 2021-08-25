AC_INIT([librtmpcast], [1.0.0], [kennedy.greg@gmail.com])

AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC

PKG_CHECK_MODULES([RTMP],[librtmp])
PKG_CHECK_MODULES([X264],[x264])
PKG_CHECK_MODULES([FDK_AAC],[fdk-aac])

AC_PROG_LIBTOOL

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
