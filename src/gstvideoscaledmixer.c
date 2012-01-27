/* Modified video mixer to resize every pad
 * before output
 * Copyright (C) 2004, 2008 Yves Degoyon <ydegoyon@gmail.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-videoscaledmixer
 *
 * Videoscaledmixer can accept AYUV, ARGB and BGRA video streams. For each of the requested
 * sink pads it will compare the incoming geometry and framerate to define the
 * output parameters. Indeed output video frames will have the geometry of the
 * biggest incoming video stream and the framerate of the fastest incoming one.
 *
 * All sink pads must be either AYUV, ARGB or BGRA, but a mixture of them is not 
 * supported. The src pad will have the same colorspace as the sinks. 
 * No colorspace conversion is done. 
 * 
 * Individual parameters for each input stream can be configured on the
 * #GstVideoScaledMixerPad.
 *
 * At this stage, videoscaledmixer is considered UNSTABLE. The API provided in the
 * properties may yet change in the near future. When videoscaledmixer is stable,
 * it will replace #videomixer
 *
 * <refsect2>
 * <title>Sample pipelines</title>
 * |[
 * gst-launch-0.10 \
 *   videotestsrc pattern=1 ! \
 *   video/x-raw-yuv,format=\(fourcc\)AYUV,framerate=\(fraction\)10/1,width=100,height=100 ! \
 *   videobox border-alpha=0 top=-70 bottom=-70 right=-220 ! \
 *   videoscaledmixer name=mix sink_0::alpha=0.7 sink_1::alpha=0.5 ! \
 *   ffmpegcolorspace ! xvimagesink \
 *   videotestsrc ! \
 *   video/x-raw-yuv,format=\(fourcc\)AYUV,framerate=\(fraction\)5/1,width=320,height=240 ! mix.
 * ]| A pipeline to demonstrate videomixer used together with videobox.
 * This should show a 320x240 pixels video test source with some transparency
 * showing the background checker pattern. Another video test source with just
 * the snow pattern of 100x100 pixels is overlayed on top of the first one on
 * the left vertically centered with a small transparency showing the first
 * video test source behind and the checker pattern under it. Note that the
 * framerate of the output video is 10 frames per second.
 * |[
 * gst-launch videotestsrc pattern=1 ! \
 *   video/x-raw-rgb, framerate=\(fraction\)10/1, width=100, height=100 ! \
 *   videoscaledmixer name=mix ! ffmpegcolorspace ! ximagesink \
 *   videotestsrc !  \
 *   video/x-raw-rgb, framerate=\(fraction\)5/1, width=320, height=240 ! mix.
 * ]| A pipeline to demostrate bgra mixing. (This does not demonstrate alpha blending). 
 * |[
 * gst-launch videotestsrc pattern=1 ! \
 *   video/x-raw-yuv,format =\(fourcc\)I420, framerate=\(fraction\)10/1, width=100, height=100 ! \
 *   videoscaledmixer name=mix ! ffmpegcolorspace ! ximagesink \
 *   videotestsrc ! \
 *   video/x-raw-yuv,format=\(fourcc\)I420, framerate=\(fraction\)5/1, width=320, height=240 ! mix.
 * ]| A pipeline to test I420
 * |[
 * gst-launch videoscaledmixer name=mixer sink_1::alpha=0.5 sink_1::xpos=50 sink_1::ypos=50 ! \
 *   ffmpegcolorspace ! ximagesink \
 *   videotestsrc pattern=snow timestamp-offset=3000000000 ! \
 *   "video/x-raw-yuv,format=(fourcc)AYUV,width=640,height=480,framerate=(fraction)30/1" ! \
 *   timeoverlay ! queue2 ! mixer. \
 *   videotestsrc pattern=smpte ! \
 *   "video/x-raw-yuv,format=(fourcc)AYUV,width=800,height=600,framerate=(fraction)10/1" ! \
 *   timeoverlay ! queue2 ! mixer.
 * ]| A pipeline to demonstrate synchronized mixing (the second stream starts after 3 seconds)
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstvideoscaledmixer.h"
#include "videoscaledmixerpad.h"

#include <gst/controller/gstcontroller.h>

#define orc_memset memset

GST_DEBUG_CATEGORY_STATIC (gst_videoscaledmixer_debug);
#define GST_CAT_DEFAULT gst_videoscaledmixer_debug

#define GST_VIDEOSCALED_MIXER_GET_LOCK(mix) \
  (GST_VIDEOSCALED_MIXER(mix)->lock)
#define GST_VIDEOSCALED_MIXER_LOCK(mix) \
  (g_mutex_lock(GST_VIDEOSCALED_MIXER_GET_LOCK (mix)))
#define GST_VIDEOSCALED_MIXER_UNLOCK(mix) \
  (g_mutex_unlock(GST_VIDEOSCALED_MIXER_GET_LOCK (mix)))

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_YUV ("Y444") ";" GST_VIDEO_CAPS_YUV ("Y42B") ";"
        GST_VIDEO_CAPS_YUV ("YUY2") ";" GST_VIDEO_CAPS_YUV ("UYVY") ";"
        GST_VIDEO_CAPS_YUV ("YVYU") ";"
        GST_VIDEO_CAPS_YUV ("I420") ";" GST_VIDEO_CAPS_YUV ("YV12") ";"
        GST_VIDEO_CAPS_YUV ("Y41B") ";" GST_VIDEO_CAPS_RGB ";"
        GST_VIDEO_CAPS_BGR ";" GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx)
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_YUV ("Y444") ";" GST_VIDEO_CAPS_YUV ("Y42B") ";"
        GST_VIDEO_CAPS_YUV ("YUY2") ";" GST_VIDEO_CAPS_YUV ("UYVY") ";"
        GST_VIDEO_CAPS_YUV ("YVYU") ";"
        GST_VIDEO_CAPS_YUV ("I420") ";" GST_VIDEO_CAPS_YUV ("YV12") ";"
        GST_VIDEO_CAPS_YUV ("Y41B") ";" GST_VIDEO_CAPS_RGB ";"
        GST_VIDEO_CAPS_BGR ";" GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx)
    );

static void gst_videoscaledmixer_child_proxy_init (gpointer g_iface,
    gpointer iface_data);
static gboolean gst_videoscaledmixer_push_sink_event (GstVideoScaledMixer * mix,
    GstEvent * event);
static void gst_videoscaledmixer_release_pad (GstElement * element, GstPad * pad);
static void gst_videoscaledmixer_reset_qos (GstVideoScaledMixer * mix);

static void
_do_init (GType object_type)
{
  static const GInterfaceInfo child_proxy_info = {
    (GInterfaceInitFunc) gst_videoscaledmixer_child_proxy_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (object_type, GST_TYPE_CHILD_PROXY,
      &child_proxy_info);
}

struct _GstVideoScaledMixerCollect
{
  GstCollectData2 collect;      /* we extend the CollectData */

  GstVideoScaledMixerPad *mixpad;

  GstBuffer *queued;            /* buffer for which we don't know the end time yet */

  GstBuffer *buffer;            /* buffer that should be blended now */
  GstClockTime start_time;
  GstClockTime end_time;
};

#define DEFAULT_PAD_ZORDER 0
#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_ALPHA  1.0
#define DEFAULT_PAD_WIDTH  320
#define DEFAULT_PAD_HEIGHT 240
#define DEFAULT_PAD_ENAME "noone"
enum
{
  PROP_PAD_0,
  PROP_PAD_ZORDER,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_ALPHA,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_ENAME
};

G_DEFINE_TYPE (GstVideoScaledMixerPad, gst_videoscaledmixer_pad, GST_TYPE_PAD);

static void
gst_videoscaledmixer_collect_free (GstCollectData2 * data)
{
  GstVideoScaledMixerCollect *cdata = (GstVideoScaledMixerCollect *) data;

  gst_buffer_replace (&cdata->buffer, NULL);
}

static gboolean
gst_videoscaledmixer_update_src_caps (GstVideoScaledMixer * mix)
{
  GSList *l;
  gint best_width = -1, best_height = -1;
  gdouble best_fps = -1, cur_fps;
  gint best_fps_n = -1, best_fps_d = -1;
  gboolean ret = TRUE;

  GST_VIDEOSCALED_MIXER_LOCK (mix);

  for (l = mix->sinkpads; l; l = l->next) {
    GstVideoScaledMixerPad *mpad = l->data;
    gint this_width, this_height;

    if (mpad->fps_n == 0 || mpad->fps_d == 0 ||
        mpad->width == 0 || mpad->height == 0)
      continue;

    this_width = mpad->width + MAX (mpad->xpos, 0);
    this_height = mpad->height + MAX (mpad->ypos, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;

    if (mpad->fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (mpad->fps_n, mpad->fps_d, &cur_fps);
    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = mpad->fps_n;
      best_fps_d = mpad->fps_d;
    }
  }

  if (best_fps_n <= 0 && best_fps_d <= 0) {
    best_fps_n = 25;
    best_fps_d = 1;
    best_fps = 25.0;
  }

  if (best_width > 0 && best_height > 0 && best_fps > 0) {
    GstCaps *caps, *peercaps;
    GstStructure *s;

    if (mix->fps_n != best_fps_n || mix->fps_d != best_fps_d) {
      if (mix->segment.last_stop != -1) {
        mix->ts_offset = mix->segment.last_stop - mix->segment.start;
        mix->nframes = 0;
      }
    }

    caps = gst_video_format_new_caps (mix->format,
        best_width, best_height, best_fps_n, best_fps_d,
        mix->par_n, mix->par_d);

    peercaps = gst_pad_peer_get_caps (mix->srcpad);
    if (peercaps) {
      GstCaps *tmp;

      s = gst_caps_get_structure (caps, 0);
      gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height",
          GST_TYPE_INT_RANGE, 1, G_MAXINT, "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1, NULL);

      tmp = gst_caps_intersect (caps, peercaps);
      gst_caps_unref (caps);
      gst_caps_unref (peercaps);
      caps = tmp;
      if (gst_caps_is_empty (caps)) {
        ret = FALSE;
        GST_VIDEOSCALED_MIXER_UNLOCK (mix);
        goto done;
      }

      gst_caps_truncate (caps);
      s = gst_caps_get_structure (caps, 0);
      gst_structure_fixate_field_nearest_int (s, "width", best_width);
      gst_structure_fixate_field_nearest_int (s, "height", best_height);
      gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
          best_fps_d);

      gst_structure_get_int (s, "width", &best_width);
      gst_structure_get_int (s, "height", &best_height);
      gst_structure_get_fraction (s, "fraction", &best_fps_n, &best_fps_d);
    }

    mix->fps_n = best_fps_n;
    mix->fps_d = best_fps_d;
    mix->width = best_width;
    mix->height = best_height;

    GST_VIDEOSCALED_MIXER_UNLOCK (mix);
    ret = gst_pad_set_caps (mix->srcpad, caps);
    gst_caps_unref (caps);
  } else {
    GST_VIDEOSCALED_MIXER_UNLOCK (mix);
  }

done:
  return ret;
}


static gboolean
gst_videoscaledmixer_pad_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVideoScaledMixer *mix;
  GstVideoScaledMixerPad *mixpad;
  GstVideoFormat fmt;
  gint width, height;
  gint fps_n = 0, fps_d = 0;
  gint par_n = 1, par_d = 1;
  gboolean ret = FALSE;
  GstStructure *s;

  GST_INFO_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  mix = GST_VIDEOSCALED_MIXER (gst_pad_get_parent (pad));
  mixpad = GST_VIDEOSCALED_MIXER_PAD (pad);

  if (!gst_video_format_parse_caps (caps, &fmt, &width, &height) ||
      !gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d)) {
    GST_ERROR_OBJECT (pad, "Failed to parse caps");
    goto beach;
  }

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_field (s, "framerate")
      && !gst_video_parse_caps_framerate (caps, &fps_n, &fps_d)) {
    GST_ERROR_OBJECT (pad, "Failed to parse caps");
    goto beach;
  }

  GST_VIDEOSCALED_MIXER_LOCK (mix);
  if (mix->format != GST_VIDEO_FORMAT_UNKNOWN) {
    if (mix->format != fmt || mix->par_n != par_n || mix->par_d != par_d) {
      GST_ERROR_OBJECT (pad, "Caps not compatible with other pads' caps");
      GST_VIDEOSCALED_MIXER_UNLOCK (mix);
      goto beach;
    }
  }

  mix->format = fmt;
  mix->par_n = par_n;
  mix->par_d = par_d;
  mixpad->fps_n = fps_n;
  mixpad->fps_d = fps_d;
  mixpad->width = width;
  mixpad->height = height;

  GST_VIDEOSCALED_MIXER_UNLOCK (mix);

  ret = gst_videoscaledmixer_update_src_caps (mix);

beach:
  gst_object_unref (mix);

  return ret;
}

static GstCaps *
gst_videoscaledmixer_pad_sink_getcaps (GstPad * pad)
{
  GstVideoScaledMixer *mix;
  GstCaps *srccaps;
  GstStructure *s;
  gint i, n;

  mix = GST_VIDEOSCALED_MIXER (gst_pad_get_parent (pad));

  srccaps = gst_pad_get_fixed_caps_func (GST_PAD (mix->srcpad));
  srccaps = gst_caps_make_writable (srccaps);

  n = gst_caps_get_size (srccaps);
  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (srccaps, i);
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    if (!gst_structure_has_field (s, "pixel-aspect-ratio"))
      gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
  }

  GST_DEBUG_OBJECT (pad, "Returning %" GST_PTR_FORMAT, srccaps);

  return srccaps;
}

static gboolean
gst_videoscaledmixer_pad_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret;
  GstVideoScaledMixer *mix;
  GstCaps *accepted_caps;
  gint i, n;
  GstStructure *s;

  mix = GST_VIDEOSCALED_MIXER (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (pad, "%" GST_PTR_FORMAT, caps);

  accepted_caps = gst_pad_get_fixed_caps_func (GST_PAD (mix->srcpad));
  accepted_caps = gst_caps_make_writable (accepted_caps);
  GST_LOG_OBJECT (pad, "src caps %" GST_PTR_FORMAT, accepted_caps);

  n = gst_caps_get_size (accepted_caps);
  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (accepted_caps, i);
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    if (!gst_structure_has_field (s, "pixel-aspect-ratio"))
      gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
  }

  ret = gst_caps_can_intersect (caps, accepted_caps);
  GST_INFO_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT, (ret ? "" : "not "),
      caps);
  GST_INFO_OBJECT (pad, "acceptable caps are %" GST_PTR_FORMAT, accepted_caps);

  gst_caps_unref (accepted_caps);

  gst_object_unref (mix);
  return ret;
}

static void
gst_videoscaledmixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoScaledMixerPad *pad = GST_VIDEOSCALED_MIXER_PAD (object);

  switch (prop_id) {
    case PROP_PAD_ZORDER:
      g_value_set_uint (value, pad->zorder);
      break;
    case PROP_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, pad->owidth);
      break;
    case PROP_PAD_ENAME:
      g_value_set_string (value, pad->ename);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, pad->oheight);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static int
pad_zorder_compare (const GstVideoScaledMixerPad * pad1,
    const GstVideoScaledMixerPad * pad2)
{
  return pad1->zorder - pad2->zorder;
}

static void
gst_videoscaledmixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoScaledMixerPad *pad = GST_VIDEOSCALED_MIXER_PAD (object);
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (gst_pad_get_parent (GST_PAD (pad)));

  switch (prop_id) {
    case PROP_PAD_ZORDER:
      pad->zorder = g_value_get_uint (value);
      break;
    case PROP_PAD_XPOS:
      pad->xpos = g_value_get_int (value);
      break;
    case PROP_PAD_YPOS:
      pad->ypos = g_value_get_int (value);
      break;
    case PROP_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    case PROP_PAD_WIDTH:
      pad->owidth = g_value_get_int (value);
      break;
    case PROP_PAD_ENAME:
      pad->ename = (char *) malloc( strlen ( (char *) g_value_get_string (value) ) + 1 );
      strcpy( pad->ename, (char *) g_value_get_string (value) );
      break;
    case PROP_PAD_HEIGHT:
      pad->oheight = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_object_unref (mix);
}

static void
gst_videoscaledmixer_pad_class_init (GstVideoScaledMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_videoscaledmixer_pad_set_property;
  gobject_class->get_property = gst_videoscaledmixer_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_ZORDER,
      g_param_spec_uint ("zorder", "Z-Order", "Z Order of the picture",
          0, 10000, DEFAULT_PAD_ZORDER,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_WIDTH,
      g_param_spec_int ("width", "Width", "Output width",
          G_MININT, G_MAXINT, DEFAULT_PAD_WIDTH,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_HEIGHT,
      g_param_spec_int ("height", "Height", "Output height",
          G_MININT, G_MAXINT, DEFAULT_PAD_HEIGHT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ENAME,
      g_param_spec_string ("ename", "EName", "external name of the pad",
          DEFAULT_PAD_ENAME,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_videoscaledmixer_pad_init (GstVideoScaledMixerPad * mixerpad)
{
  /* setup some pad functions */
  gst_pad_set_setcaps_function (GST_PAD (mixerpad),
      gst_videoscaledmixer_pad_sink_setcaps);
  gst_pad_set_acceptcaps_function (GST_PAD (mixerpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_pad_sink_acceptcaps));
  gst_pad_set_getcaps_function (GST_PAD (mixerpad),
      gst_videoscaledmixer_pad_sink_getcaps);

  mixerpad->zorder = DEFAULT_PAD_ZORDER;
  mixerpad->xpos = DEFAULT_PAD_XPOS;
  mixerpad->ypos = DEFAULT_PAD_YPOS;
  mixerpad->alpha = DEFAULT_PAD_ALPHA;
  mixerpad->owidth = DEFAULT_PAD_WIDTH;
  mixerpad->oheight = DEFAULT_PAD_HEIGHT;
  mixerpad->ename = (char*)malloc(strlen(DEFAULT_PAD_ENAME)+1);
  strcpy( mixerpad->ename, DEFAULT_PAD_ENAME );
}

/* GstVideoScaledMixer */
#define DEFAULT_BACKGROUND VIDEOSCALED_MIXER_BACKGROUND_CHECKER
enum
{
  PROP_0,
  PROP_BACKGROUND
};

GST_BOILERPLATE_FULL (GstVideoScaledMixer, gst_videoscaledmixer, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_videoscaledmixer_update_qos (GstVideoScaledMixer * mix, gdouble proportion,
    GstClockTimeDiff diff, GstClockTime timestamp)
{
  GST_DEBUG_OBJECT (mix,
      "Updating QoS: proportion %lf, diff %s%" GST_TIME_FORMAT ", timestamp %"
      GST_TIME_FORMAT, proportion, (diff < 0) ? "-" : "",
      GST_TIME_ARGS (ABS (diff)), GST_TIME_ARGS (timestamp));

  GST_OBJECT_LOCK (mix);
  mix->proportion = proportion;
  if (G_LIKELY (timestamp != GST_CLOCK_TIME_NONE)) {
    if (G_UNLIKELY (diff > 0))
      mix->earliest_time =
          timestamp + 2 * diff + gst_util_uint64_scale_int (GST_SECOND,
          mix->fps_d, mix->fps_n);
    else
      mix->earliest_time = timestamp + diff;
  } else {
    mix->earliest_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (mix);
}

static void
gst_videoscaledmixer_reset_qos (GstVideoScaledMixer * mix)
{
  gst_videoscaledmixer_update_qos (mix, 0.5, 0, GST_CLOCK_TIME_NONE);
  mix->qos_processed = mix->qos_dropped = 0;
}

static void
gst_videoscaledmixer_read_qos (GstVideoScaledMixer * mix, gdouble * proportion,
    GstClockTime * time)
{
  GST_OBJECT_LOCK (mix);
  *proportion = mix->proportion;
  *time = mix->earliest_time;
  GST_OBJECT_UNLOCK (mix);
}

static void
gst_videoscaledmixer_reset (GstVideoScaledMixer * mix)
{
  GSList *l;

  mix->format = GST_VIDEO_FORMAT_UNKNOWN;
  mix->width = mix->height = 0;
  mix->fps_n = mix->fps_d = 0;
  mix->par_n = mix->par_d = 0;
  mix->ts_offset = 0;
  mix->nframes = 0;

  gst_segment_init (&mix->segment, GST_FORMAT_TIME);
  mix->segment.last_stop = -1;

  gst_videoscaledmixer_reset_qos (mix);

  for (l = mix->sinkpads; l; l = l->next) {
    GstVideoScaledMixerPad *p = l->data;
    GstVideoScaledMixerCollect *mixcol = p->mixcol;

    gst_buffer_replace (&mixcol->buffer, NULL);
    mixcol->start_time = -1;
    mixcol->end_time = -1;

    p->fps_n = p->fps_d = 0;
    p->width = p->height = 0;
  }

  mix->newseg_pending = TRUE;
  mix->flush_stop_pending = FALSE;
}

/*  1 == OK
 *  0 == need more data
 * -1 == EOS
 * -2 == error
 */
static gint
gst_videoscaledmixer_fill_queues (GstVideoScaledMixer * mix,
    GstClockTime output_start_time, GstClockTime output_end_time)
{
  GSList *l;
  gboolean eos = TRUE;
  gboolean need_more_data = FALSE;

  for (l = mix->sinkpads; l; l = l->next) {
    GstVideoScaledMixerPad *pad = l->data;
    GstVideoScaledMixerCollect *mixcol = pad->mixcol;
    GstSegment *segment = &pad->mixcol->collect.segment;
    GstBuffer *buf;

    buf = gst_collect_pads2_peek (mix->collect, &mixcol->collect);
    if (buf) {
      GstClockTime start_time, end_time;

      start_time = GST_BUFFER_TIMESTAMP (buf);
      if (start_time == -1) {
        gst_buffer_unref (buf);
        GST_ERROR_OBJECT (pad, "Need timestamped buffers!");
        return -2;
      }

      /* FIXME: Make all this work with negative rates */

      if ((mixcol->buffer && start_time < GST_BUFFER_TIMESTAMP (mixcol->buffer))
          || (mixcol->queued
              && start_time < GST_BUFFER_TIMESTAMP (mixcol->queued))) {
        GST_WARNING_OBJECT (pad, "Buffer from the past, dropping");
        gst_buffer_unref (buf);
        buf = gst_collect_pads2_pop (mix->collect, &mixcol->collect);
        gst_buffer_unref (buf);
        need_more_data = TRUE;
        continue;
      }

      if (mixcol->queued) {
        end_time = start_time - GST_BUFFER_TIMESTAMP (mixcol->queued);
        start_time = GST_BUFFER_TIMESTAMP (mixcol->queued);
        gst_buffer_unref (buf);
        buf = gst_buffer_ref (mixcol->queued);
      } else {
        end_time = GST_BUFFER_DURATION (buf);

        if (end_time == -1) {
          mixcol->queued = buf;
          need_more_data = TRUE;
          continue;
        }
      }

      g_assert (start_time != -1 && end_time != -1);
      end_time += start_time;   /* convert from duration to position */

      if (mixcol->end_time != -1 && mixcol->end_time > end_time) {
        GST_WARNING_OBJECT (pad, "Buffer from the past, dropping");
        if (buf == mixcol->queued) {
          gst_buffer_unref (buf);
          gst_buffer_replace (&mixcol->queued, NULL);
        } else {
          gst_buffer_unref (buf);
          buf = gst_collect_pads2_pop (mix->collect, &mixcol->collect);
          gst_buffer_unref (buf);
        }

        need_more_data = TRUE;
        continue;
      }

      /* Check if it's inside the segment */
      if (start_time >= segment->stop || end_time < segment->start) {
        GST_DEBUG_OBJECT (pad, "Buffer outside the segment");

        if (buf == mixcol->queued) {
          gst_buffer_unref (buf);
          gst_buffer_replace (&mixcol->queued, NULL);
        } else {
          gst_buffer_unref (buf);
          buf = gst_collect_pads2_pop (mix->collect, &mixcol->collect);
          gst_buffer_unref (buf);
        }

        need_more_data = TRUE;
        continue;
      }

      /* Clip to segment and convert to running time */
      start_time = MAX (start_time, segment->start);
      if (segment->stop != -1)
        end_time = MIN (end_time, segment->stop);
      start_time =
          gst_segment_to_running_time (segment, GST_FORMAT_TIME, start_time);
      end_time =
          gst_segment_to_running_time (segment, GST_FORMAT_TIME, end_time);
      g_assert (start_time != -1 && end_time != -1);

      /* Convert to the output segment rate */
      if (mix->segment.abs_rate != 1.0) {
        start_time *= mix->segment.abs_rate;
        end_time *= mix->segment.abs_rate;
      }

      if (end_time >= output_start_time && start_time < output_end_time) {
        GST_DEBUG_OBJECT (pad,
            "Taking new buffer with start time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start_time));
        gst_buffer_replace (&mixcol->buffer, buf);
        mixcol->start_time = start_time;
        mixcol->end_time = end_time;

        if (buf == mixcol->queued) {
          gst_buffer_unref (buf);
          gst_buffer_replace (mixcol->queued, NULL);
        } else {
          gst_buffer_unref (buf);
          buf = gst_collect_pads2_pop (mix->collect, &mixcol->collect);
          gst_buffer_unref (buf);
        }
        eos = FALSE;
      } else if (start_time >= output_end_time) {
        GST_DEBUG_OBJECT (pad, "Keeping buffer until %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start_time));
        gst_buffer_unref (buf);
        eos = FALSE;
      } else {
        GST_DEBUG_OBJECT (pad, "Too old buffer -- dropping");
        if (buf == mixcol->queued) {
          gst_buffer_unref (buf);
          gst_buffer_replace (&mixcol->queued, NULL);
        } else {
          gst_buffer_unref (buf);
          buf = gst_collect_pads2_pop (mix->collect, &mixcol->collect);
          gst_buffer_unref (buf);
        }

        need_more_data = TRUE;
        continue;
      }
    } else {
      if (mixcol->end_time != -1) {
        if (mixcol->end_time < output_start_time) {
          gst_buffer_replace (&mixcol->buffer, NULL);
          mixcol->start_time = mixcol->end_time = -1;
          if (!GST_COLLECT_PADS2_STATE_IS_SET (mixcol,
                  GST_COLLECT_PADS2_STATE_EOS))
            need_more_data = TRUE;
        } else {
          eos = FALSE;
        }
      }
    }
  }

  if (need_more_data)
  {
    return 0;
  }
  if (eos)
  {
    printf( "video scaled mixer: got eos");
    return -1;
  }

  return 1;
}

static GstFlowReturn
gst_videoscaledmixer_blend_buffers (GstVideoScaledMixer * mix,
    GstClockTime output_start_time, GstClockTime output_end_time,
    GstBuffer ** outbuf)
{
  GSList *l;
  GstFlowReturn ret;
  guint outsize;

  outsize = gst_video_format_get_size (mix->format, mix->width, mix->height);
  ret = gst_pad_alloc_buffer_and_set_caps (mix->srcpad, GST_BUFFER_OFFSET_NONE,
      outsize, GST_PAD_CAPS (mix->srcpad), outbuf);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_BUFFER_TIMESTAMP (*outbuf) = output_start_time;
  GST_BUFFER_DURATION (*outbuf) = output_end_time - output_start_time;

  // setting time stamps
  for (l = mix->sinkpads; l; l = l->next) {
    GstVideoScaledMixerPad *pad = l->data;
    GstVideoScaledMixerCollect *mixcol = pad->mixcol;

    if (mixcol->buffer != NULL) {
      GstClockTime timestamp;
      gint64 stream_time;
      GstSegment *seg;

      seg = &mixcol->collect.segment;

      timestamp = GST_BUFFER_TIMESTAMP (mixcol->buffer);

      stream_time =
          gst_segment_to_stream_time (seg, GST_FORMAT_TIME, timestamp);

      /* sync object properties on stream time */
      if (GST_CLOCK_TIME_IS_VALID (stream_time))
        gst_object_sync_values (G_OBJECT (pad), stream_time);
    }
  }

  if ( mix->format == GST_VIDEO_FORMAT_I420 )
  {
    // now copying data in an optimized way
    guint8 *pYData, *pUData, *pVData;;
    guint8 *pYOutputData = GST_BUFFER_DATA (*outbuf);
    guint8 *pUOutputData = pYOutputData+(mix->width*mix->height);
    guint8 *pVOutputData = pYOutputData+(mix->width*mix->height)+(mix->width*mix->height>>2);
    GstVideoScaledMixerPad *cpad;
    int px, py;
    int rx, ry;
    int pzorder;
 
    // for all output pixel
    for ( py=0; py<mix->height; py++ ) 
    {
      for ( px=0; px<mix->width; px++ ) 
      { 
        pYData=NULL;
        pzorder=0;

        for (l = mix->sinkpads; l; l = l->next) {
           GstVideoScaledMixerPad *pad = l->data;
           GstVideoScaledMixerCollect *mixcol = pad->mixcol;

           if (mixcol->buffer != NULL && (pad->alpha>0.0) ) 
           {
              if ( px>=pad->xpos && px<(pad->xpos+pad->owidth)
                   && py>=pad->ypos && py<(pad->ypos+pad->oheight)
                   && (pad->zorder)>=pzorder
                 )
              {
                 pzorder = pad->zorder;
                 pYData = GST_BUFFER_DATA( mixcol->buffer );
                 pUData = pYData+(pad->width*pad->height);
                 pVData = pYData+(pad->width*pad->height)+(pad->width*pad->height>>2);
                 cpad=pad;
              }
           }
        }

        if ( pYData != NULL )
        { 
           if ( cpad->width != cpad->owidth || cpad->height != cpad->oheight )
           {
             rx = (int)((double)(px-cpad->xpos)*(double)cpad->width/(double)cpad->owidth);
             ry = (int)((double)(py-cpad->ypos)*(double)cpad->height/(double)cpad->oheight);
           }
           else
           {
             rx = px-cpad->xpos;
             ry = py-cpad->ypos;
           }
           *(pYOutputData+py*mix->width+px) = *(pYData+ry*cpad->width+rx)*(cpad->alpha);
           *(pUOutputData+(py>>1)*(mix->width>>1)+(px>>1)) = *(pUData+(ry>>1)*(cpad->width>>1)+(rx>>1));
           *(pVOutputData+(py>>1)*(mix->width>>1)+(px>>1)) = *(pVData+(ry>>1)*(cpad->width>>1)+(rx>>1));
        }
      }
    }
  }
  else
  {
    GST_ERROR_OBJECT (mix, "Your output will be green, sorry, only YUV/I420 format is supported in this experimental mixer");
  }

  return GST_FLOW_OK;
}

/* Perform qos calculations before processing the next frame. Returns TRUE if
 * the frame should be processed, FALSE if the frame can be dropped entirely */
static gint64
gst_videoscaledmixer_do_qos (GstVideoScaledMixer * mix, GstClockTime timestamp)
{
  GstClockTime qostime, earliest_time;
  gdouble proportion;
  gint64 jitter;

  /* no timestamp, can't do QoS => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    GST_LOG_OBJECT (mix, "invalid timestamp, can't do QoS, process frame");
    return -1;
  }

  /* get latest QoS observation values */
  gst_videoscaledmixer_read_qos (mix, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    GST_LOG_OBJECT (mix, "no observation yet, process frame");
    return -1;
  }

  /* qos is done on running time */
  qostime =
      gst_segment_to_running_time (&mix->segment, GST_FORMAT_TIME, timestamp);

  /* see how our next timestamp relates to the latest qos timestamp */
  GST_LOG_OBJECT (mix, "qostime %" GST_TIME_FORMAT ", earliest %"
      GST_TIME_FORMAT, GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));

  jitter = GST_CLOCK_DIFF (qostime, earliest_time);
  if (qostime != GST_CLOCK_TIME_NONE && jitter > 0) {
    GST_DEBUG_OBJECT (mix, "we are late, drop frame");
    return jitter;
  }

  GST_LOG_OBJECT (mix, "process frame");
  return jitter;
}

static GstFlowReturn
gst_videoscaledmixer_collected (GstCollectPads2 * pads, GstVideoScaledMixer * mix)
{
  GstFlowReturn ret;
  GstClockTime output_start_time, output_end_time;
  GstBuffer *outbuf = NULL;
  gint res;
  gint64 jitter;

  /* If we're not negotiated yet... */
  if (mix->format == GST_VIDEO_FORMAT_UNKNOWN)
    return GST_FLOW_NOT_NEGOTIATED;

  if (g_atomic_int_compare_and_exchange (&mix->flush_stop_pending, TRUE, FALSE)) {
    GST_DEBUG_OBJECT (mix, "pending flush stop");
    gst_pad_push_event (mix->srcpad, gst_event_new_flush_stop ());
  }

  GST_VIDEOSCALED_MIXER_LOCK (mix);

  if (mix->newseg_pending) {
    GST_DEBUG_OBJECT (mix, "Sending NEWSEGMENT event");
    if (!gst_pad_push_event (mix->srcpad, gst_event_new_new_segment_full (FALSE,
                mix->segment.rate, mix->segment.applied_rate,
                mix->segment.format, mix->segment.start, mix->segment.stop,
                mix->segment.time))) {
      ret = GST_FLOW_ERROR;
      goto done;
    }
    mix->newseg_pending = FALSE;
  }

  if (mix->segment.last_stop == -1)
    output_start_time = mix->segment.start;
  else
    output_start_time = mix->segment.last_stop;

  if (output_start_time >= mix->segment.stop) {
    GST_DEBUG_OBJECT (mix, "Segment done");
    gst_pad_push_event (mix->srcpad, gst_event_new_eos ());
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }

  output_end_time =
      mix->ts_offset + gst_util_uint64_scale (mix->nframes + 1,
      GST_SECOND * mix->fps_d, mix->fps_n);
  if (mix->segment.stop != -1)
    output_end_time = MIN (output_end_time, mix->segment.stop);

  res = gst_videoscaledmixer_fill_queues (mix, output_start_time, output_end_time);

  if (res == 0) {
    GST_DEBUG_OBJECT (mix, "Need more data for decisions");
    ret = GST_FLOW_OK;
    goto done;
  } else if (res == -1) {
    GST_DEBUG_OBJECT (mix, "All sinkpads are EOS -- forwarding");
    gst_pad_push_event (mix->srcpad, gst_event_new_eos ());
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  } else if (res == -2) {
    GST_ERROR_OBJECT (mix, "Error collecting buffers");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  jitter = gst_videoscaledmixer_do_qos (mix, output_start_time);
  if (jitter <= 0) {
    ret =
        gst_videoscaledmixer_blend_buffers (mix, output_start_time,
        output_end_time, &outbuf);
    mix->qos_processed++;
  } else {
    GstMessage *msg;

    mix->qos_dropped++;

    /* TODO: live */
    msg =
        gst_message_new_qos (GST_OBJECT_CAST (mix), FALSE,
        gst_segment_to_running_time (&mix->segment, GST_FORMAT_TIME,
            output_start_time), gst_segment_to_stream_time (&mix->segment,
            GST_FORMAT_TIME, output_start_time), output_start_time,
        output_end_time - output_start_time);
    gst_message_set_qos_values (msg, jitter, mix->proportion, 1000000);
    gst_message_set_qos_stats (msg, GST_FORMAT_BUFFERS, mix->qos_processed,
        mix->qos_dropped);
    gst_element_post_message (GST_ELEMENT_CAST (mix), msg);

    ret = GST_FLOW_OK;
  }

  gst_segment_set_last_stop (&mix->segment, GST_FORMAT_TIME, output_end_time);
  mix->nframes++;

  GST_VIDEOSCALED_MIXER_UNLOCK (mix);
  if (outbuf) {
    GST_LOG_OBJECT (mix,
        "Pushing buffer with ts %" GST_TIME_FORMAT " and duration %"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));
    ret = gst_pad_push (mix->srcpad, outbuf);
  }
  GST_VIDEOSCALED_MIXER_LOCK (mix);

done:
  GST_VIDEOSCALED_MIXER_UNLOCK (mix);

  return ret;
}

static GstCaps *
gst_videoscaledmixer_src_getcaps (GstPad * pad)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (gst_pad_get_parent (pad));
  GstCaps *caps;
  GstStructure *s;
  gint n;

  if (mix->format != GST_VIDEO_FORMAT_UNKNOWN) {
    caps = gst_caps_copy (GST_PAD_CAPS (mix->srcpad));
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (mix->srcpad));
  }

  n = gst_caps_get_size (caps) - 1;
  for (; n >= 0; n--) {
    s = gst_caps_get_structure (caps, n);
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
    if (mix->fps_d != 0) {
      gst_structure_set (s,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    }
  }

  gst_object_unref (mix);

  return caps;
}

static gboolean
gst_videoscaledmixer_query_duration (GstVideoScaledMixer * mix, GstQuery * query)
{
  gint64 max;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  res = TRUE;
  done = FALSE;

  /* Take maximum of all durations */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (mix));
  while (!done) {
    GstIteratorResult ires;
    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);
        gint64 duration;

        /* ask sink peer for duration */
        res &= gst_pad_query_peer_duration (pad, &format, &duration);
        printf( "pad duration : %d %ld\n", res, duration );
        /* take max from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            max = duration;
            done = TRUE;
          }
          /* else see if bigger than current max */
          else if (duration > max)
            max = duration;
        }
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    /* and store the max */
    GST_DEBUG_OBJECT (mix, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (max));
    gst_query_set_duration (query, format, max);
  }

  return res;
}

static gboolean
gst_videoscaledmixer_query_latency (GstVideoScaledMixer * mix, GstQuery * query)
{
  GstClockTime min, max;
  gboolean live;
  gboolean res;
  GstIterator *it;
  gboolean done;

  res = TRUE;
  done = FALSE;
  live = FALSE;
  min = 0;
  max = GST_CLOCK_TIME_NONE;

  /* Take maximum of all latency values */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (mix));
  while (!done) {
    GstIteratorResult ires;
    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);
        GstQuery *peerquery;
        GstClockTime min_cur, max_cur;
        gboolean live_cur;

        peerquery = gst_query_new_latency ();

        /* Ask peer for latency */
        res &= gst_pad_peer_query (pad, peerquery);

        /* take max from all valid return values */
        if (res) {
          gst_query_parse_latency (peerquery, &live_cur, &min_cur, &max_cur);

          if (min_cur > min)
            min = min_cur;

          if (max_cur != GST_CLOCK_TIME_NONE &&
              ((max != GST_CLOCK_TIME_NONE && max_cur > max) ||
                  (max == GST_CLOCK_TIME_NONE)))
            max = max_cur;

          live = live || live_cur;
        }

        gst_query_unref (peerquery);
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        live = FALSE;
        min = 0;
        max = GST_CLOCK_TIME_NONE;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    /* store the results */
    GST_DEBUG_OBJECT (mix, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}

static gboolean
gst_videoscaledmixer_src_query (GstPad * pad, GstQuery * query)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_position (query, format,
              gst_segment_to_stream_time (&mix->segment, GST_FORMAT_TIME,
                  mix->segment.last_stop));
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_videoscaledmixer_query_duration (mix, query);
      break;
    case GST_QUERY_LATENCY:
      res = gst_videoscaledmixer_query_latency (mix, query);
      break;
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads */
      res = FALSE;
      gst_query_unref (query);
      break;
  }

  gst_object_unref (mix);
  return res;
}

static gboolean
gst_videoscaledmixer_src_event (GstPad * pad, GstEvent * event)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (gst_pad_get_parent (pad));
  gboolean result;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      gdouble proportion;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      gst_videoscaledmixer_update_qos (mix, proportion, diff, timestamp);

      result = gst_videoscaledmixer_push_sink_event (mix, event);
      break;
    }
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat fmt;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GSList *l;
      gdouble abs_rate;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &rate, &fmt, &flags, &start_type,
          &start, &stop_type, &stop);

      if (rate <= 0.0) {
        GST_ERROR_OBJECT (mix, "Negative rates not supported yet");
        result = FALSE;
        gst_event_unref (event);
        break;
      }

      GST_DEBUG_OBJECT (mix, "Handling SEEK event");

      /* check if we are flushing */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (mix->srcpad, gst_event_new_flush_start ());

        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads2_set_flushing (mix->collect, TRUE);
      }

      /* now wait for the collected to be finished and mark a new
       * segment */
      GST_COLLECT_PADS2_STREAM_LOCK (mix->collect);

      abs_rate = ABS (rate);

      GST_VIDEOSCALED_MIXER_LOCK (mix);
      for (l = mix->sinkpads; l; l = l->next) {
        GstVideoScaledMixerPad *p = l->data;

        if (flags & GST_SEEK_FLAG_FLUSH) {
          gst_buffer_replace (&p->mixcol->buffer, NULL);
          p->mixcol->start_time = p->mixcol->end_time = -1;
          continue;
        }

        /* Convert to the output segment rate */
        if (mix->segment.abs_rate != abs_rate) {
          if (mix->segment.abs_rate != 1.0 && p->mixcol->buffer) {
            p->mixcol->start_time /= mix->segment.abs_rate;
            p->mixcol->end_time /= mix->segment.abs_rate;
          }
          if (abs_rate != 1.0 && p->mixcol->buffer) {
            p->mixcol->start_time *= abs_rate;
            p->mixcol->end_time *= abs_rate;
          }
        }
      }
      GST_VIDEOSCALED_MIXER_UNLOCK (mix);

      gst_segment_set_seek (&mix->segment, rate, fmt, flags, start_type, start,
          stop_type, stop, NULL);
      mix->segment.last_stop = -1;
      mix->ts_offset = 0;
      mix->nframes = 0;
      mix->newseg_pending = TRUE;

      if (flags & GST_SEEK_FLAG_FLUSH) {
        gst_collect_pads2_set_flushing (mix->collect, FALSE);

        /* we can't send FLUSH_STOP here since upstream could start pushing data
         * after we unlock mix->collect.
         * We set flush_stop_pending to TRUE instead and send FLUSH_STOP after
         * forwarding the seek upstream or from gst_videomixer_collected,
         * whichever happens first.
         */
        mix->flush_stop_pending = TRUE;
      }

      GST_COLLECT_PADS2_STREAM_UNLOCK (mix->collect);

      gst_videoscaledmixer_reset_qos (mix);

      result = gst_videoscaledmixer_push_sink_event (mix, event);

      if (g_atomic_int_compare_and_exchange (&mix->flush_stop_pending, TRUE,
              FALSE)) {
        GST_DEBUG_OBJECT (mix, "pending flush stop");
        gst_pad_push_event (mix->srcpad, gst_event_new_flush_stop ());
      }

      break;
    }
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      gst_event_unref (event);
      break;
    default:
      /* just forward the rest for now */
      result = gst_videoscaledmixer_push_sink_event (mix, event);
      break;
  }
  gst_object_unref (mix);

  return result;
}

static gboolean
gst_videoscaledmixer_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (gst_pad_get_parent_element (pad));
  gboolean ret = FALSE;
  GstVideoFormat fmt;
  gint width, height;
  gint fps_n, fps_d;
  gint par_n, par_d;

  GST_INFO_OBJECT (pad, "set src caps: %" GST_PTR_FORMAT, caps);

  mix->blend = NULL;
  mix->overlay = NULL;

  if (!gst_video_format_parse_caps (caps, &fmt, &width, &height) ||
      !gst_video_parse_caps_framerate (caps, &fps_n, &fps_d) ||
      !gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d))
    goto done;

  GST_VIDEOSCALED_MIXER_LOCK (mix);

  if (mix->fps_n != fps_n || mix->fps_d != fps_d) {
    if (mix->segment.last_stop != -1) {
      mix->ts_offset = mix->segment.last_stop - mix->segment.start;
      mix->nframes = 0;
    }
    gst_videoscaledmixer_reset_qos (mix);
  }

  mix->format = fmt;
  mix->width = width;
  mix->height = height;
  mix->fps_n = fps_n;
  mix->fps_d = fps_d;
  mix->par_n = par_n;
  mix->par_d = par_d;
  ret = TRUE;

  GST_VIDEOSCALED_MIXER_UNLOCK (mix);

done:
  gst_object_unref (mix);

  return ret;
}

static GstFlowReturn
gst_videoscaledmixer_sink_prepare_buffer (GstCollectPads2 * pads,
    GstCollectData2 * data, GstBuffer * buf, GstVideoScaledMixer * mix)
{
  GstVideoScaledMixerPad *pad = GST_VIDEOSCALED_MIXER_PAD (data->pad);
  GstVideoScaledMixerCollect *mixcol = pad->mixcol;
  GstClockTime start_time, end_time;

  start_time = GST_BUFFER_TIMESTAMP (buf);
  if (start_time == -1) {
    GST_ERROR_OBJECT (pad, "Timestamped buffers required!");
    return GST_FLOW_ERROR;
  }

  end_time = GST_BUFFER_DURATION (buf);
  if (end_time == -1)
     end_time = gst_util_uint64_scale_int (GST_SECOND, pad->fps_d, pad->fps_n);
  if (end_time == -1)
    return GST_FLOW_OK;

  start_time = MAX (start_time, mixcol->collect.segment.start);
  start_time =
      gst_segment_to_running_time (&mixcol->collect.segment,
      GST_FORMAT_TIME, start_time);

  end_time += GST_BUFFER_TIMESTAMP (buf);
  if (mixcol->collect.segment.stop != -1)
    end_time = MIN (end_time, mixcol->collect.segment.stop);
  end_time =
      gst_segment_to_running_time (&mixcol->collect.segment,
      GST_FORMAT_TIME, end_time);

  /* Convert to the output segment rate */
  if (mix->segment.abs_rate != 1.0) {
    start_time *= mix->segment.abs_rate;
    end_time *= mix->segment.abs_rate;
  }

  if (mixcol->buffer != NULL && end_time < mixcol->end_time) {
    return GST_COLLECT_PADS2_FLOW_DROP;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_videoscaledmixer_sink_event (GstCollectPads2 * pads, GstCollectData2 * cdata,
    GstEvent * event, GstVideoScaledMixer * mix)
{
  GstVideoScaledMixerPad *pad = GST_VIDEOSCALED_MIXER_PAD (cdata->pad);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "Got %s event on pad %s:%s",
      GST_EVENT_TYPE_NAME (event), GST_DEBUG_PAD_NAME (pad));

  /* return FALSE => event will be forwarded */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;
      gst_event_parse_new_segment (event, NULL, NULL, &fmt, NULL, NULL, NULL);

      g_assert (fmt == GST_FORMAT_TIME);
      /* eat NEWSEGMENT events, collectpads2 unrefs the event */
      ret = FALSE;
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      mix->newseg_pending = TRUE;
      mix->flush_stop_pending = FALSE;
      gst_videoscaledmixer_reset_qos (mix);
      gst_buffer_replace (&pad->mixcol->buffer, NULL);
      pad->mixcol->start_time = -1;
      pad->mixcol->end_time = -1;

      gst_segment_init (&mix->segment, GST_FORMAT_TIME);
      mix->segment.last_stop = -1;
      mix->ts_offset = 0;
      mix->nframes = 0;

      gst_pad_push_event (mix->srcpad, event);
      break;
    default:
      gst_pad_push_event (mix->srcpad, event);
      break;
  }

  return ret;
}

static gboolean
forward_event_func (GstPad * pad, GValue * ret, GstEvent * event)
{
  gst_event_ref (event);
  GST_LOG_OBJECT (pad, "About to send event %s", GST_EVENT_TYPE_NAME (event));
  if (!gst_pad_push_event (pad, event)) {
    g_value_set_boolean (ret, FALSE);
    GST_WARNING_OBJECT (pad, "Sending event  %p (%s) failed.",
        event, GST_EVENT_TYPE_NAME (event));
  } else {
    GST_LOG_OBJECT (pad, "Sent event  %p (%s).",
        event, GST_EVENT_TYPE_NAME (event));
  }
  gst_object_unref (pad);
  return TRUE;
}

static gboolean
gst_videoscaledmixer_push_sink_event (GstVideoScaledMixer * mix, GstEvent * event)
{
  GstIterator *it;
  GValue vret = { 0 };

  GST_LOG_OBJECT (mix, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, TRUE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (mix));
  gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func, &vret,
      event);
  gst_iterator_free (it);
  gst_event_unref (event);

  return g_value_get_boolean (&vret);
}

/* GstElement vmethods */
static GstStateChangeReturn
gst_videoscaledmixer_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LOG_OBJECT (mix, "starting collectpads");
      gst_collect_pads2_start (mix->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOG_OBJECT (mix, "stopping collectpads");
      gst_collect_pads2_stop (mix->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_videoscaledmixer_reset (mix);
      break;
    default:
      break;
  }

  return ret;
}

static GstPad *
gst_videoscaledmixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstVideoScaledMixer *mix;
  GstVideoScaledMixerPad *mixpad;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  mix = GST_VIDEOSCALED_MIXER (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%d")) {
    gint serial = 0;
    gchar *name = NULL;
    GstVideoScaledMixerCollect *mixcol = NULL;

    GST_VIDEOSCALED_MIXER_LOCK (mix);
    if (req_name == NULL || strlen (req_name) < 6
        || !g_str_has_prefix (req_name, "sink_")) {
      /* no name given when requesting the pad, use next available int */
      serial = mix->next_sinkpad++;
    } else {
      /* parse serial number from requested padname */
      serial = g_ascii_strtoull (&req_name[5], NULL, 10);
      if (serial >= mix->next_sinkpad)
        mix->next_sinkpad = serial + 1;
    }
    /* create new pad with the name */
    name = g_strdup_printf ("sink_%d", serial);
    mixpad = g_object_new (GST_TYPE_VIDEOSCALED_MIXER_PAD, "name", name, "direction",
        templ->direction, "template", templ, NULL);
    g_free (name);

    mixpad->zorder = mix->numpads;
    mixpad->xpos = DEFAULT_PAD_XPOS;
    mixpad->ypos = DEFAULT_PAD_YPOS;
    mixpad->alpha = DEFAULT_PAD_ALPHA;
    mixpad->owidth = DEFAULT_PAD_WIDTH;
    mixpad->oheight = DEFAULT_PAD_HEIGHT;
    mixpad->ename = (char*)malloc(strlen(DEFAULT_PAD_ENAME)+1);
    strcpy( mixpad->ename, DEFAULT_PAD_ENAME );

    mixcol = (GstVideoScaledMixerCollect *)
        gst_collect_pads2_add_pad_full (mix->collect, GST_PAD (mixpad),
        sizeof (GstVideoScaledMixerCollect),
        (GstCollectData2DestroyNotify) gst_videoscaledmixer_collect_free, TRUE);

    /* Keep track of each other */
    mixcol->mixpad = mixpad;
    mixpad->mixcol = mixcol;

    mixcol->start_time = -1;
    mixcol->end_time = -1;

    /* Keep an internal list of mixpads for zordering */
    mix->sinkpads = g_slist_append (mix->sinkpads, mixpad);
    mix->numpads++;
    GST_VIDEOSCALED_MIXER_UNLOCK (mix);
  } else {
    return NULL;
  }

  GST_DEBUG_OBJECT (element, "Adding pad %s", GST_PAD_NAME (mixpad));

  /* add the pad to the element */
  gst_element_add_pad (element, GST_PAD (mixpad));
  gst_child_proxy_child_added (GST_OBJECT (mix), GST_OBJECT (mixpad));

  return GST_PAD (mixpad);
}

static void
gst_videoscaledmixer_release_pad (GstElement * element, GstPad * pad)
{
  GstVideoScaledMixer *mix = NULL;
  GstVideoScaledMixerPad *mixpad;
  gboolean update_caps;

  mix = GST_VIDEOSCALED_MIXER (element);

  GST_VIDEOSCALED_MIXER_LOCK (mix);
  if (G_UNLIKELY (g_slist_find (mix->sinkpads, pad) == NULL)) {
    g_warning ("Unknown pad %s", GST_PAD_NAME (pad));
    goto error;
  }

  mixpad = GST_VIDEOSCALED_MIXER_PAD (pad);

  mix->sinkpads = g_slist_remove (mix->sinkpads, pad);
  gst_child_proxy_child_removed (GST_OBJECT (mix), GST_OBJECT (mixpad));
  mix->numpads--;

  update_caps = mix->format != GST_VIDEO_FORMAT_UNKNOWN;
  GST_VIDEOSCALED_MIXER_UNLOCK (mix);

  gst_collect_pads2_remove_pad (mix->collect, pad);

  if (update_caps)
    gst_videoscaledmixer_update_src_caps (mix);

  gst_element_remove_pad (element, pad);
  return;
error:
  GST_VIDEOSCALED_MIXER_UNLOCK (mix);
}

/* GObject vmethods */
static void
gst_videoscaledmixer_finalize (GObject * o)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (o);

  gst_object_unref (mix->collect);
  g_mutex_free (mix->lock);

  G_OBJECT_CLASS (parent_class)->finalize (o);
}

static void
gst_videoscaledmixer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videoscaledmixer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstChildProxy implementation */
static GstObject *
gst_videoscaledmixer_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (child_proxy);
  GstObject *obj;

  GST_VIDEOSCALED_MIXER_LOCK (mix);
  if ((obj = g_slist_nth_data (mix->sinkpads, index)))
    gst_object_ref (obj);
  GST_VIDEOSCALED_MIXER_UNLOCK (mix);
  return obj;
}

static guint
gst_videoscaledmixer_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint count = 0;
  GstVideoScaledMixer *mix = GST_VIDEOSCALED_MIXER (child_proxy);

  GST_VIDEOSCALED_MIXER_LOCK (mix);
  count = mix->numpads;
  GST_VIDEOSCALED_MIXER_UNLOCK (mix);
  GST_INFO_OBJECT (mix, "Children Count: %d", count);
  return count;
}

static void
gst_videoscaledmixer_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  GST_INFO ("intializing child proxy interface");
  iface->get_child_by_index = gst_videoscaledmixer_child_proxy_get_child_by_index;
  iface->get_children_count = gst_videoscaledmixer_child_proxy_get_children_count;
}

/* GObject boilerplate */
static void
gst_videoscaledmixer_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details_simple (element_class, "Scaled video mixer",
      "Filter/Editor/Video",
      "Mix multiple video streams", "Wim Taymans <wim@fluendo.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_videoscaledmixer_class_init (GstVideoScaledMixerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_videoscaledmixer_finalize;

  gobject_class->get_property = gst_videoscaledmixer_get_property;
  gobject_class->set_property = gst_videoscaledmixer_set_property;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_change_state);

  /* Register the pad class */
  g_type_class_ref (GST_TYPE_VIDEOSCALED_MIXER_PAD);
}

static void
gst_videoscaledmixer_init (GstVideoScaledMixer * mix, GstVideoScaledMixerClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mix);

  mix->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_getcaps_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_src_getcaps));
  gst_pad_set_setcaps_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_src_setcaps));
  gst_pad_set_query_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_src_query));
  gst_pad_set_event_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_src_event));
  gst_element_add_pad (GST_ELEMENT (mix), mix->srcpad);

  mix->collect = gst_collect_pads2_new ();

  gst_collect_pads2_set_function (mix->collect,
      (GstCollectPads2Function) GST_DEBUG_FUNCPTR (gst_videoscaledmixer_collected),
      mix);
  gst_collect_pads2_set_event_function (mix->collect,
      (GstCollectPads2EventFunction) gst_videoscaledmixer_sink_event, mix);
  gst_collect_pads2_set_prepare_buffer_function (mix->collect,
      (GstCollectPads2BufferFunction) gst_videoscaledmixer_sink_prepare_buffer, mix);

  mix->lock = g_mutex_new ();
  /* initialize variables */
  gst_videoscaledmixer_reset (mix);
}

/* Element registration */
gboolean
gst_videoscaledmixer_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_videoscaledmixer_debug, "videoscaledmixer", 0,
      "video scaled mixer");

  return gst_element_register (plugin, "videoscaledmixer", GST_RANK_SECONDARY,
      GST_TYPE_VIDEOSCALED_MIXER);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_videoscaledmixer_register(plugin))
    return FALSE;
  
  // we don't use the blending functions at all
  // gst_video_mixer_init_blend ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoscaledmixer",
    "Scalable video mixer",
    plugin_init, VERSION, "LGPL", "VideoScaledMixer", "http://giss.tv")


