/*
 * Copyright (C) 2013 Jolla LTD.
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

#include "gstimgsrcpad.h"
#include "gstdroidcamsrc.h"
#include "cameraparams.h"
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (droidimgsrc_debug);
#define GST_CAT_DEFAULT droidimgsrc_debug

static gboolean gst_droid_cam_src_imgsrc_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_droid_cam_src_imgsrc_getcaps (GstPad * pad);
static void gst_droid_cam_src_imgsrc_fixatecaps (GstPad * pad, GstCaps * caps);
static gboolean gst_droid_cam_src_imgsrc_activatepush (GstPad * pad,
    gboolean active);

GstPad *
gst_img_src_pad_new (GstStaticPadTemplate * pad_template, const char *name)
{
  // TODO: better location for this
  GST_DEBUG_CATEGORY_INIT (droidimgsrc_debug, "droidimgsrc", 0,
      "Android camera image source pad");

  GstPad *pad = gst_pad_new_from_static_template (pad_template, name);

  gst_pad_set_setcaps_function (pad, gst_droid_cam_src_imgsrc_setcaps);
  gst_pad_set_getcaps_function (pad, gst_droid_cam_src_imgsrc_getcaps);
  gst_pad_set_fixatecaps_function (pad, gst_droid_cam_src_imgsrc_fixatecaps);
  gst_pad_set_activatepush_function (pad,
      gst_droid_cam_src_imgsrc_activatepush);

  /*
     // TODO: ?
     gst_pad_set_query_type_function (pad, gst_droid_cam_src_imgsrc_query_type);
     gst_pad_set_query_function (pad, gst_droid_cam_src_imgsrc_query);

   */


  return pad;
}

static gboolean
gst_droid_cam_src_imgsrc_setcaps (GstPad * pad, GstCaps * caps)
{
  // TODO:

  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  int width, height;

  GST_DEBUG_OBJECT (src, "imgsrc setcaps %" GST_PTR_FORMAT, caps);

  if (!caps || gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
    /* We are happy. */
    return TRUE;
  }

  gst_video_format_parse_caps (caps, NULL, &width, &height);

  if (width == 0 || height == 0) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("Invalid dimensions"), (NULL));
    return FALSE;
  }

  camera_params_set_capture_size (src->camera_params, width, height);

  return klass->set_camera_params (src);
}

static GstCaps *
gst_droid_cam_src_imgsrc_getcaps (GstPad * pad)
{
  // TODO:

  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (src, "imgsrc getcaps");

  GST_OBJECT_LOCK (src);

  if (src->camera_params) {
    caps = camera_params_get_capture_caps (src->camera_params);
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  GST_OBJECT_UNLOCK (src);

  GST_LOG_OBJECT (src, "returning %" GST_PTR_FORMAT, caps);

  return caps;
}

static void
gst_droid_cam_src_imgsrc_fixatecaps (GstPad * pad, GstCaps * caps)
{
  // TODO:

  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstStructure *s;

  GST_LOG_OBJECT (src, "fixatecaps %" GST_PTR_FORMAT, caps);

  gst_caps_truncate (caps);

  s = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (s, "width", DEFAULT_VF_WIDTH);
  gst_structure_fixate_field_nearest_int (s, "height", DEFAULT_VF_HEIGHT);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", DEFAULT_VF_FPS,
      1);

  GST_DEBUG_OBJECT (src, "caps now is %" GST_PTR_FORMAT, caps);
}

static gboolean
gst_droid_cam_src_imgsrc_activatepush (GstPad * pad, gboolean active)
{
  // TODO:
  return TRUE;
}

gboolean
gst_img_src_pad_renegotiate (GstPad * pad)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (src, "renegotiate");

  /* TODO: */

  return TRUE;
}
