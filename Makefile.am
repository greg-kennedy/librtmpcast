ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -Wall -Wextra

lib_LTLIBRARIES = librtmpcast.la
librtmpcast_la_SOURCES = rtmpcast.c audio_passthrough.c
librtmpcast_la_CFLAGS = $(RTMP_CFLAGS)
librtmpcast_la_LDFLAGS = -version-info 1:0:0 $(RTMP_LIBS)

if X264
librtmpcast_la_SOURCES += video_x264.c
librtmpcast_la_CFLAGS += $(X264_CFLAGS)
librtmpcast_la_LDFLAGS += $(X264_LIBS)
endif

if FDK_AAC
librtmpcast_la_SOURCES += audio_fdkaac.c
librtmpcast_la_CFLAGS += $(FDK_AAC_CFLAGS)
librtmpcast_la_LDFLAGS += $(FDK_AAC_LIBS)
endif

noinst_PROGRAMS = example

example_SOURCES = example.c
example_LDADD = $(lib_LTLIBRARIES)
example_DEPENDENCIES = $(lib_LTLIBRARIES)
