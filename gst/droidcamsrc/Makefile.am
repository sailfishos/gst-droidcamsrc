plugin_LTLIBRARIES = libgstdroidcamsrc.la

libgstdroidcamsrc_la_SOURCES =  plugin.c \
				gstdroidcamsrc.c \
				gstcameramemory.c \
				cameraparams.cc \
				enums.c \
				gstvfsrcpad.c \
				gstimgsrcpad.c \
				gstvidsrcpad.c \
				gstphotoiface.c \
				exif.c \
				gstcamerasettings.c

libgstdroidcamsrc_la_CFLAGS = $(GST_CFLAGS) \
                              $(DROID_CFLAGS) \
                              $(EXIF_CFLAGS)

libgstdroidcamsrc_la_CXXFLAGS = $(GST_CFLAGS) \
                                $(DROID_CFLAGS)

libgstdroidcamsrc_la_LIBADD = $(GST_LIBS) \
                              $(EXIF_LIBS) \
                              -lhardware \
                              -lgstanativewindowbuffer-1.0 \
                              -lgstbasecamerabinsrc-1.0 \
                              -lgstphotography-1.0

libgstdroidcamsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdroidcamsrc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdroidcamsrc.h \
		 gstcameramemory.h \
		 cameraparams.h \
		 enums.h \
		 gstvfsrcpad.h \
		 gstimgsrcpad.h \
		 gstvidsrcpad.h \
		 gstphotoiface.h \
		 exif.h \
		 gstcamerasettings.h
