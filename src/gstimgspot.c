/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2008 Michael Sheldon <mike@mikeasoft.com>
 * Copyright (C) 2009 Noam Lewis <jones.noamle@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-imgspot
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=../data/slides.mp4  ! decodebin ! imgspot imgdir=./data/images algorithm=histogram ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstimgspot.h"

GST_DEBUG_CATEGORY_STATIC (gst_imgspot_debug);
#define GST_CAT_DEFAULT gst_imgspot_debug

#define DEFAULT_ALGORITHM "histogram"

enum
{
  PROP_0,
  PROP_ALGORITHM,
  PROP_IMGDIR
};

/* the capabilities of the inputs and outputs.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb")
    );

GST_BOILERPLATE (GstImgSpot, gst_imgspot, GstElement, GST_TYPE_ELEMENT);

static void gst_imgspot_finalize (GObject * object);
static void gst_imgspot_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_imgspot_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_imgspot_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_imgspot_chain (GstPad * pad, GstBuffer * buf);

static void gst_imgspot_load_images (GstImgSpot * filter);
static void gst_imgspot_match (IplImage * input, double *lower_dist);

/* GObject vmethod implementations */

static void
gst_imgspot_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "imgspot",
      "Filter/Analysis/Video",
      "Recognizes pre-loaded images from an incoming video stream",
      "Yves Degoyon for Kognate <ydegoyon@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the imgspot's class */
static void
gst_imgspot_class_init (GstImgSpotClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_imgspot_finalize;
  gobject_class->set_property = gst_imgspot_set_property;
  gobject_class->get_property = gst_imgspot_get_property;

  g_object_class_install_property (gobject_class, PROP_ALGORITHM,
      g_param_spec_string ("algorithm", "Algorithm",
          "Specifies the algorithm used. 'histogram'=HISTOGRAM.",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_IMGDIR,
      g_param_spec_string ("imgdir", "Imgdir", "Directory of images collection.",
          NULL, G_PARAM_READWRITE));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_imgspot_init (GstImgSpot * filter,
    GstImgSpotClass * gclass)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_imgspot_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_imgspot_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->imgdir = NULL;
  filter->algorithm = (char*) malloc( strlen(DEFAULT_ALGORITHM)+1 );;
  strcpy( filter->algorithm, DEFAULT_ALGORITHM );
  gst_imgspot_load_images (filter);
}

static void
gst_imgspot_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImgSpot *filter = GST_IMGSPOT (object);

  switch (prop_id) {
    case PROP_ALGORITHM:
      filter->algorithm = (char *) g_value_get_string (value);
      break;
    case PROP_IMGDIR:
      filter->imgdir = (char *) g_value_get_string (value);
      gst_imgspot_load_images (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_imgspot_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstImgSpot *filter = GST_IMGSPOT (object);

  switch (prop_id) {
    case PROP_ALGORITHM:
      g_value_set_string (value, filter->algorithm);
      break;
    case PROP_IMGDIR:
      g_value_set_string (value, filter->imgdir);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_imgspot_set_caps (GstPad * pad, GstCaps * caps)
{
  GstImgSpot *filter;
  GstPad *otherpad;
  gint width, height;
  GstStructure *structure;

  filter = GST_IMGSPOT (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
  gst_object_unref (filter);

  return gst_pad_set_caps (otherpad, caps);
}

static void
gst_imgspot_finalize (GObject * object)
{
  GstImgSpot *filter;
  filter = GST_IMGSPOT (object);

}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_imgspot_chain (GstPad * pad, GstBuffer * buf)
{
  GstImgSpot *filter;
  CvPoint best_pos;
  double best_res;

  filter = GST_IMGSPOT (GST_OBJECT_PARENT (pad));

  if ((!filter) || (!buf) || filter->imgdir == NULL) {
    return GST_FLOW_OK;
  }

  return gst_pad_push (filter->srcpad, buf);
}

static void
gst_imgspot_match (IplImage * input, double *lower_dist)
{
}

static void
gst_imgspot_load_images (GstImgSpot * filter)
{
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_imgspot_plugin_init (GstPlugin * imgspot)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_imgspot_debug, "imgspot",
      0,
      "Recognizes pre-loaded images from an incoming video stream" );

  return gst_element_register (imgspot, "imgspot", GST_RANK_NONE, GST_TYPE_IMGSPOT);
}
