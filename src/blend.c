/* 
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Mindfruit Bv.
 *   Author: Sjoerd Simons <sjoerd@luon.net>
 *   Author: Alex Ugarte <alexugarte@gmail.com>
 * Copyright (C) 2009 Alex Ugarte <augarte@vicomtech.org>
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "blend.h"
#include "blendorc.h"

#include <string.h>

#include <gst/video/video.h>

#define BLEND(D,S,alpha) (((D) * (256 - (alpha)) + (S) * (alpha)) >> 8)

GST_DEBUG_CATEGORY_STATIC (gst_videomixer_blend_debug);
#define GST_CAT_DEFAULT gst_videomixer_blend_debug

/* Below are the implementations of everything */

/* A32 is for AYUV, ARGB and BGRA */
#define BLEND_A32(name, method, LOOP)		\
static void \
method##_ ##name (const guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  guint s_alpha; \
  gint src_stride, dest_stride; \
  \
  src_stride = src_width * 4; \
  dest_stride = dest_width * 4; \
  \
  s_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
  \
  /* If it's completely transparent... we just return */ \
  if (G_UNLIKELY (s_alpha == 0)) \
    return; \
  \
  /* adjust src pointers for negative sizes */ \
  if (xpos < 0) { \
    src += -xpos * 4; \
    src_width -= -xpos; \
    xpos = 0; \
  } \
  if (ypos < 0) { \
    src += -ypos * src_stride; \
    src_height -= -ypos; \
    ypos = 0; \
  } \
  /* adjust width/height if the src is bigger than dest */ \
  if (xpos + src_width > dest_width) { \
    src_width = dest_width - xpos; \
  } \
  if (ypos + src_height > dest_height) { \
    src_height = dest_height - ypos; \
  } \
  \
  dest = dest + 4 * xpos + (ypos * dest_stride); \
  \
  LOOP (dest, src, src_height, src_width, src_stride, dest_stride, s_alpha); \
}

#define BLEND_A32_LOOP(name, method)			\
static inline void \
_##method##_loop_##name (guint8 * dest, const guint8 * src, gint src_height, \
    gint src_width, gint src_stride, gint dest_stride, guint s_alpha) \
{ \
  s_alpha = MIN (255, s_alpha); \
  orc_##method##_##name (dest, dest_stride, src, src_stride, \
      s_alpha, src_width, src_height); \
}

BLEND_A32_LOOP (argb, blend);
BLEND_A32_LOOP (bgra, blend);
BLEND_A32_LOOP (argb, overlay);
BLEND_A32_LOOP (bgra, overlay);

#if G_BYTE_ORDER == LITTLE_ENDIAN
BLEND_A32 (argb, blend, _blend_loop_argb);
BLEND_A32 (bgra, blend, _blend_loop_bgra);
BLEND_A32 (argb, overlay, _overlay_loop_argb);
BLEND_A32 (bgra, overlay, _overlay_loop_bgra);
#else
BLEND_A32 (argb, blend, _blend_loop_bgra);
BLEND_A32 (bgra, blend, _blend_loop_argb);
BLEND_A32 (argb, overlay, _overlay_loop_bgra);
BLEND_A32 (bgra, overlay, _overlay_loop_argb);
#endif

#define YUV_TO_R(Y,U,V) (CLAMP (1.164 * (Y - 16) + 1.596 * (V - 128), 0, 255))
#define YUV_TO_G(Y,U,V) (CLAMP (1.164 * (Y - 16) - 0.813 * (V - 128) - 0.391 * (U - 128), 0, 255))
#define YUV_TO_B(Y,U,V) (CLAMP (1.164 * (Y - 16) + 2.018 * (U - 128), 0, 255))

/* Y444, Y42B, I420, YV12, Y41B */
#define PLANAR_YUV_BLEND(format_name,format_enum,x_round,y_round,MEMCPY,BLENDLOOP) \
inline static void \
_blend_##format_name (const guint8 * src, guint8 * dest, \
    gint src_stride, gint dest_stride, gint src_width, gint src_height, \
    gdouble src_alpha) \
{ \
  gint i; \
  gint b_alpha; \
  \
  /* If it's completely transparent... we just return */ \
  if (G_UNLIKELY (src_alpha == 0.0)) { \
    GST_INFO ("Fast copy (alpha == 0.0)"); \
    return; \
  } \
  \
  /* If it's completely opaque, we do a fast copy */ \
  if (G_UNLIKELY (src_alpha == 1.0)) { \
    GST_INFO ("Fast copy (alpha == 1.0)"); \
    for (i = 0; i < src_height; i++) { \
      MEMCPY (dest, src, src_width); \
      src += src_stride; \
      dest += dest_stride; \
    } \
    return; \
  } \
  \
  b_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
  \
  BLENDLOOP(dest, dest_stride, src, src_stride, b_alpha, src_width, src_height); \
} \
\
static void \
blend_##format_name (const guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  const guint8 *b_src; \
  guint8 *b_dest; \
  gint b_src_width = src_width; \
  gint b_src_height = src_height; \
  gint xoffset = 0; \
  gint yoffset = 0; \
  gint src_comp_rowstride, dest_comp_rowstride; \
  gint src_comp_height; \
  gint src_comp_width; \
  gint comp_ypos, comp_xpos; \
  gint comp_yoffset, comp_xoffset; \
  \
  xpos = x_round (xpos); \
  ypos = y_round (ypos); \
  \
  /* adjust src pointers for negative sizes */ \
  if (xpos < 0) { \
    xoffset = -xpos; \
    b_src_width -= -xpos; \
    xpos = 0; \
  } \
  if (ypos < 0) { \
    yoffset += -ypos; \
    b_src_height -= -ypos; \
    ypos = 0; \
  } \
  /* If x or y offset are larger then the source it's outside of the picture */ \
  if (xoffset > src_width || yoffset > src_width) { \
    return; \
  } \
  \
  /* adjust width/height if the src is bigger than dest */ \
  if (xpos + src_width > dest_width) { \
    b_src_width = dest_width - xpos; \
  } \
  if (ypos + src_height > dest_height) { \
    b_src_height = dest_height - ypos; \
  } \
  if (b_src_width < 0 || b_src_height < 0) { \
    return; \
  } \
  \
  /* First mix Y, then U, then V */ \
  b_src = src + gst_video_format_get_component_offset (format_enum, 0, src_width, src_height); \
  b_dest = dest + gst_video_format_get_component_offset (format_enum, 0, dest_width, dest_height); \
  src_comp_rowstride = gst_video_format_get_row_stride (format_enum, 0, src_width); \
  dest_comp_rowstride = gst_video_format_get_row_stride (format_enum, 0, dest_width); \
  src_comp_height = gst_video_format_get_component_height (format_enum, 0, b_src_height); \
  src_comp_width = gst_video_format_get_component_width (format_enum, 0, b_src_width); \
  comp_xpos = (xpos == 0) ? 0 : gst_video_format_get_component_width (format_enum, 0, xpos); \
  comp_ypos = (ypos == 0) ? 0 : gst_video_format_get_component_height (format_enum, 0, ypos); \
  comp_xoffset = (xoffset == 0) ? 0 : gst_video_format_get_component_width (format_enum, 0, xoffset); \
  comp_yoffset = (yoffset == 0) ? 0 : gst_video_format_get_component_height (format_enum, 0, yoffset); \
  _blend_##format_name (b_src + comp_xoffset + comp_yoffset * src_comp_rowstride, \
      b_dest + comp_xpos + comp_ypos * dest_comp_rowstride, \
      src_comp_rowstride, \
      dest_comp_rowstride, src_comp_width, src_comp_height, \
      src_alpha); \
  \
  b_src = src + gst_video_format_get_component_offset (format_enum, 1, src_width, src_height); \
  b_dest = dest + gst_video_format_get_component_offset (format_enum, 1, dest_width, dest_height); \
  src_comp_rowstride = gst_video_format_get_row_stride (format_enum, 1, src_width); \
  dest_comp_rowstride = gst_video_format_get_row_stride (format_enum, 1, dest_width); \
  src_comp_height = gst_video_format_get_component_height (format_enum, 1, b_src_height); \
  src_comp_width = gst_video_format_get_component_width (format_enum, 1, b_src_width); \
  comp_xpos = (xpos == 0) ? 0 : gst_video_format_get_component_width (format_enum, 1, xpos); \
  comp_ypos = (ypos == 0) ? 0 : gst_video_format_get_component_height (format_enum, 1, ypos); \
  comp_xoffset = (xoffset == 0) ? 0 : gst_video_format_get_component_width (format_enum, 1, xoffset); \
  comp_yoffset = (yoffset == 0) ? 0 : gst_video_format_get_component_height (format_enum, 1, yoffset); \
  _blend_##format_name (b_src + comp_xoffset + comp_yoffset * src_comp_rowstride, \
      b_dest + comp_xpos + comp_ypos * dest_comp_rowstride, \
      src_comp_rowstride, \
      dest_comp_rowstride, src_comp_width, src_comp_height, \
      src_alpha); \
  \
  b_src = src + gst_video_format_get_component_offset (format_enum, 2, src_width, src_height); \
  b_dest = dest + gst_video_format_get_component_offset (format_enum, 2, dest_width, dest_height); \
  src_comp_rowstride = gst_video_format_get_row_stride (format_enum, 2, src_width); \
  dest_comp_rowstride = gst_video_format_get_row_stride (format_enum, 2, dest_width); \
  src_comp_height = gst_video_format_get_component_height (format_enum, 2, b_src_height); \
  src_comp_width = gst_video_format_get_component_width (format_enum, 2, b_src_width); \
  comp_xpos = (xpos == 0) ? 0 : gst_video_format_get_component_width (format_enum, 2, xpos); \
  comp_ypos = (ypos == 0) ? 0 : gst_video_format_get_component_height (format_enum, 2, ypos); \
  comp_xoffset = (xoffset == 0) ? 0 : gst_video_format_get_component_width (format_enum, 2, xoffset); \
  comp_yoffset = (yoffset == 0) ? 0 : gst_video_format_get_component_height (format_enum, 2, yoffset); \
  _blend_##format_name (b_src + comp_xoffset + comp_yoffset * src_comp_rowstride, \
      b_dest + comp_xpos + comp_ypos * dest_comp_rowstride, \
      src_comp_rowstride, \
      dest_comp_rowstride, src_comp_width, src_comp_height, \
      src_alpha); \
}

#define GST_ROUND_UP_1(x) (x)

PLANAR_YUV_BLEND (i420, GST_VIDEO_FORMAT_I420, GST_ROUND_UP_2,
    GST_ROUND_UP_2, memcpy, orc_blend_u8);
PLANAR_YUV_BLEND (y444, GST_VIDEO_FORMAT_Y444, GST_ROUND_UP_1,
    GST_ROUND_UP_1, memcpy, orc_blend_u8);
PLANAR_YUV_BLEND (y42b, GST_VIDEO_FORMAT_Y42B, GST_ROUND_UP_2,
    GST_ROUND_UP_1, memcpy, orc_blend_u8);
PLANAR_YUV_BLEND (y41b, GST_VIDEO_FORMAT_Y41B, GST_ROUND_UP_4,
    GST_ROUND_UP_1, memcpy, orc_blend_u8);

/* RGB, BGR, xRGB, xBGR, RGBx, BGRx */

#define RGB_BLEND(name, bpp, MEMCPY, BLENDLOOP) \
static void \
blend_##name (const guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  gint b_alpha; \
  gint i; \
  gint src_stride, dest_stride; \
  \
  src_stride = GST_ROUND_UP_4 (src_width * bpp); \
  dest_stride = GST_ROUND_UP_4 (dest_width * bpp); \
  \
  b_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
  \
  /* adjust src pointers for negative sizes */ \
  if (xpos < 0) { \
    src += -xpos * bpp; \
    src_width -= -xpos; \
    xpos = 0; \
  } \
  if (ypos < 0) { \
    src += -ypos * src_stride; \
    src_height -= -ypos; \
    ypos = 0; \
  } \
  /* adjust width/height if the src is bigger than dest */ \
  if (xpos + src_width > dest_width) { \
    src_width = dest_width - xpos; \
  } \
  if (ypos + src_height > dest_height) { \
    src_height = dest_height - ypos; \
  } \
  \
  dest = dest + bpp * xpos + (ypos * dest_stride); \
  /* If it's completely transparent... we just return */ \
  if (G_UNLIKELY (src_alpha == 0.0)) { \
    GST_INFO ("Fast copy (alpha == 0.0)"); \
    return; \
  } \
  \
  /* If it's completely opaque, we do a fast copy */ \
  if (G_UNLIKELY (src_alpha == 1.0)) { \
    GST_INFO ("Fast copy (alpha == 1.0)"); \
    for (i = 0; i < src_height; i++) { \
      MEMCPY (dest, src, bpp * src_width); \
      src += src_stride; \
      dest += dest_stride; \
    } \
    return; \
  } \
  \
  BLENDLOOP(dest, dest_stride, src, src_stride, b_alpha, src_width * bpp, src_height); \
}

#define MEMSET_RGB_C(name, r, g, b) \
static inline void \
_memset_##name##_c (guint8* dest, gint red, gint green, gint blue, gint width) { \
  gint j; \
  \
  for (j = 0; j < width; j++) { \
    dest[r] = red; \
    dest[g] = green; \
    dest[b] = blue; \
    dest += 3; \
  } \
}

#define MEMSET_XRGB(name, r, g, b) \
static inline void \
_memset_##name (guint8* dest, gint red, gint green, gint blue, gint width) { \
  guint32 val; \
  \
  val = GUINT32_FROM_BE ((red << r) | (green << g) | (blue << b)); \
  orc_splat_u32 ((guint32 *) dest, val, width); \
}

#define _orc_memcpy_u32(dest,src,len) orc_memcpy_u32((guint32 *) dest, (const guint32 *) src, len/4)

RGB_BLEND (rgb, 3, memcpy, orc_blend_u8);
MEMSET_RGB_C (rgb, 0, 1, 2);

MEMSET_RGB_C (bgr, 2, 1, 0);

RGB_BLEND (xrgb, 4, _orc_memcpy_u32, orc_blend_u8);
MEMSET_XRGB (xrgb, 24, 16, 0);

MEMSET_XRGB (xbgr, 0, 16, 24);

MEMSET_XRGB (rgbx, 24, 16, 8);

MEMSET_XRGB (bgrx, 8, 16, 24);

/* YUY2, YVYU, UYVY */

#define PACKED_422_BLEND(name, MEMCPY, BLENDLOOP) \
static void \
blend_##name (const guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  gint b_alpha; \
  gint i; \
  gint src_stride, dest_stride; \
  \
  src_stride = GST_ROUND_UP_4 (src_width * 2); \
  dest_stride = GST_ROUND_UP_4 (dest_width * 2); \
  \
  b_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
  \
  xpos = GST_ROUND_UP_2 (xpos); \
  \
  /* adjust src pointers for negative sizes */ \
  if (xpos < 0) { \
    src += -xpos * 2; \
    src_width -= -xpos; \
    xpos = 0; \
  } \
  if (ypos < 0) { \
    src += -ypos * src_stride; \
    src_height -= -ypos; \
    ypos = 0; \
  } \
  \
  /* adjust width/height if the src is bigger than dest */ \
  if (xpos + src_width > dest_width) { \
    src_width = dest_width - xpos; \
  } \
  if (ypos + src_height > dest_height) { \
    src_height = dest_height - ypos; \
  } \
  \
  dest = dest + 2 * xpos + (ypos * dest_stride); \
  /* If it's completely transparent... we just return */ \
  if (G_UNLIKELY (src_alpha == 0.0)) { \
    GST_INFO ("Fast copy (alpha == 0.0)"); \
    return; \
  } \
  \
  /* If it's completely opaque, we do a fast copy */ \
  if (G_UNLIKELY (src_alpha == 1.0)) { \
    GST_INFO ("Fast copy (alpha == 1.0)"); \
    for (i = 0; i < src_height; i++) { \
      MEMCPY (dest, src, 2 * src_width); \
      src += src_stride; \
      dest += dest_stride; \
    } \
    return; \
  } \
  \
  BLENDLOOP(dest, dest_stride, src, src_stride, b_alpha, 2 * src_width, src_height); \
}

PACKED_422_BLEND (yuy2, memcpy, orc_blend_u8);

/* Init function */
BlendFunction gst_video_mixer_blend_argb;
BlendFunction gst_video_mixer_blend_bgra;
BlendFunction gst_video_mixer_overlay_argb;
BlendFunction gst_video_mixer_overlay_bgra;
/* AYUV/ABGR is equal to ARGB, RGBA is equal to BGRA */
BlendFunction gst_video_mixer_blend_y444;
BlendFunction gst_video_mixer_blend_y42b;
BlendFunction gst_video_mixer_blend_i420;
/* I420 is equal to YV12 */
BlendFunction gst_video_mixer_blend_y41b;
BlendFunction gst_video_mixer_blend_rgb;
/* BGR is equal to RGB */
BlendFunction gst_video_mixer_blend_rgbx;
/* BGRx, xRGB, xBGR are equal to RGBx */
BlendFunction gst_video_mixer_blend_yuy2;
/* YVYU and UYVY are equal to YUY2 */

void
gst_video_mixer_init_blend (void)
{
  GST_DEBUG_CATEGORY_INIT (gst_videomixer_blend_debug, "videomixer_blend", 0,
      "video mixer blending functions");

  gst_video_mixer_blend_argb = blend_argb;
  gst_video_mixer_blend_bgra = blend_bgra;
  gst_video_mixer_overlay_argb = overlay_argb;
  gst_video_mixer_overlay_bgra = overlay_bgra;
  gst_video_mixer_blend_i420 = blend_i420;
  gst_video_mixer_blend_y444 = blend_y444;
  gst_video_mixer_blend_y42b = blend_y42b;
  gst_video_mixer_blend_y41b = blend_y41b;
  gst_video_mixer_blend_rgb = blend_rgb;
  gst_video_mixer_blend_xrgb = blend_xrgb;
  gst_video_mixer_blend_yuy2 = blend_yuy2;

}
