AC_PREREQ([2.53])

AC_INIT([gst-droidcamsrc],[1.0.0])

GST_REQUIRED=1.2.3

AC_CONFIG_SRCDIR([gst/Makefile.am])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.10])

AM_MAINTAINER_MODE([enable])

AC_PROG_CC
AC_PROG_CXX

LT_PREREQ([2.2.6])
LT_INIT

AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

PKG_CHECK_MODULES(GST, [
  gstreamer-1.0 >= $GST_REQUIRED
  gstreamer-base-1.0 >= $GST_REQUIRED
  gstreamer-video-1.0 >= $GST_REQUIRED
  gstreamer-plugins-bad-1.0 >= $GST_REQUIRED
  gstreamer-pbutils-1.0 >= $GST_REQUIRED
  gstreamer-tag-1.0 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
      on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

PKG_CHECK_MODULES(DROID, [
  android-headers
], [
  AC_SUBST(DROID_CFLAGS)
  AC_SUBST(DROID_LIBS)
], [
  AC_MSG_ERROR([
      android-headers package is missing
  ])
])

PKG_CHECK_MODULES(EXIF, [
  libexif
], [
  AC_SUBST(EXIF_CFLAGS)
  AC_SUBST(EXIF_LIBS)
], [
  AC_MSG_ERROR([
      libexif package is missing
  ])
])

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-1.0/plugins"
else
  plugindir="\$(libdir)/gstreamer-1.0"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

AC_CHECK_LIB(hardware, hw_get_module, [], AC_MSG_ERROR([libhardware not found]))

AC_CONFIG_FILES([Makefile
		gst/Makefile
		gst/droidcamsrc/Makefile
		data/Makefile
		test/Makefile
		])
AC_OUTPUT

