/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstkfilesrc.h:
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

#ifndef __GST_KFILE_SRC_H__
#define __GST_KFILE_SRC_H__

#include <sys/types.h>

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#define GST_TYPE_KFILE_SRC \
  (gst_kfilesrc_get_type())
#define GST_KFILE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_KFILE_SRC,GstKFileSrc))
#define GST_KFILE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_KFILE_SRC,GstKFileSrcClass))
#define GST_IS_KFILE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_KFILE_SRC))
#define GST_IS_KFILE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_KFILE_SRC))
#define GST_KFILE_SRC_CAST(obj) ((GstKFileSrc*) obj)

typedef struct _GstKFileSrc GstKFileSrc;
typedef struct _GstKFileSrcClass GstKFileSrcClass;

/**
 * GstKFileSrc:
 *
 * Opaque #GstKFileSrc structure.
 */
struct _GstKFileSrc {
  GstBaseSrc element;

  /*< private >*/
  guint pagesize;			/* system page size */

  gchar *filename;			/* filename */
  gchar *uri;				/* caching the URI */
  gint fd;				/* open file descriptor */
  guint64 read_position;		/* position of fd */
  guint64 size;		                /* file size */

  gboolean touch;			/* whether to touch every page */
  gboolean using_mmap;                  /* whether we opened it with mmap */
  gboolean sequential;                  /* Whether to madvise (MADV_SEQUENTIAL) 
                                           for mmap pages */
  gboolean seekable;                    /* whether the file is seekable */
  gboolean is_regular;                  /* whether it's a (symlink to a)
                                           regular file */
  GstBuffer *mapbuf;
  size_t mapsize;
  gboolean use_mmap;
};

struct _GstKFileSrcClass {
  GstBaseSrcClass parent_class;
};

GType gst_kfilesrc_get_type (void);

G_END_DECLS

#endif /* __GST_KFILE_SRC_H__ */
