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
 * gst-launch filesrc location=data/slides.mp4  ! decodebin ! ffmpegcolorspace ! imgspot width=320 height=240 algorithm=histogram|surf|match imgdir=../data/images minscore=50|0.90 output=bus ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <gst/gst.h>

#include "gstimgspot.h"

GST_DEBUG_CATEGORY_STATIC (gst_imgspot_debug);
#define GST_CAT_DEFAULT gst_imgspot_debug

#define DEFAULT_ALGORITHM "surf"
#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240 
#define DEFAULT_MINSCORE 0.90 
#define DEFAULT_OUTPUT "console" 

enum
{
  PROP_0,
  PROP_ALGORITHM,
  PROP_IMGDIR,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_MINSCORE,
  PROP_OUTPUT,
  PROP_RESET // not a real property
};

static time_t start_t=0;
static time_t pcurrent_t=0;
static time_t current_t=0;
static int nbframes=0;

// SURF proximity calculations
double
compareSURFDescriptors( const float* d1, const float* d2, double best, int length )
{
    double total_cost = 0;
    double t0, t1, t2, t3;
    int i;

    assert( length % 4 == 0 );

    for( i = 0; i < length; i += 4 )
    {
        t0 = d1[i] - d2[i];
        t1 = d1[i+1] - d2[i+1];
        t2 = d1[i+2] - d2[i+2];
        t3 = d1[i+3] - d2[i+3];
        total_cost += t0*t0 + t1*t1 + t2*t2 + t3*t3;
        if( total_cost > best )
            break;
    }
    return total_cost;
}

int
naiveNearestNeighbor( const float* vec, int laplacian,
                      const CvSeq* model_keypoints,
                      const CvSeq* model_descriptors )
{
    int length = (int)(model_descriptors->elem_size/sizeof(float));
    int i, neighbor = -1;
    double d, dist1 = 1e6, dist2 = 1e6;
    CvSeqReader reader, kreader;

    if ( ( model_keypoints == NULL ) || ( model_descriptors == NULL ) )
    {
       printf( "gstimgspot : FATAL : SURF is NULL!!" );
       return -1;
    }
    cvStartReadSeq( model_keypoints, &kreader, 0 );
    cvStartReadSeq( model_descriptors, &reader, 0 );

    for( i = 0; i < model_descriptors->total; i++ )
    {
        const CvSURFPoint* kp = (const CvSURFPoint*)kreader.ptr;
        const float* mvec = (const float*)reader.ptr;
        CV_NEXT_SEQ_ELEM( kreader.seq->elem_size, kreader );
        CV_NEXT_SEQ_ELEM( reader.seq->elem_size, reader );
        if( laplacian != kp->laplacian )
            continue;
        d = compareSURFDescriptors( vec, mvec, dist2, length );
        if( d < dist1 )
        {
            dist2 = dist1;
            dist1 = d;
            neighbor = i;
        }
        else if ( d < dist2 )
            dist2 = d;
    }
    if ( dist1 < 0.6*dist2 )
        return neighbor;
    return -1;
}

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
          "Specifies the algorithm used.",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_IMGDIR,
      g_param_spec_string ("imgdir", "Imgdir", "Directory of images collection.",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the resized image for comparison.", 32, 1024, 320, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the resized image for comparison.", 32, 1024, 320, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MINSCORE,
      g_param_spec_float ("minscore", "Minscore", "Score to reach to be a true positive test.", 0.1, 1000, 0.1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_OUTPUT,
      g_param_spec_string ("output", "Output", "Output mode : console or bus.", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_RESET,
      g_param_spec_boolean ("reset", "Reset", "Reset number of frames to 0.", FALSE, G_PARAM_READWRITE));
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
  filter->algorithm = (char *) malloc( strlen( DEFAULT_ALGORITHM ) + 1 );
  strcpy( filter->algorithm, DEFAULT_ALGORITHM );
  filter->output = (char *) malloc( strlen( DEFAULT_OUTPUT ) + 1 );
  strcpy( filter->output, DEFAULT_OUTPUT );
  filter->width = DEFAULT_WIDTH;
  filter->height = DEFAULT_HEIGHT;
  filter->minscore = DEFAULT_MINSCORE;
  pthread_mutex_init(&filter->mutex, NULL);

  // data for loaded images
  filter->nb_loaded_images=0;
  filter->loaded_images=NULL;
  filter->loaded_hist=NULL;
  filter->previous = -1;
  filter->keypoints = NULL;
  filter->descriptors = NULL;
  filter->storage = NULL;
}

static void
gst_imgspot_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImgSpot *filter = GST_IMGSPOT (object);
  int nwidth, nheight, reset;
  float nminscore;

  switch (prop_id) {
    case PROP_ALGORITHM:
      if ( ( strcmp( (char *) g_value_get_string (value), "histogram" ) != 0 ) &&
           ( strcmp( (char *) g_value_get_string (value), "surf" ) != 0 ) && 
           ( strcmp( (char *) g_value_get_string (value), "match" ) != 0 ) )
      {
         printf( "gstimgspot : wrong algorithm : %s.\n", (char *) g_value_get_string (value));
         break;
      }
      // printf( "gstimgspot : setting algorithm : %s.\n", (char *) g_value_get_string (value));
      if ( filter->nb_loaded_images > 0 )
      {
         printf( "gstimgspot : reloading images : %s.\n", (char *) g_value_get_string (value));
         pthread_mutex_lock(&filter->mutex);
         filter->previous=-1;
         nbframes=0;
         gst_imgspot_load_images (filter);
         pthread_mutex_unlock(&filter->mutex);
      }
      filter->algorithm = (char *) malloc( strlen ( (char *) g_value_get_string (value) ) + 1 );
      strcpy( filter->algorithm, (char *) g_value_get_string (value) );
      break;
    case PROP_IMGDIR:
      filter->imgdir = (char *) malloc( strlen( (char *) g_value_get_string (value) ) + 1 );
      strcpy( filter->imgdir, (char *) g_value_get_string (value) );
      printf( "gstimgspot : Loading images from : %s.\n", filter->imgdir, filter->algorithm);
      pthread_mutex_lock(&filter->mutex);
      filter->previous=-1;
      nbframes=0;
      gst_imgspot_load_images (filter);
      pthread_mutex_unlock(&filter->mutex);
      break;
    case PROP_WIDTH:
      nwidth = g_value_get_int (value);
      if ( nwidth < 32 )
      {
         printf( "gstimgspot : invalid width : %d.\n", nwidth);
         break;
      }
      filter->width = nwidth;
      break;
    case PROP_HEIGHT:
      nheight = g_value_get_int (value);
      if ( nheight < 32 )
      {
         printf( "gstimgspot : invalid height : %d.\n", nheight);
         break;
      }
      filter->height = nheight;
      break;
    case PROP_MINSCORE:
      nminscore = g_value_get_float (value);
      if ( nminscore < 0.5 )
      {
         printf( "gstimgspot : invalid minimum score : %f.\n", nminscore);
         break;
      }
      filter->minscore = nminscore;
      break;
    case PROP_OUTPUT:
      if ( ( strcmp( (char *) g_value_get_string (value), "console" ) != 0 ) &&
           ( strcmp( (char *) g_value_get_string (value), "bus" ) != 0 ) )
      {
         printf( "gstimgspot : wrong output mode : %s.\n", (char *) g_value_get_string (value));
         break;
      }
      filter->output = (char *) malloc( strlen ( (char *) g_value_get_string (value) ) + 1 );
      strcpy( filter->output, (char *) g_value_get_string (value) );
      break;
    case PROP_RESET:
      reset = (int) g_value_get_boolean (value);
      if ( reset ) nbframes=0;
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
    case PROP_WIDTH:
      g_value_set_int (value, filter->width);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, filter->height);
      break;
    case PROP_MINSCORE:
      g_value_set_float (value, filter->minscore);
      break;
    case PROP_OUTPUT:
      g_value_set_string (value, filter->output);
      break;
    case PROP_RESET:
      g_value_set_boolean (value, FALSE);
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

  filter->incomingImage = cvCreateImage(cvSize (width, height), IPL_DEPTH_8U, 3);
  filter->gray = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 1);

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
  gst_object_unref (filter);

  return gst_pad_set_caps (otherpad, caps);
}

static void
gst_imgspot_finalize (GObject * object)
{
  GstImgSpot *filter;
  filter = GST_IMGSPOT (object);

  pthread_mutex_destroy(&filter->mutex);
  if (filter->incomingImage) {
    cvReleaseImage (&filter->incomingImage);
  }
  if (filter->gray) {
    cvReleaseImage (&filter->gray);
  }
  if ( filter->storage != NULL )
  {
    cvReleaseMemStorage( &filter->storage );
  }
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
  IplImage *cesized_image;
  int i, j, spotted, nbFoundPoints, nbMaxPoints;
  double mdist;

  filter = GST_IMGSPOT (GST_OBJECT_PARENT (pad));

  if ((!filter) || (!buf) || filter->imgdir == NULL || filter->width <= 0 || filter->height <= 0 ) {
    GST_ERROR ("gstimgspot : chain : something is wrong" );
    printf( "gstimgspot : chain : something is wrong\n");
    return GST_FLOW_OK;
  }

  // first frames are not processed bacause some gst plugins might not be initialized
  if ( (int)start_t == 0 ) time( &start_t );
  time( &current_t );
  pcurrent_t=current_t;

  // skip first camera frames
  nbframes++;
  if ( nbframes < 20 ) return GST_FLOW_OK;

  pthread_mutex_lock(&filter->mutex);

  memcpy( filter->incomingImage->imageData, (char *) GST_BUFFER_DATA (buf), (size_t) GST_BUFFER_SIZE (buf) );

  // process the new frame
  cesized_image = cvCreateImage(cvSize(filter->width,filter->height), filter->incomingImage->depth, filter->incomingImage->nChannels);
  // resize the image
  cvResize( filter->incomingImage, cesized_image, CV_INTER_LINEAR );
  if ( ( cesized_image->width <= 0 ) || ( cesized_image->width <= 0 ) ) return GST_FLOW_OK;

  if ( strcmp( filter->algorithm, "histogram" ) == 0 )
  {
     // calculate histogram and compare it
     {
       // defines quantification
       CvHistogram *incoming_hist;
       int s_bins = 25;
       int v_bins = 25;
       double dist;

       int    hist_size[]  = { s_bins, v_bins };
       float  s_ranges[]   = { 0, 255 };         // hue is [0,180]
       float  v_ranges[]   = { 0, 255 };
       float* ranges[]     = { s_ranges, v_ranges };

       incoming_hist = cvCreateHist( 2, hist_size, CV_HIST_ARRAY, ranges, 1);

       IplImage *planes[2]; 
       IplImage *hsv = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 3);
       IplImage *h_plane = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 1);
       IplImage *s_plane = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 1);
       IplImage *v_plane = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 1);
       planes[0] = s_plane;
       planes[1] = v_plane;

       // Convert to hsv
       cvCvtColor( cesized_image, hsv, CV_BGR2HSV );
       cvCvtPixToPlane( hsv, h_plane, s_plane, v_plane, 0 );

       cvCalcHist( planes, incoming_hist, 0, 0 ); //Compute histogram
       cvNormalizeHist( incoming_hist, 1.0 );  //Normalize it 

       // check which closer image is displayed
       spotted = -1;
       mdist = 0.0;
       if ( filter->nb_loaded_images > 0 )
         for (i=0; i<filter->nb_loaded_images; i++) 
         {
            dist = cvCompareHist(incoming_hist, filter->loaded_hist[i], CV_COMP_CORREL );
            if ( dist > mdist ) 
            { 
               spotted = i;
               mdist = dist;
            }
         }

       if ( spotted != -1 && spotted != filter->previous ) 
       {
          if ( strcmp( filter->output, "console") == 0 )
          {
             time_t ptime;
             struct tm *ltime;

             ptime = current_t-start_t;
             ltime = gmtime( &ptime );
             printf( "gstimgspot : histogram : image %s at %.2d:%.2d:%.2d (score=%f)\n", filter->loaded_images[spotted], ltime->tm_hour, ltime->tm_min, ltime->tm_sec, mdist );       
          }

          if ( strcmp( filter->output, "bus") == 0 )
          {
             GstStructure *s = gst_structure_new ("imgspot",
             "algorithm", G_TYPE_STRING, "histogram",
             "spotted", G_TYPE_UINT, spotted,
             "name", G_TYPE_STRING, filter->loaded_images[spotted],
             "score", G_TYPE_UINT, mdist,
             NULL);

             GstMessage *m = gst_message_new_element (GST_OBJECT (filter), s);
             gst_element_post_message (GST_ELEMENT (filter), m);
          }

          filter->previous = spotted;
       }

       cvReleaseImage( &hsv);
       cvReleaseImage( &h_plane );
       cvReleaseImage( &s_plane );
       cvReleaseImage( &v_plane );
     }

  } // algorithm = histogram

  if ( strcmp( filter->algorithm, "surf" ) == 0 )
  {
     CvSeq *imageKeypoints = 0, *imageDescriptors = 0;
     CvSeqReader reader, kreader;

     if ( filter->storage == NULL ) filter->storage = cvCreateMemStorage(1024*1024); // blocks of 1Mb

     cvCvtColor(cesized_image, filter->gray, CV_BGR2GRAY);
     cvExtractSURF( filter->gray, 0, &imageKeypoints, &imageDescriptors, filter->storage, cvSURFParams(500, 1), 0 );

     spotted = -1;
     nbMaxPoints = 0;

     if ( filter->nb_loaded_images > 0 )
       for (i=0; i<filter->nb_loaded_images; i++) 
       {
         nbFoundPoints=0;
         cvStartReadSeq( imageKeypoints, &kreader, 0 );
         cvStartReadSeq( imageDescriptors, &reader, 0 );

         for( j = 0; j < imageDescriptors->total; j++ )
         {
            const CvSURFPoint* kp = (const CvSURFPoint*)kreader.ptr;
            const float* descriptor = (const float*)reader.ptr;
            int nearest_neighbor = naiveNearestNeighbor( descriptor, kp->laplacian, filter->keypoints[i], filter->descriptors[i] );
            if ( nearest_neighbor > 0 )
            { 
               nbFoundPoints++;
            }
            CV_NEXT_SEQ_ELEM( kreader.seq->elem_size, kreader );
            CV_NEXT_SEQ_ELEM( reader.seq->elem_size, reader );
         }
         if ( ( nbFoundPoints > nbMaxPoints ) && ( nbFoundPoints > filter->minscore ) )
         {
             spotted = i;
             nbMaxPoints = nbFoundPoints;
         }
       }

     if ( spotted != -1 && spotted != filter->previous ) 
     {
        if ( strcmp( filter->output, "console") == 0 )
        {
           time_t ptime;
           struct tm *ltime;

           ptime = current_t-start_t;
           ltime = gmtime( &ptime );
           printf( "gstimgspot : surf : image %s  at %.2d:%.2d:%.2d (score=%d)\n", filter->loaded_images[spotted], ltime->tm_hour, ltime->tm_min, ltime->tm_sec, nbMaxPoints );
        }

        if ( strcmp( filter->output, "bus") == 0 )
        {
           GstStructure *s = gst_structure_new ("imgspot",
           "algorithm", G_TYPE_STRING, "surf",
           "spotted", G_TYPE_UINT, spotted,
           "name", G_TYPE_STRING, filter->loaded_images[spotted],
           "score", G_TYPE_UINT, nbMaxPoints,
           NULL);
 
           GstMessage *m = gst_message_new_element (GST_OBJECT (filter), s);
           gst_element_post_message (GST_ELEMENT (filter), m);
        }

        filter->previous = spotted;
     }
                  
  } // algorithm = surf

  // doing a template match on all images
  // it might be slow
  if ( strcmp( filter->algorithm, "match" ) == 0 )
  {
     IplImage *dist_image = cvCreateImage (cvSize (1, 1), IPL_DEPTH_32F, 1);
     double dist_min = 0, dist_max = 0, dist;
     CvPoint min_pos, max_pos;
     int method = CV_TM_SQDIFF_NORMED;

     spotted = -1;
     mdist = 0.0;

     if ( filter->nb_loaded_images > 0 )
       for (i=0; i<filter->nb_loaded_images; i++) 
       {
          cvMatchTemplate (filter->images[i], cesized_image, dist_image, method);
          cvMinMaxLoc (dist_image, &dist_min, &dist_max, &min_pos, &max_pos, NULL);      
          if ((CV_TM_SQDIFF_NORMED == method) || (CV_TM_SQDIFF == method)) {
            dist = dist_min;
            if (CV_TM_SQDIFF_NORMED == method) {
              dist = 1 - dist;
            }
          } else {
            dist = dist_max;
          }

          if (  dist > mdist && dist > filter->minscore )
          {
             spotted = i;
             mdist = dist;
          }
       }

     if ( spotted != -1 && spotted != filter->previous ) 
     {
        if ( strcmp( filter->output, "console") == 0 )
        {
           time_t ptime;
           struct tm *ltime;
           char cmd[1024];

           ptime = current_t-start_t;
           ltime = gmtime( &ptime );
           printf( "gstimgspot : match : image %s  at %.2d:%.2d:%.2d (score=%f)\n", filter->loaded_images[spotted], ltime->tm_hour, ltime->tm_min, ltime->tm_sec, mdist );       
        }

        if ( strcmp( filter->output, "bus") == 0 )
        {
           GstStructure *s = gst_structure_new ("imgspot",
           "algorithm", G_TYPE_STRING, "match",
           "spotted", G_TYPE_UINT, spotted,
           "name", G_TYPE_STRING, filter->loaded_images[spotted],
           "score", G_TYPE_UINT, mdist,
           NULL);

           GstMessage *m = gst_message_new_element (GST_OBJECT (filter), s);
           gst_element_post_message (GST_ELEMENT (filter), m);
        }

        filter->previous = spotted;
     }

     cvReleaseImage( &dist_image );
  } // algorithm = match

  cvReleaseImage( &cesized_image );
  pthread_mutex_unlock(&filter->mutex);

  return gst_pad_push (filter->srcpad, buf);
}

static void
gst_imgspot_load_images (GstImgSpot * filter)
{

  if (filter->imgdir == NULL) {
    GST_ERROR ("imgdir is null" );
    printf( "gstimgspot : ERROR : cannot load images : imgdir is null\n");
    return;
  }

  // scan the directory
  {
     int nentries, i, icount;
     struct dirent** dentries;      /* all directory entries                         */
     IplImage* src_image;
     IplImage* resized_image;
     char imgfullname[1024];

       if ( ( nentries = scandir(filter->imgdir, &dentries, NULL, alphasort ) ) == -1 )
       {
          printf( "gstimgspot : ERROR : cannot scan directory : %s \n", filter->imgdir );
          perror( "scandir" );
          return;
       }

       // free previouly stored data
       if ( filter->loaded_images != NULL )
       {
         for ( i=0; i<filter->nb_loaded_images; i++ )
         {
           free( filter->loaded_images[i] );
           if ( strcmp( filter->algorithm, "histogram" ) == 0 )
                cvReleaseHist(&filter->loaded_hist[i]);
           if ( strcmp( filter->algorithm, "match" ) == 0 )
                cvReleaseImage(&filter->images[i]);
         }
         free( filter->loaded_images );
         filter->loaded_images=NULL;
         if ( strcmp( filter->algorithm, "histogram" ) == 0 )
         {
           free( filter->loaded_hist );
           filter->loaded_hist=NULL;
         }
         if ( strcmp( filter->algorithm, "surf" ) == 0 )
         {
           if ( filter->storage != NULL )
           {
             cvReleaseMemStorage( &filter->storage );
             filter->storage = cvCreateMemStorage(1024*1024); // blocks of 1Mb
           }
           free( filter->keypoints );
           free( filter->descriptors );
           filter->keypoints=NULL;
           filter->descriptors=NULL;
         }
         if ( strcmp( filter->algorithm, "match" ) == 0 )
         {
           free( filter->images );
           filter->images=NULL;
         }
       }

       // calculate the number of images
       filter->nb_loaded_images=0;
       for ( i=0; i<nentries; i++ )
       {
          // ckeck if we found an image
          if ( strcasestr( dentries[i]->d_name, ".gif" ) ||
               strcasestr( dentries[i]->d_name, ".jpg" ) ||
               strcasestr( dentries[i]->d_name, ".jpeg" ) ||
               strcasestr( dentries[i]->d_name, ".png" ) ||
               strcasestr( dentries[i]->d_name, ".tiff" ) ||
               strcasestr( dentries[i]->d_name, ".bmp" )
             )
          {
             filter->nb_loaded_images++;
          }

       }

       // allocate necessary structures
       filter->loaded_images = (char**)malloc(filter->nb_loaded_images*sizeof(char*));
       if ( strcmp( filter->algorithm, "histogram" ) == 0 )
          filter->loaded_hist = (CvHistogram**)malloc(filter->nb_loaded_images*sizeof(CvHistogram*));
       if ( strcmp( filter->algorithm, "surf" ) == 0 )
       {
          filter->keypoints = (CvSeq**)malloc(filter->nb_loaded_images*sizeof(CvSeq*));
          filter->descriptors = (CvSeq**)malloc(filter->nb_loaded_images*sizeof(CvSeq*));
       }
       if ( strcmp( filter->algorithm, "match" ) == 0 )
       {
          filter->images = (IplImage**)malloc(filter->nb_loaded_images*sizeof(IplImage*));
       }

       icount=0;
       for ( i=0; i<nentries; i++ )
       {
          // ckeck if we found an image
          if ( strcasestr( dentries[i]->d_name, ".gif" ) ||
               strcasestr( dentries[i]->d_name, ".jpg" ) ||
               strcasestr( dentries[i]->d_name, ".jpeg" ) ||
               strcasestr( dentries[i]->d_name, ".png" ) ||
               strcasestr( dentries[i]->d_name, ".tiff" ) ||
               strcasestr( dentries[i]->d_name, ".bmp" )
             )
          {
             filter->loaded_images[icount] = (char *)malloc(strlen(dentries[i]->d_name)+1);
             strcpy( filter->loaded_images[icount], dentries[i]->d_name );

             // load and resize image
             sprintf( imgfullname, "%s/%s", filter->imgdir, filter->loaded_images[icount] );
             src_image = cvLoadImage(imgfullname,CV_LOAD_IMAGE_COLOR);

             resized_image = cvCreateImage(cvSize(filter->width,filter->height), src_image->depth, src_image->nChannels);

             // resize the image
             cvResize( src_image, resized_image, CV_INTER_LINEAR );

             if ( strcmp( filter->algorithm, "histogram" ) == 0 )
             {
                // calculate histogram
                {
                   // defines quantification
                   int s_bins = 25;
                   int v_bins = 25;

                   int    hist_size[]  = { s_bins, v_bins };
                   float  s_ranges[]   = { 0, 255 };         // hue is [0,180]
                   float  v_ranges[]   = { 0, 255 };
                   float* ranges[]     = { s_ranges, v_ranges };

                   filter->loaded_hist[icount] = cvCreateHist( 2, hist_size, CV_HIST_ARRAY, ranges, 1);

                   IplImage *planes[2]; 
                   IplImage *hsv = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 3);
                   IplImage *h_plane = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 1);
                   IplImage *s_plane = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 1);
                   IplImage *v_plane = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 1);
                   planes[0] = s_plane;
                   planes[1] = v_plane;

                   // Convert to hsv
                   cvCvtColor( resized_image, hsv, CV_BGR2HSV );
                   cvCvtPixToPlane( hsv, h_plane, s_plane, v_plane, 0 );

                   cvCalcHist( planes, filter->loaded_hist[icount], 0, 0 ); //Compute histogram
                   cvNormalizeHist( filter->loaded_hist[icount], 1.0 );  //Normalize it 

                   cvReleaseImage( &hsv );
                   cvReleaseImage( &h_plane );
                   cvReleaseImage( &s_plane );
                   cvReleaseImage( &v_plane );
                }

                cvReleaseImage( &resized_image );
                cvReleaseImage( &src_image );
             } // algorithm = histogram

             if ( strcmp( filter->algorithm, "surf" ) == 0 )
             {
                IplImage *gray = cvCreateImage(cvSize(filter->width,filter->height), IPL_DEPTH_8U, 1);

                if ( filter->storage == NULL ) filter->storage = cvCreateMemStorage(1024*1024); // blocks of 1Mb

                cvCvtColor(resized_image, gray, CV_BGR2GRAY);
                filter->keypoints[icount]=0;
                filter->descriptors[icount]=0;
                cvExtractSURF( gray, 0, &filter->keypoints[icount], &filter->descriptors[icount], filter->storage, cvSURFParams(500, 1), 0 );

                cvReleaseImage( &gray );
                  
             } // algorithm = surf

             if ( strcmp( filter->algorithm, "match" ) == 0 )
             {
               // not doing much, only stored the resized image
               filter->images[icount] = cvCloneImage( resized_image );
             }

             icount++;
          }

       }
  }

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

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_imgspot_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "imgspot",
    "GStreamer OpenCV Image Recogniton",
    plugin_init, VERSION, "LGPL", "ImgSpot", "http://giss.tv")


