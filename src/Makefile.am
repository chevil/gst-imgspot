
plugin_LTLIBRARIES = libgstimgspot.la

# sources used to compile this plug-in
libgstimgspot_la_SOURCES = gstimgspot.c

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
libgstimgspot_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(OPENCV_CFLAGS) -I.

libgstimgspot_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS)

libgstimgspot_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
