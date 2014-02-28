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

#include "gstvidsrcpad.h"
#include "gstdroidcamsrc.h"
#include "cameraparams.h"
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (droidvidsrc_debug);
#define GST_CAT_DEFAULT droidvidsrc_debug

static gboolean gst_droid_cam_src_vidsrc_activatemode (GstPad * pad,
    GstObject *parent, GstPadMode mode, gboolean active);
static gboolean gst_droid_cam_src_vidsrc_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_droid_cam_src_vidsrc_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static void gst_droid_cam_src_vidsrc_loop (gpointer data);

/* TODO: Check any potential events needed by camerabin2 (start capture, finish capture, ...) */

GstPad *
gst_vid_src_pad_new (GstStaticPadTemplate * pad_template, const char *name)
{
  // TODO: better location for this
  GST_DEBUG_CATEGORY_INIT (droidvidsrc_debug, "droidvidsrc", 0,
      "Android camera video source pad");

  GstPad *pad = gst_pad_new_from_static_template (pad_template, name);

  gst_pad_set_activatemode_function (pad, gst_droid_cam_src_vidsrc_activatemode);
  gst_pad_set_event_function (pad, gst_droid_cam_src_vidsrc_event);
  gst_pad_set_query_function (pad, gst_droid_cam_src_vidsrc_query);

  return pad;
}

static GstCaps *
gst_vid_src_pad_get_supported_caps (GstDroidCamSrc *src)
{
  GstCaps *caps;

  GST_OBJECT_LOCK (src);

  if (src->camera_params) {
    caps = camera_params_get_video_caps (src->camera_params);
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (src->vidsrc));
  }

  GST_OBJECT_UNLOCK (src);

  return caps;
}

static gboolean
gst_droid_cam_src_vidsrc_activatemode (GstPad * pad, GstObject *parent,
    GstPadMode mode, gboolean active)
{
  GstDroidCamSrc *src;

  src = GST_DROID_CAM_SRC (parent);

  if (mode != GST_PAD_MODE_PUSH) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "vidsrc activatepush: %d", active);

  if (active) {
    GstEvent *event = gst_pad_get_sticky_event (pad, GST_EVENT_STREAM_START, 0);

    if (event) {
      gst_event_unref (event);
    } else {
      gchar *stream_id = gst_pad_create_stream_id (pad, GST_ELEMENT_CAST(src),
          "video");
      event = gst_event_new_stream_start (stream_id);
      gst_pad_push_event (pad, event);
      g_free (stream_id);
    }

    /* First we do caps negotiation */
    if (!gst_vid_src_pad_renegotiate (src->vidsrc)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean gst_droid_cam_src_vidsrc_event (GstPad * pad,
    GstObject * parent, GstEvent * event)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_RECONFIGURE:
      src->video_renegotiate = TRUE;
      return TRUE;
    default:
      return FALSE;
  }
}

static gboolean
gst_droid_cam_src_vidsrc_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS: {
      GstCaps *caps = gst_vid_src_pad_get_supported_caps (src);

      GST_LOG_OBJECT (src, "queried caps %" GST_PTR_FORMAT, caps);

      gst_query_set_caps_result (query, caps);

      return TRUE;
    }
    default:
      return FALSE;
    }
}

gboolean
gst_vid_src_pad_renegotiate (GstPad * pad)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_PAD_PARENT (pad));
  GstCaps *caps;
  GstCaps *peer;
  gboolean ret = FALSE;
  GstDroidCamSrcClass *klass;
  GstVideoInfo info;
  GstCapsFeatures *features;
  gboolean metadata_buffers = FALSE;


  GST_DEBUG_OBJECT (src, "vidsrc negotiate");

  klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  GST_OBJECT_LOCK (src);
  caps = camera_params_get_video_caps (src->camera_params);
  GST_OBJECT_UNLOCK (src);

  if (!caps || gst_caps_is_empty (caps)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT,
        ("Failed to get any supported caps"), (NULL));

    if (caps) {
      gst_caps_unref (caps);
    }

    ret = FALSE;
    goto out;
  }

  peer = gst_pad_peer_query_caps (src->vidsrc, caps);
  if (peer) {
    gst_caps_unref (caps);
    caps = peer;
  }

  GST_LOG_OBJECT (src, "caps %" GST_PTR_FORMAT, caps);

  if (gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);

    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("No common caps"), (NULL));

    goto out;
  }

  if (!gst_caps_is_fixed (caps)) {
    GstStructure *s;

    caps = gst_caps_truncate (caps);

    s = gst_caps_get_structure (caps, 0);

    gst_structure_fixate_field_nearest_int (s, "width", DEFAULT_VIDEO_WIDTH);
    gst_structure_fixate_field_nearest_int (s, "height", DEFAULT_VIDEO_HEIGHT);
    gst_structure_fixate_field_nearest_fraction (s, "framerate", DEFAULT_FPS, 1);
  }

  GST_DEBUG_OBJECT (src, "resolved caps %" GST_PTR_FORMAT, caps);

  ret = gst_pad_push_event (src->vidsrc, gst_event_new_caps (caps));

  if (ret) {
    ret = gst_video_info_from_caps (&info, caps);

    GST_OBJECT_LOCK (src);
    camera_params_set_resolution (src->camera_params, "video-size",
        info.width, info.height);
    GST_OBJECT_UNLOCK (src);
  } else {
    GST_ERROR_OBJECT (src,
        "Upstream didn't accept caps: %" GST_PTR_FORMAT, caps);
  }

  features = gst_caps_get_features (caps, 0);
  if (features
      && gst_caps_features_contains (features, "memory:AndroidMetadata")) {
    metadata_buffers = TRUE;
  }

  gst_caps_unref (caps);

out:
  if (ret) {
    int err = src->dev->ops->store_meta_data_in_buffers (src->dev,
        metadata_buffers);

    if (err == 0) {
      src->video_renegotiate = FALSE;
      /* set camera parameters */
      ret = klass->set_camera_params (src);
    } else {
      GST_WARNING_OBJECT (src,
          "failed to enable meta data storage in video buffers: %d", err);
    }
  }

  return ret;
}
