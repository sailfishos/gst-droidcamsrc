noinst_LTLIBRARIES = libtest.la
libtest_la_SOURCES = test.c
noinst_HEADERS = test.h
INCLUDES = $(GST_CFLAGS)

bin_PROGRAMS = simple
simple_SOURCES = simple.c
simple_LDADD = libtest.la $(GST_LIBS)
