
plugin_LTLIBRARIES = libgstimgspot.la libgstkfilesrc.la libgstvideoscaledmixer.la libgstrtsp.la libgstaudio.la

# sources used to compile this plug-in
libgstimgspot_la_SOURCES = gstimgspot.c
libgstvideoscaledmixer_la_SOURCES = gstvideoscaledmixer.c
libgstkfilesrc_la_SOURCES = gstkfilesrc.c
libgstrtsp_la_SOURCES = gstrtpdec.c  gstrtsp.c  gstrtspext.c  gstrtspsrc.c
libgstaudio_la_SOURCES = audio.c gstringbuffer.c gstaudioclock.c mixerutils.c multichannel.c gstbaseaudiosink.c gstbaseaudiosrc.c gstaudiofilter.c gstaudiosink.c gstaudiosrc.c audio-enumtypes.c

libgstimgspot_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(OPENCV_CFLAGS) $(ORC_CFLAGS) -I.
libgstimgspot_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) $(ORC_LIBS)
libgstimgspot_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstkfilesrc_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(OPENCV_CFLAGS) $(ORC_CFLAGS) -I.
libgstkfilesrc_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) $(ORC_LIBS)
libgstkfilesrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideoscaledmixer_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(OPENCV_CFLAGS) $(ORC_CFLAGS) -I.
libgstvideoscaledmixer_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) $(ORC_LIBS)
libgstvideoscaledmixer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstrtsp_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(OPENCV_CFLAGS) $(ORC_CFLAGS) -I.
libgstrtsp_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) $(ORC_LIBS)
libgstrtsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstaudio_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(OPENCV_CFLAGS) $(ORC_CFLAGS) -I.
libgstaudio_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) $(ORC_LIBS)
libgstaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
