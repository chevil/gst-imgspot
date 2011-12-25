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
 
#ifndef __GST_VIDEOSCALED_MIXER_PAD_H__
#define __GST_VIDEOSCALED_MIXER_PAD_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstcollectpads2.h"

G_BEGIN_DECLS

#define GST_TYPE_VIDEOSCALED_MIXER_PAD (gst_videoscaledmixer_pad_get_type())
#define GST_VIDEOSCALED_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOSCALED_MIXER_PAD, GstVideoScaledMixerPad))
#define GST_VIDEOSCALED_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_MIXER_PAD, GstVideoScaledMixerPadClass))
#define GST_IS_VIDEOSCALED_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOSCALED_MIXER_PAD))
#define GST_IS_VIDEOSCALED_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOSCALED_MIXER_PAD))

typedef struct _GstVideoScaledMixerPad GstVideoScaledMixerPad;
typedef struct _GstVideoScaledMixerPadClass GstVideoScaledMixerPadClass;
typedef struct _GstVideoScaledMixerCollect GstVideoScaledMixerCollect;

/**
 * GstVideoScaledMixerPad:
 *
 * The opaque #GstVideoScaledMixerPad structure.
 */
struct _GstVideoScaledMixerPad
{
  GstPad parent;

  /* < private > */

  /* caps */
  gint width, height;
  gint owidth, oheight; // output width and height
  gint fps_n;
  gint fps_d;

  /* properties */
  gint xpos, ypos;
  guint zorder;
  gdouble alpha;

  GstVideoScaledMixerCollect *mixcol;
};

struct _GstVideoScaledMixerPadClass
{
  GstPadClass parent_class;
};

GType gst_videoscaledmixer_pad_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEOSCALED_MIXER_PAD_H__ */
