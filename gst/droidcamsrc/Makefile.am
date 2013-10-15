plugin_LTLIBRARIES = libgstdroidcamsrc.la

libgstdroidcamsrc_la_SOURCES =  plugin.c \
				gstdroidcamsrc.c \
				gstcameramemory.c \
				gstcamerabufferpool.c \
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
                              -lgstgralloc \
                              -lgstnativebuffer \
                              -lgstbasecamerabinsrc-0.10

libgstdroidcamsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdroidcamsrc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdroidcamsrc.h \
		 gstcameramemory.h \
		 gstcamerabufferpool.h \
		 cameraparams.h \
		 enums.h \
		 gstvfsrcpad.h \
		 gstimgsrcpad.h \
		 gstvidsrcpad.h \
		 gstphotoiface.h \
		 table.h \
		 exif.h \
		 gstcamerasettings.h
