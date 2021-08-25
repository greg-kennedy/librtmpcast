ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -Wall -Wextra

lib_LTLIBRARIES = librtmpcast.la
librtmpcast_la_SOURCES = rtmpcast.c
librtmpcast_la_CFLAGS = $(RTMP_CFLAGS) $(X264_CFLAGS) $(FDK_AAC_CFLAGS)
librtmpcast_la_LDFLAGS = -version-info 1:0:0 $(RTMP_LIBS) $(X264_LIBS) $(FDK_AAC_LIBS)

noinst_PROGRAMS = example

example_SOURCES = example.c
example_LDADD = $(lib_LTLIBRARIES)
example_DEPENDENCIES = $(lib_LTLIBRARIES)

