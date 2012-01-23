/* Modified videoscaledmixer to be able to resize each component
 * Copyright (C) 2012 Yves Degoyon <ydegoyon@gmail.com>
 * Made on request for Kognate <rebusman>
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
 * Videomixer can accept AYUV, ARGB and BGRA video streams. For each of the requested
 * sink pads it will compare the incoming geometry and framerate to define the
 * output parameters. Indeed output video frames will have the geometry of the
 * biggest incoming video stream and the framerate of the fastest incoming one.
 *
 * All sink pads must be either AYUV, ARGB or BGRA, but a mixture of them is not 
 * supported. The src pad will have the same colorspace as the sinks. 
 * No colorspace conversion is done. 
 * 
 *
 * Individual parameters for each input stream can be configured on the
 * #GstVideoScaledMixerPad.
 *
 * <refsect2>
 * <title>Sample pipelines</title>
 * |[
 * gst-launch videotestsrc pattern=1 ! video/x-raw-yuv,format=\(fourcc\)AYUV, framerate=\(fraction\)10/1, width=100, height=100 ! videobox border-alpha=0 alpha=0.5 top=-70 bottom=-70 right=-220 ! videoscaledmixer name=mix ! ffmpegcolorspace ! xvimagesink videotestsrc ! video/x-raw-yuv, format=\(fourcc\)AYUV, framerate=\(fraction\)5/1, width=320, height=240 ! alpha alpha=0.7 ! mix.
 * ]| A pipeline to demonstrate videoscaledmixer used together with videobox.
 * This should show a 320x240 pixels video test source with some transparency
 * showing the background checker pattern. Another video test source with just
 * the snow pattern of 100x100 pixels is overlayed on top of the first one on
 * the left vertically centered with a small transparency showing the first
 * video test source behind and the checker pattern under it. Note that the
 * framerate of the output video is 10 frames per second.
 * |[
 * gst-launch videotestsrc pattern=1 ! video/x-raw-rgb, framerate=\(fraction\)10/1, width=100, height=100 ! videoscaledmixer name=mix ! ffmpegcolorspace ! ximagesink videotestsrc ! video/x-raw-rgb, framerate=\(fraction\)5/1, width=320, height=240 ! mix.
 * ]| A pipeline to demostrate bgra mixing. (This does not demonstrate alpha blending). 
 * |[
 * gst-launch   videotestsrc pattern=1 ! video/x-raw-yuv,format =\(fourcc\)I420, framerate=\(fraction\)10/1, width=100, height=100 ! videoscaledmixer name=mix ! ffmpegcolorspace ! ximagesink videotestsrc ! video/x-raw-yuv,format=\(fourcc\)I420, framerate=\(fraction\)5/1, width=320, height=240 ! mix.
 * ]| A pipeline to test I420
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_GCC_ASM
#if defined(HAVE_CPU_I386) || defined(HAVE_CPU_X86_64)
#define BUILD_X86_ASM
#endif
#endif

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/controller/gstcontroller.h>
#include <gst/video/video.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "gstvideoscaledmixer.h"

GST_DEBUG_CATEGORY_STATIC (gst_videoscaledmixer_debug);
#define GST_CAT_DEFAULT gst_videoscaledmixer_debug

#define GST_VIDEO_SCALED_MIXER_GET_STATE_LOCK(mix) \
  (GST_VIDEO_SCALED_MIXER(mix)->state_lock)
#define GST_VIDEO_SCALED_MIXER_STATE_LOCK(mix) \
  (g_mutex_lock(GST_VIDEO_SCALED_MIXER_GET_STATE_LOCK (mix)))
#define GST_VIDEO_SCALED_MIXER_STATE_UNLOCK(mix) \
  (g_mutex_unlock(GST_VIDEO_SCALED_MIXER_GET_STATE_LOCK (mix)))

static GType gst_videoscaledmixer_get_type (void);

static void gst_videoscaledmixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_videoscaledmixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gboolean gst_videoscaledmixer_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_videoscaledmixer_sink_event (GstPad * pad, GstEvent * event);

static void gst_videoscaledmixer_sort_pads (GstVideoScaledMixer * mix);

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

GType gst_videoscaledmixer_pad_get_type (void);
G_DEFINE_TYPE (GstVideoScaledMixerPad, gst_videoscaledmixer_pad, GST_TYPE_PAD);

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
gst_videoscaledmixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoScaledMixerPad *pad = GST_VIDEO_SCALED_MIXER_PAD (object);

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

static void
gst_videoscaledmixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoScaledMixerPad *pad = GST_VIDEO_SCALED_MIXER_PAD (object);
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent (GST_PAD (pad)));

  switch (prop_id) {
    case PROP_PAD_ZORDER:
      GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);
      pad->zorder = g_value_get_uint (value);
      gst_videoscaledmixer_sort_pads (mix);
      GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
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

/* Perform qos calculations before processing the next frame. Returns TRUE if
 * the frame should be processed, FALSE if the frame can be dropped entirely */
static gboolean
gst_videoscaledmixer_do_qos (GstVideoScaledMixer * mix, GstClockTime timestamp)
{
  GstClockTime qostime, earliest_time;
  gdouble proportion;

  /* no timestamp, can't do QoS => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    GST_LOG_OBJECT (mix, "invalid timestamp, can't do QoS, process frame");
    return TRUE;
  }

  /* get latest QoS observation values */
  gst_videoscaledmixer_read_qos (mix, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    GST_LOG_OBJECT (mix, "no observation yet, process frame");
    return TRUE;
  }

  /* qos is done on running time */
  qostime =
      gst_segment_to_running_time (&mix->segment, GST_FORMAT_TIME, timestamp);

  /* see how our next timestamp relates to the latest qos timestamp */
  GST_LOG_OBJECT (mix, "qostime %" GST_TIME_FORMAT ", earliest %"
      GST_TIME_FORMAT, GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));

  if (qostime != GST_CLOCK_TIME_NONE && qostime <= earliest_time) {
    GST_DEBUG_OBJECT (mix, "we are late, drop frame");
    return TRUE;
    // return FALSE;
  }

  GST_LOG_OBJECT (mix, "process frame");
  return TRUE;
}

static void
gst_videoscaledmixer_set_master_geometry (GstVideoScaledMixer * mix)
{
  GSList *walk;
  gint width = 0, height = 0, fps_n = 0, fps_d = 0, par_n = 0, par_d = 0;
  GstVideoScaledMixerPad *master = NULL;

  walk = mix->sinkpads;
  while (walk) {
    GstVideoScaledMixerPad *mixpad = GST_VIDEO_SCALED_MIXER_PAD (walk->data);

    walk = g_slist_next (walk);

    /* Biggest input geometry will be our output geometry */
    width = MAX (width, mixpad->in_width);
    height = MAX (height, mixpad->in_height);

    /* If mix framerate < mixpad framerate, using fractions */
    GST_DEBUG_OBJECT (mixpad, "comparing framerate %d/%d to mixpad's %d/%d",
        fps_n, fps_d, mixpad->fps_n, mixpad->fps_d);
    if ((!fps_n && !fps_d) ||
        ((gint64) fps_n * mixpad->fps_d < (gint64) mixpad->fps_n * fps_d)) {
      fps_n = mixpad->fps_n;
      fps_d = mixpad->fps_d;
      par_n = mixpad->par_n;
      par_d = mixpad->par_d;
      GST_DEBUG_OBJECT (mixpad, "becomes the master pad");
      master = mixpad;
    }
  }

  /* set results */
  if (mix->master != master || mix->in_width != width
      || mix->in_height != height || mix->fps_n != fps_n
      || mix->fps_d != fps_d || mix->par_n != par_n || mix->par_d != par_d) {
    mix->setcaps = TRUE;
    mix->sendseg = TRUE;
    gst_videoscaledmixer_reset_qos (mix);
    mix->master = master;
    mix->in_width = width;
    mix->in_height = height;
    mix->fps_n = fps_n;
    mix->fps_d = fps_d;
    mix->par_n = par_n;
    mix->par_d = par_d;
  }
}

static gboolean
gst_videoscaledmixer_pad_sink_setcaps (GstPad * pad, GstCaps * vscaps)
{
  GstVideoScaledMixer *mix;
  GstVideoScaledMixerPad *mixpad;
  GstStructure *structure;
  gint in_width, in_height;
  gboolean ret = FALSE;
  const GValue *framerate, *par;

  GST_INFO_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, vscaps);

  mix = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent (pad));
  mixpad = GST_VIDEO_SCALED_MIXER_PAD (pad);

  if (!mixpad)
    goto beach;

  structure = gst_caps_get_structure (vscaps, 0);

  if (!gst_structure_get_int (structure, "width", &in_width)
      || !gst_structure_get_int (structure, "height", &in_height)
      || (framerate = gst_structure_get_value (structure, "framerate")) == NULL)
    goto beach;
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);
  mixpad->fps_n = gst_value_get_fraction_numerator (framerate);
  mixpad->fps_d = gst_value_get_fraction_denominator (framerate);
  if (par) {
    mixpad->par_n = gst_value_get_fraction_numerator (par);
    mixpad->par_d = gst_value_get_fraction_denominator (par);
  } else {
    mixpad->par_n = mixpad->par_d = 1;
  }

  mixpad->in_width = in_width;
  mixpad->in_height = in_height;

  gst_videoscaledmixer_set_master_geometry (mix);
  GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);

  ret = TRUE;

beach:
  gst_object_unref (mix);

  return ret;
}

static GstCaps *
gst_videoscaledmixer_pad_sink_getcaps (GstPad * pad)
{
  GstVideoScaledMixer *mix;
  GstVideoScaledMixerPad *mixpad;
  GstCaps *res = NULL;
  GstCaps *mastercaps;
  GstStructure *st;

  mix = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent (pad));
  mixpad = GST_VIDEO_SCALED_MIXER_PAD (pad);

  if (!mixpad)
    goto beach;

  /* Get downstream allowed caps */
  res = gst_pad_get_allowed_caps (mix->srcpad);
  if (G_UNLIKELY (res == NULL)) {
    res = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
    goto beach;
  }

  GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);

  /* Return as-is if not other sinkpad set as master */
  if (mix->master == NULL) {
    GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
    goto beach;
  }

  mastercaps = gst_pad_get_fixed_caps_func (GST_PAD (mix->master));

  /* If master pad caps aren't negotiated yet, return downstream
   * allowed caps */
  if (!GST_CAPS_IS_SIMPLE (mastercaps)) {
    GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
    gst_caps_unref (mastercaps);
    goto beach;
  }

  gst_caps_unref (res);
  res = gst_caps_make_writable (mastercaps);
  st = gst_caps_get_structure (res, 0);
  gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  if (!gst_structure_has_field (st, "pixel-aspect-ratio"))
    gst_structure_set (st, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);

  GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);


beach:
  GST_DEBUG_OBJECT (pad, "Returning %" GST_PTR_FORMAT, res);

  return res;
}

/*
* We accept the caps if it has the same format as other sink pads in 
* the element.
*/
static gboolean
gst_videoscaledmixer_pad_sink_acceptcaps (GstPad * pad, GstCaps * vscaps)
{
  gboolean ret;
  GstVideoScaledMixer *mix;
  GstCaps *acceptedCaps;

  mix = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (pad, "%" GST_PTR_FORMAT, vscaps);
  GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);

  if (mix->master) {
    acceptedCaps = gst_pad_get_fixed_caps_func (GST_PAD (mix->master));
    acceptedCaps = gst_caps_make_writable (acceptedCaps);
    GST_LOG_OBJECT (pad, "master's caps %" GST_PTR_FORMAT, acceptedCaps);
    if (GST_CAPS_IS_SIMPLE (acceptedCaps)) {
      GstStructure *s;
      s = gst_caps_get_structure (acceptedCaps, 0);
      gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
      if (!gst_structure_has_field (s, "pixel-aspect-ratio"))
        gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
            NULL);
    }
  } else {
    acceptedCaps = gst_pad_get_fixed_caps_func (pad);
  }

  GST_INFO_OBJECT (pad, "vscaps: %" GST_PTR_FORMAT, vscaps);
  GST_INFO_OBJECT (mix, "vscaps: %" GST_PTR_FORMAT, vscaps);
  GST_INFO_OBJECT (pad, "acceptedCaps: %" GST_PTR_FORMAT, acceptedCaps);
  GST_INFO_OBJECT (mix, "vscaps: %" GST_PTR_FORMAT, vscaps);

  ret = gst_caps_can_intersect (vscaps, acceptedCaps);
  GST_INFO_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT, (ret ? "" : "not "),
      vscaps);
  gst_caps_unref (acceptedCaps);
  GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
  gst_object_unref (mix);
  return ret;
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

/* VideoScaledMixer signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

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

static void gst_videoscaledmixer_finalize (GObject * object);

static GstCaps *gst_videoscaledmixer_getcaps (GstPad * pad);
static gboolean gst_videoscaledmixer_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_videoscaledmixer_query (GstPad * pad, GstQuery * query);

static GstFlowReturn gst_videoscaledmixer_collected (GstCollectPads * pads,
    GstVideoScaledMixer * mix);
static GstPad *gst_videoscaledmixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_videoscaledmixer_release_pad (GstElement * element, GstPad * pad);

static void gst_videoscaledmixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videoscaledmixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_videoscaledmixer_change_state (GstElement * element,
    GstStateChange transition);

/*static guint gst_videoscaledmixer_signals[LAST_SIGNAL] = { 0 }; */

static void gst_videoscaledmixer_child_proxy_init (gpointer g_iface,
    gpointer iface_data);
static void _do_init (GType object_type);

GST_BOILERPLATE_FULL (GstVideoScaledMixer, gst_videoscaledmixer, GstElement,
    GST_TYPE_ELEMENT, _do_init);

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
  GST_INFO ("GstChildProxy interface registered");
}

static GstObject *
gst_videoscaledmixer_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (child_proxy);
  GstObject *obj;

  GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);
  if ((obj = g_slist_nth_data (mix->sinkpads, index)))
    gst_object_ref (obj);
  GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
  return obj;
}

static guint
gst_videoscaledmixer_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint count = 0;
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (child_proxy);

  GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);
  count = mix->numpads;
  GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
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

static void
gst_videoscaledmixer_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details_simple (element_class, "Video mixer",
      "Filter/Editor/Video",
      "Mix and scale video sources", "Yves Degoyon <ydegoyon@gmail.com>");
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
  (void) (GST_TYPE_VIDEO_SCALED_MIXER_PAD);
}

static void
gst_videoscaledmixer_collect_free (GstVideoScaledMixerCollect * mixcol)
{
  if (mixcol->buffer) {
    gst_buffer_unref (mixcol->buffer);
    mixcol->buffer = NULL;
  }
}

static void
gst_videoscaledmixer_reset (GstVideoScaledMixer * mix)
{
  GSList *walk;

  mix->in_width = 0;
  mix->in_height = 0;
  mix->out_width = 0;
  mix->out_height = 0;
  mix->fps_n = mix->fps_d = 0;
  mix->par_n = mix->par_d = 1;
  mix->setcaps = FALSE;
  mix->sendseg = FALSE;

  mix->segment_position = 0;
  gst_segment_init (&mix->segment, GST_FORMAT_TIME);

  gst_videoscaledmixer_reset_qos (mix);

  mix->fmt = GST_VIDEO_FORMAT_UNKNOWN;

  mix->last_ts = 0;
  mix->last_duration = -1;

  /* clean up collect data */
  walk = mix->collect->data;
  while (walk) {
    GstVideoScaledMixerCollect *data = (GstVideoScaledMixerCollect *) walk->data;

    gst_videoscaledmixer_collect_free (data);
    walk = g_slist_next (walk);
  }

  mix->next_sinkpad = 0;
  mix->flush_stop_pending = FALSE;
}

static void
gst_videoscaledmixer_init (GstVideoScaledMixer * mix, GstVideoScaledMixerClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mix);

  mix->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_getcaps_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_getcaps));
  gst_pad_set_setcaps_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_setcaps));
  gst_pad_set_query_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_query));
  gst_pad_set_event_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoscaledmixer_src_event));
  gst_element_add_pad (GST_ELEMENT (mix), mix->srcpad);

  mix->collect = gst_collect_pads_new ();

  gst_collect_pads_set_function (mix->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_videoscaledmixer_collected),
      mix);

  mix->state_lock = g_mutex_new ();
  /* initialize variables */
  gst_videoscaledmixer_reset (mix);
}

static void
gst_videoscaledmixer_finalize (GObject * object)
{
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (object);

  gst_object_unref (mix->collect);
  g_mutex_free (mix->state_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
gst_videoscaledmixer_query (GstPad * pad, GstQuery * query)
{
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, mix->last_ts);
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
       * sinkpads, send to the master pad until then */
      res = gst_pad_query (GST_PAD_CAST (mix->master), query);
      break;
  }

  gst_object_unref (mix);
  return res;
}

static GstCaps *
gst_videoscaledmixer_getcaps (GstPad * pad)
{
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent (pad));
  GstCaps *caps;
  GstStructure *structure;
  int numCaps;

  if (mix->master) {
    caps =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_PAD (mix->master)));
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (mix->srcpad));
  }

  numCaps = gst_caps_get_size (caps) - 1;
  for (; numCaps >= 0; numCaps--) {
    structure = gst_caps_get_structure (caps, numCaps);
    if (mix->out_width != 0) {
      gst_structure_set (structure, "width", G_TYPE_INT, mix->out_width, NULL);
    }
    if (mix->out_height != 0) {
      gst_structure_set (structure, "height", G_TYPE_INT, mix->out_height,
          NULL);
    }
    if (mix->fps_d != 0) {
      gst_structure_set (structure,
          "framerate", GST_TYPE_FRACTION, mix->fps_n, mix->fps_d, NULL);
    }
  }

  gst_object_unref (mix);

  return caps;
}

static gboolean
gst_videoscaledmixer_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVideoScaledMixer *mixer = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent_element (pad));
  gboolean ret = FALSE;

  GST_INFO_OBJECT (mixer, "set src caps: %" GST_PTR_FORMAT, caps);

  mixer->blend = NULL;
  mixer->fill_checker = NULL;
  mixer->fill_color = NULL;

  if (!gst_video_format_parse_caps (caps, &mixer->fmt, NULL, NULL))
    goto done;

  ret=TRUE;

done:
  gst_object_unref (mixer);

  return ret;
}

static GstPad *
gst_videoscaledmixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstVideoScaledMixer *mix = NULL;
  GstVideoScaledMixerPad *mixpad = NULL;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  g_return_val_if_fail (templ != NULL, NULL);

  if (G_UNLIKELY (templ->direction != GST_PAD_SINK)) {
    g_warning ("videoscaledmixer: request pad that is not a SINK pad");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_VIDEO_SCALED_MIXER (element), NULL);

  mix = GST_VIDEO_SCALED_MIXER (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%d")) {
    gint serial = 0;
    gchar *name = NULL;
    GstVideoScaledMixerCollect *mixcol = NULL;

    GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);
    if (req_name == NULL || strlen (req_name) < 6
        || !g_str_has_prefix (req_name, "sink_")) {
      /* no name given when requesting the pad, use next available int */
      serial = mix->next_sinkpad++;
    } else {
      /* parse serial number from requested padname */
      serial = atoi (&req_name[5]);
      if (serial >= mix->next_sinkpad)
        mix->next_sinkpad = serial + 1;
    }
    /* create new pad with the name */
    name = g_strdup_printf ("sink_%d", serial);
    mixpad = g_object_new (GST_TYPE_VIDEO_SCALED_MIXER_PAD, "name", name, "direction",
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
        gst_collect_pads_add_pad (mix->collect, GST_PAD (mixpad),
        sizeof (GstVideoScaledMixerCollect));

    /* FIXME: hacked way to override/extend the event function of
     * GstCollectPads; because it sets its own event function giving the
     * element no access to events */
    mix->collect_event =
        (GstPadEventFunction) GST_PAD_EVENTFUNC (GST_PAD (mixpad));
    gst_pad_set_event_function (GST_PAD (mixpad),
        GST_DEBUG_FUNCPTR (gst_videoscaledmixer_sink_event));

    /* Keep track of each other */
    mixcol->mixpad = mixpad;
    mixpad->mixcol = mixcol;

    /* Keep an internal list of mixpads for zordering */
    mix->sinkpads = g_slist_append (mix->sinkpads, mixpad);
    mix->numpads++;
    GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
  } else {
    g_warning ("videoscaledmixer: this is not our template!");
    return NULL;
  }

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

  mix = GST_VIDEO_SCALED_MIXER (element);
  GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);
  if (G_UNLIKELY (g_slist_find (mix->sinkpads, pad) == NULL)) {
    g_warning ("Unknown pad %s", GST_PAD_NAME (pad));
    goto error;
  }

  mixpad = GST_VIDEO_SCALED_MIXER_PAD (pad);

  mix->sinkpads = g_slist_remove (mix->sinkpads, pad);
  gst_videoscaledmixer_collect_free (mixpad->mixcol);
  gst_collect_pads_remove_pad (mix->collect, pad);
  gst_child_proxy_child_removed (GST_OBJECT (mix), GST_OBJECT (mixpad));
  /* determine possibly new geometry and master */
  gst_videoscaledmixer_set_master_geometry (mix);
  mix->numpads--;
  GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);

  gst_element_remove_pad (element, pad);
  return;
error:
  GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
}

static int
pad_zorder_compare (const GstVideoScaledMixerPad * pad1,
    const GstVideoScaledMixerPad * pad2)
{
  return pad1->zorder - pad2->zorder;
}

static void
gst_videoscaledmixer_sort_pads (GstVideoScaledMixer * mix)
{
  mix->sinkpads = g_slist_sort (mix->sinkpads,
      (GCompareFunc) pad_zorder_compare);
}

/* try to get a buffer on all pads. As long as the queued value is
 * negative, we skip buffers */
static gboolean
gst_videoscaledmixer_fill_queues (GstVideoScaledMixer * mix)
{
  GSList *walk = NULL;
  gboolean eos = TRUE;

  g_return_val_if_fail (GST_IS_VIDEO_SCALED_MIXER (mix), FALSE);

  /* try to make sure we have a buffer from each usable pad first */
  walk = mix->collect->data;
  while (walk) {
    GstCollectData *data = (GstCollectData *) walk->data;
    GstVideoScaledMixerCollect *mixcol = (GstVideoScaledMixerCollect *) data;
    GstVideoScaledMixerPad *mixpad = mixcol->mixpad;

    walk = g_slist_next (walk);

    if (mixcol->buffer == NULL) {
      GstBuffer *buf = NULL;

      GST_LOG_OBJECT (mix, "we need a new buffer");

      buf = gst_collect_pads_peek (mix->collect, data);

      if (buf) {
        guint64 duration;

        mixcol->buffer = buf;
        duration = GST_BUFFER_DURATION (mixcol->buffer);

        GST_LOG_OBJECT (mix, "we have a buffer with duration %" GST_TIME_FORMAT
            ", queued %" GST_TIME_FORMAT, GST_TIME_ARGS (duration),
            GST_TIME_ARGS (mixpad->queued));

        /* no duration on the buffer, use the framerate */
        if (!GST_CLOCK_TIME_IS_VALID (duration)) {
          if (mixpad->fps_n == 0) {
            duration = GST_CLOCK_TIME_NONE;
          } else {
            duration =
                gst_util_uint64_scale_int (GST_SECOND, mixpad->fps_d,
                mixpad->fps_n);
          }
        }
        if (GST_CLOCK_TIME_IS_VALID (duration))
          mixpad->queued += duration;
        else if (!mixpad->queued)
          mixpad->queued = GST_CLOCK_TIME_NONE;

        GST_LOG_OBJECT (mix, "now queued: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (mixpad->queued));
      } else {
        GST_LOG_OBJECT (mix, "pop returned a NULL buffer");
      }
    }
    if (mix->sendseg && (mixpad == mix->master)) {
      GstEvent *event;
      gint64 stop, start;
      GstSegment *segment = &data->segment;

      /* FIXME, use rate/applied_rate as set on all sinkpads.
       * - currently we just set rate as received from last seek-event
       * We could potentially figure out the duration as well using
       * the current segment positions and the stated stop positions.
       * Also we just start from stream time 0 which is rather
       * weird. For non-synchronized mixing, the time should be
       * the min of the stream times of all received segments,
       * rationale being that the duration is at least going to
       * be as long as the earliest stream we start mixing. This
       * would also be correct for synchronized mixing but then
       * the later streams would be delayed until the stream times
       * match.
       */
      GST_INFO_OBJECT (mix, "_sending play segment");

      start = segment->accum;

      /* get the duration of the segment if we can and add it to the accumulated
       * time on the segment. */
      if (segment->stop != -1 && segment->start != -1)
        stop = start + (segment->stop - segment->start);
      else
        stop = -1;

      gst_segment_set_newsegment (&mix->segment, FALSE, segment->rate,
          segment->format, start, stop, start + mix->segment_position);
      event =
          gst_event_new_new_segment_full (FALSE, segment->rate, 1.0,
          segment->format, start, stop, start + mix->segment_position);
      gst_pad_push_event (mix->srcpad, event);
      mix->sendseg = FALSE;
    }

    if (mixcol->buffer != NULL && GST_CLOCK_TIME_IS_VALID (mixpad->queued)) {
      /* got a buffer somewhere so we're not eos */
      eos = FALSE;
    }
  }

  return eos;
}

/* blend all buffers present on the pads */
static void
gst_videoscaledmixer_blend_buffers (GstVideoScaledMixer * mix, GstBuffer * outbuf)
{
  GSList *l;

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

  if ( mix->fmt == GST_VIDEO_FORMAT_I420 )
  {
    // now copying data in an optimized way
    guint8 *pYData, *pUData, *pVData;;
    guint8 *pYOutputData = GST_BUFFER_DATA (outbuf);
    guint8 *pUOutputData = pYOutputData+(mix->out_width*mix->out_height);
    guint8 *pVOutputData = pYOutputData+(mix->out_width*mix->out_height)+(mix->out_width*mix->out_height>>2);
    GstVideoScaledMixerPad *cpad;
    int px, py;
    int rx, ry;
    int pzorder;
    struct timeval startt, endt;

    gettimeofday( &startt, NULL );

    // for all output pixel
    for ( py=0; py<mix->out_height; py++ )
    {
      for ( px=0; px<mix->out_width; px++ )
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
                 pUData = pYData+(pad->in_width*pad->in_height);
                 pVData = pYData+(pad->in_width*pad->in_height)+(pad->in_width*pad->in_height>>2);
                 cpad=pad;
              }
           }
        }

        if ( pYData != NULL )
        {
           if ( ( cpad->in_width != cpad->owidth ) || ( cpad->in_height != cpad->oheight ) )
           {
             rx = (int)((double)(px-cpad->xpos)*(double)cpad->in_width/(double)cpad->owidth);
             ry = (int)((double)(py-cpad->ypos)*(double)cpad->in_height/(double)cpad->oheight);
           }
           else
           {
              rx = px;
              ry = py;
           }
           *(pYOutputData+py*mix->out_width+px) = *(pYData+ry*cpad->in_width+rx)*(cpad->alpha);
           *(pUOutputData+(py>>1)*(mix->out_width>>1)+(px>>1)) = *(pUData+(ry>>1)*(cpad->in_width>>1)+(rx>>1));
           *(pVOutputData+(py>>1)*(mix->out_width>>1)+(px>>1)) = *(pVData+(ry>>1)*(cpad->in_width>>1)+(rx>>1));
        }
      }
    }

    gettimeofday( &endt, NULL );
    GST_DEBUG_OBJECT ( mix, "mixer loop took %ld\n", (endt.tv_sec-startt.tv_sec)*1000000+(endt.tv_usec-startt.tv_usec) );

  }
  else
  {
    GST_ERROR_OBJECT (mix, "Your output will be green, sorry, only YUV/I420 format is supported in this experimental mixer");
  }

  return;

}

/* remove buffers from the queue that were expired in the
 * interval of the master, we also prepare the queued value
 * in the pad so that we can skip and fill buffers later on */
static void
gst_videoscaledmixer_update_queues (GstVideoScaledMixer * mix)
{
  GSList *walk;
  gint64 interval;

  interval = mix->master->queued;
  if (interval <= 0) {
    if (mix->fps_n == 0) {
      interval = G_MAXINT64;
    } else {
      interval = gst_util_uint64_scale_int (GST_SECOND, mix->fps_d, mix->fps_n);
    }
    GST_LOG_OBJECT (mix, "set interval to %" G_GINT64_FORMAT " nanoseconds",
        interval);
  }

  walk = mix->sinkpads;
  while (walk) {
    GstVideoScaledMixerPad *pad = GST_VIDEO_SCALED_MIXER_PAD (walk->data);
    GstVideoScaledMixerCollect *mixcol = pad->mixcol;

    walk = g_slist_next (walk);

    if (mixcol->buffer != NULL) {
      pad->queued -= interval;
      GST_LOG_OBJECT (pad, "queued now %" G_GINT64_FORMAT, pad->queued);
      if (pad->queued <= 0) {
        GstBuffer *buffer =
            gst_collect_pads_pop (mix->collect, &mixcol->collect);

        GST_LOG_OBJECT (pad, "unreffing buffer");
        if (buffer)
          gst_buffer_unref (buffer);
        else
          GST_WARNING_OBJECT (pad,
              "Buffer was removed by GstCollectPads in the meantime");

        gst_buffer_unref (mixcol->buffer);
        mixcol->buffer = NULL;
      }
    }
  }
}

static GstFlowReturn
gst_videoscaledmixer_collected (GstCollectPads * pads, GstVideoScaledMixer * mix)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf = NULL;
  size_t outsize = 0;
  gboolean eos = FALSE;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (GST_IS_VIDEO_SCALED_MIXER (mix), GST_FLOW_ERROR);

  /* This must be set, otherwise we have no caps */
  if (G_UNLIKELY (mix->in_width == 0))
    return GST_FLOW_NOT_NEGOTIATED;

  if (g_atomic_int_compare_and_exchange (&mix->flush_stop_pending, TRUE, FALSE)) {
    GST_DEBUG_OBJECT (mix, "pending flush stop");
    gst_pad_push_event (mix->srcpad, gst_event_new_flush_stop ());
  }

  GST_LOG_OBJECT (mix, "all pads are collected");
  GST_VIDEO_SCALED_MIXER_STATE_LOCK (mix);

  eos = gst_videoscaledmixer_fill_queues (mix);

  if (eos) {
    /* Push EOS downstream */
    GST_LOG_OBJECT (mix, "all our sinkpads are EOS, pushing downstream");
    gst_pad_push_event (mix->srcpad, gst_event_new_eos ());
    ret = GST_FLOW_WRONG_STATE;
    goto error;
  }

  /* If geometry has changed we need to set new caps on the buffer */
  if (mix->in_width != mix->out_width || mix->in_height != mix->out_height
      || mix->setcaps) {
    GstCaps *newcaps = NULL;

    newcaps = gst_caps_make_writable
        (gst_pad_get_negotiated_caps (GST_PAD (mix->master)));
    gst_caps_set_simple (newcaps,
        "width", G_TYPE_INT, mix->in_width,
        "height", G_TYPE_INT, mix->in_height,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, mix->par_n, mix->par_d, NULL);

    mix->out_width = mix->in_width;
    mix->out_height = mix->in_height;
    mix->setcaps = FALSE;

    /* Calculating out buffer size from input size */
    gst_pad_set_caps (mix->srcpad, newcaps);
    gst_caps_unref (newcaps);
  }

  /* Get timestamp & duration */
  if (mix->master->mixcol->buffer != NULL) {
    GstClockTime in_ts;
    GstSegment *seg;
    GstVideoScaledMixerCollect *mixcol = mix->master->mixcol;

    seg = &mixcol->collect.segment;
    in_ts = GST_BUFFER_TIMESTAMP (mixcol->buffer);

    timestamp = gst_segment_to_running_time (seg, GST_FORMAT_TIME, in_ts);
    duration = GST_BUFFER_DURATION (mixcol->buffer);

    mix->last_ts = timestamp;
    mix->last_duration = duration;
  } else {
    timestamp = mix->last_ts;
    duration = mix->last_duration;
  }

  if (GST_CLOCK_TIME_IS_VALID (duration))
    mix->last_ts += duration;

  if (!gst_videoscaledmixer_do_qos (mix, timestamp)) {
    gst_videoscaledmixer_update_queues (mix);
    GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
    ret = GST_FLOW_OK;
    goto beach;
  }

  /* allocate an output buffer */
  outsize =
      gst_video_format_get_size (mix->fmt, mix->out_width, mix->out_height);
  ret =
      gst_pad_alloc_buffer_and_set_caps (mix->srcpad, GST_BUFFER_OFFSET_NONE,
      outsize, GST_PAD_CAPS (mix->srcpad), &outbuf);

  if (ret != GST_FLOW_OK) {
    goto error;
  }

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;

  gst_videoscaledmixer_blend_buffers (mix, outbuf);

  gst_videoscaledmixer_update_queues (mix);
  GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);

  ret = gst_pad_push (mix->srcpad, outbuf);
  GST_LOG_OBJECT (mix, "buffer sent");

beach:
  return ret;

  /* ERRORS */
error:
  {
    if (outbuf)
      gst_buffer_unref (outbuf);

    GST_VIDEO_SCALED_MIXER_STATE_UNLOCK (mix);
    goto beach;
  }
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

/* forwards the event to all sinkpads, takes ownership of the
 * event
 *
 * Returns: TRUE if the event could be forwarded on all
 * sinkpads.
 */
static gboolean
forward_event (GstVideoScaledMixer * mix, GstEvent * event)
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

static gboolean
gst_videoscaledmixer_src_event (GstPad * pad, GstEvent * event)
{
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent (pad));
  gboolean result;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      gdouble proportion;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      gst_videoscaledmixer_update_qos (mix, proportion, diff, timestamp);
      gst_event_unref (event);

      /* TODO: The QoS event should be transformed and send upstream */
      result = TRUE;
      break;
    }
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      GstSeekType curtype;
      gint64 cur;

      /* parse the seek parameters */
      gst_event_parse_seek (event, NULL, NULL, &flags, &curtype,
          &cur, NULL, NULL);

      /* check if we are flushing */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads_set_flushing (mix->collect, TRUE);

        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (mix->srcpad, gst_event_new_flush_start ());
      }

      /* now wait for the collected to be finished and mark a new
       * segment */
      GST_OBJECT_LOCK (mix->collect);
      if (curtype == GST_SEEK_TYPE_SET)
        mix->segment_position = cur;
      else
        mix->segment_position = 0;
      mix->sendseg = TRUE;

      if (flags & GST_SEEK_FLAG_FLUSH) {
        gst_collect_pads_set_flushing (mix->collect, FALSE);

        /* we can't send FLUSH_STOP here since upstream could start pushing data
         * after we unlock mix->collect.
         * We set flush_stop_pending to TRUE instead and send FLUSH_STOP after
         * forwarding the seek upstream or from gst_videoscaledmixer_collected,
         * whichever happens first.
         */
        mix->flush_stop_pending = TRUE;
      }

      GST_OBJECT_UNLOCK (mix->collect);
      gst_videoscaledmixer_reset_qos (mix);

      result = forward_event (mix, event);

      if (g_atomic_int_compare_and_exchange (&mix->flush_stop_pending,
              TRUE, FALSE)) {
        GST_DEBUG_OBJECT (mix, "pending flush stop");
        gst_pad_push_event (mix->srcpad, gst_event_new_flush_stop ());
      }

      break;
    }
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      break;
    default:
      /* just forward the rest for now */
      result = forward_event (mix, event);
      break;
  }
  gst_object_unref (mix);

  return result;
}

static gboolean
gst_videoscaledmixer_sink_event (GstPad * pad, GstEvent * event)
{
  GstVideoScaledMixerPad *vpad = GST_VIDEO_SCALED_MIXER_PAD (pad);
  GstVideoScaledMixer *videoscaledmixer = GST_VIDEO_SCALED_MIXER (gst_pad_get_parent (pad));
  gboolean ret;

  GST_DEBUG_OBJECT (pad, "Got %s event on pad %s:%s",
      GST_EVENT_TYPE_NAME (event), GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      /* mark a pending new segment. This event is synchronized
       * with the streaming thread so we can safely update the
       * variable without races. It's somewhat weird because we
       * assume the collectpads forwarded the FLUSH_STOP past us
       * and downstream (using our source pad, the bastard!).
       */
      videoscaledmixer->sendseg = TRUE;
      videoscaledmixer->flush_stop_pending = FALSE;
      gst_videoscaledmixer_reset_qos (videoscaledmixer);

      /* Reset pad state after FLUSH_STOP */
      if (vpad->mixcol->buffer)
        gst_buffer_unref (vpad->mixcol->buffer);
      vpad->mixcol->buffer = NULL;
      vpad->queued = 0;
      break;
    case GST_EVENT_NEWSEGMENT:
      if (!videoscaledmixer->master || vpad == videoscaledmixer->master) {
        videoscaledmixer->sendseg = TRUE;
        gst_videoscaledmixer_reset_qos (videoscaledmixer);
      }
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = videoscaledmixer->collect_event (pad, event);

  gst_object_unref (videoscaledmixer);
  return ret;
}


static void
gst_videoscaledmixer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (object);

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
  GstVideoScaledMixer *mix = GST_VIDEO_SCALED_MIXER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_videoscaledmixer_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoScaledMixer *mix;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_VIDEO_SCALED_MIXER (element), GST_STATE_CHANGE_FAILURE);

  mix = GST_VIDEO_SCALED_MIXER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LOG_OBJECT (mix, "starting collectpads");
      gst_collect_pads_start (mix->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOG_OBJECT (mix, "stopping collectpads");
      gst_collect_pads_stop (mix->collect);
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

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_videoscaledmixer_debug, "videoscaledmixer", 0,
      "video scaled mixer");

  return gst_element_register (plugin, "videoscaledmixer", GST_RANK_PRIMARY,
      GST_TYPE_VIDEO_SCALED_MIXER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoscaledmixer",
    "Video scaled mixer", plugin_init, VERSION, "LGPL", "videoscaledmixer", "http://giss.tv")
