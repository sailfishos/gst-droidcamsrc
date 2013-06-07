plugin_LTLIBRARIES = libgstdroidcamsrc.la

libgstdroidcamsrc_la_SOURCES =  plugin.c \
				gstdroidcamsrc.c \
				gstcameramemory.c \
				gstcamerabufferpool.c \
				cameraparams.cc

libgstdroidcamsrc_la_CFLAGS = $(GST_CFLAGS) -I/usr/include/android/ -Iandroid/
libgstdroidcamsrc_la_CXXFLAGS = $(GST_CFLAGS) -I/usr/include/android/ -Iandroid/
libgstdroidcamsrc_la_LIBADD = $(GST_LIBS) -lhardware -lgstgralloc -lgstnativebuffer
libgstdroidcamsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdroidcamsrc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = android/camera.h \
		 android/camera_common.h \
		 gstdroidcamsrc.h \
		 gstcameramemory.h \
		 gstcamerabufferpool.h \
		 cameraparams.h
