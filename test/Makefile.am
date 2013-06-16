noinst_LTLIBRARIES = libtest.la
libtest_la_SOURCES = test.c
noinst_HEADERS = test.h
INCLUDES = $(GST_CFLAGS)

bin_PROGRAMS = simple capture video

simple_SOURCES = simple.c
simple_LDADD = libtest.la $(GST_LIBS)

capture_SOURCES = capture.c
capture_LDADD = libtest.la $(GST_LIBS)

video_SOURCES = video.c
video_LDADD = libtest.la $(GST_LIBS)
