/* Generic video mixer plugin
 * Copyright (C) 2008 Wim Taymans <wim@fluendo.com>
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
 
#ifndef __GST_VIDEOSCALED_MIXER_H__
#define __GST_VIDEOSCALED_MIXER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "videoscaledmixerpad.h"

G_BEGIN_DECLS

#define GST_TYPE_VIDEOSCALED_MIXER (gst_videoscaledmixer_get_type())
#define GST_VIDEOSCALED_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOSCALED_MIXER, GstVideoScaledMixer))
#define GST_VIDEOSCALED_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOSCALED_MIXER, GstVideoScaledMixerClass))
#define GST_IS_VIDEOSCALED_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOSCALED_MIXER))
#define GST_IS_VIDEOSCALED_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOSCALED_MIXER))

typedef struct _GstVideoScaledMixer GstVideoScaledMixer;
typedef struct _GstVideoScaledMixerClass GstVideoScaledMixerClass;

/**
 * GstVideoScaledMixerBackground:
 * @VIDEOSCALED_MIXER_BACKGROUND_CHECKER: checker pattern background
 * @VIDEOSCALED_MIXER_BACKGROUND_BLACK: solid color black background
 * @VIDEOSCALED_MIXER_BACKGROUND_WHITE: solid color white background
 * @VIDEOSCALED_MIXER_BACKGROUND_TRANSPARENT: background is left transparent and layers are composited using "A OVER B" composition rules. This is only applicable to AYUV and ARGB (and variants) as it preserves the alpha channel and allows for further mixing.
 *
 * The different backgrounds videoscaledmixer can blend over.
 */
typedef enum
{
  VIDEOSCALED_MIXER_BACKGROUND_CHECKER,
  VIDEOSCALED_MIXER_BACKGROUND_BLACK,
  VIDEOSCALED_MIXER_BACKGROUND_WHITE,
  VIDEOSCALED_MIXER_BACKGROUND_TRANSPARENT,
}
GstVideoScaledMixerBackground;

/**
 * GstVideoScaledMixer:
 *
 * The opaque #GstVideoScaledMixer structure.
 */
struct _GstVideoScaledMixer
{
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* Lock to prevent the state to change while blending */
  GMutex *state_lock;
  /* Sink pads using Collect Pads from core's base library */
  GstCollectPads *collect;
  /* sinkpads, a GSList of GstVideoScaledMixerPads */
  GSList *sinkpads;

  gint numpads;

  GstClockTime last_ts;
  GstClockTime last_duration;

  /* the master pad */
  GstVideoScaledMixerPad *master;

  GstVideoFormat fmt;

  gint in_width, in_height;
  gint out_width, out_height;
  gboolean setcaps;
  gboolean sendseg;

  GstVideoScaledMixerBackground background;

  gint fps_n;
  gint fps_d;

  gint par_n;
  gint par_d;

  /* Next available sinkpad index */
  gint next_sinkpad;

  /* sink event handling */
  GstPadEventFunction collect_event;
  guint64	segment_position;

  /* Current downstream segment */
  GstSegment    segment;

  /* QoS stuff */
  gdouble proportion;
  GstClockTime earliest_time;

  gboolean flush_stop_pending;
};

struct _GstVideoScaledMixerClass
{
  GstElementClass parent_class;
};

GType gst_video_mixer_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEOSCALED_MIXER_H__ */
