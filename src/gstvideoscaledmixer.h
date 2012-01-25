/* Generic video mixer plugin
 * Copyright (C) 2008 Wim Taymans <wim@fluendo.com>
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
 
#ifndef __GST_VIDEOSCALED_MIXER_H__
#define __GST_VIDEOSCALED_MIXER_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "blend.h"
#include "gstcollectpads2.h"

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
 * GstVideoScaledMixer:
 *
 * The opaque #GstVideoScaledMixer structure.
 */
struct _GstVideoScaledMixer
{
  GstElement element;

  /* < private > */

  /* pad */
  GstPad *srcpad;

  /* Lock to prevent the state to change while blending */
  GMutex *lock;
  /* Sink pads using Collect Pads 2*/
  GstCollectPads2 *collect;

  /* sinkpads, a GSList of GstVideoScaledMixerPads */
  GSList *sinkpads;
  gint numpads;
  /* Next available sinkpad index */
  gint next_sinkpad;

  /* Output caps */
  GstVideoFormat format;
  gint width, height;
  gint fps_n;
  gint fps_d;
  gint par_n;
  gint par_d;

  gboolean newseg_pending;
  gboolean flush_stop_pending;

  /* Current downstream segment */
  GstSegment segment;
  GstClockTime ts_offset;
  guint64 nframes;

  /* QoS stuff */
  gdouble proportion;
  GstClockTime earliest_time;
  guint64 qos_processed, qos_dropped;

  BlendFunction blend, overlay;
};

struct _GstVideoScaledMixerClass
{
  GstElementClass parent_class;
};

GType gst_videoscaledmixer_get_type (void);
gboolean gst_videoscaledmixer_register (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_VIDEOSCALED_MIXER_H__ */
