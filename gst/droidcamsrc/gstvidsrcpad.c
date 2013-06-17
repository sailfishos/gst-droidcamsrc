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
#if 0
static gboolean gst_droid_cam_src_vidsrc_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_droid_cam_src_vidsrc_getcaps (GstPad * pad);
static void gst_droid_cam_src_vidsrc_fixatecaps (GstPad * pad, GstCaps * caps);
static gboolean gst_droid_cam_src_vidsrc_activatepush (GstPad * pad,
    gboolean active);
static const GstQueryType *gst_droid_cam_src_vidsrc_query_type (GstPad * pad);
static gboolean gst_droid_cam_src_vidsrc_query (GstPad * pad, GstQuery * query);

static void gst_droid_cam_src_vidsrc_loop (gpointer data);
static gboolean gst_droid_cam_src_vidsrc_negotiate (GstDroidCamSrc * src);

/* TODO: Check any potential events needed by camerabin2 (start capture, finish capture, ...) */
#endif

GstPad *
gst_vid_src_pad_new (GstStaticPadTemplate * pad_template, const char *name)
{
  // TODO: better location for this
  GST_DEBUG_CATEGORY_INIT (droidvidsrc_debug, "droidvidsrc", 0,
      "Android camera video source pad");

  GstPad *pad = gst_pad_new_from_static_template (pad_template, name);
#if 0
  gst_pad_set_setcaps_function (pad, gst_droid_cam_src_vidsrc_setcaps);
  gst_pad_set_getcaps_function (pad, gst_droid_cam_src_vidsrc_getcaps);
  gst_pad_set_fixatecaps_function (pad, gst_droid_cam_src_vidsrc_fixatecaps);
  gst_pad_set_activatepush_function (pad,
      gst_droid_cam_src_vidsrc_activatepush);

  gst_pad_set_query_type_function (pad, gst_droid_cam_src_vidsrc_query_type);
  gst_pad_set_query_function (pad, gst_droid_cam_src_vidsrc_query);

  /* TODO: install an event handler via gst_pad_set_event_function() to catch renegotiation. */

#endif
  return pad;
}

#if 0
static gboolean
gst_droid_cam_src_vidsrc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  int width, height;

  GST_DEBUG_OBJECT (src, "vidsrc setcaps %" GST_PTR_FORMAT, caps);

  if (!caps || gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
    /* We are happy. */
    return TRUE;
  }

  gst_video_format_parse_caps (caps, NULL, &width, &height);

  if (width == 0 || height == 0) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("Invalid dimensions"), (NULL));
    return FALSE;
  }

  GST_OBJECT_LOCK (src);
  camera_params_set_capture_size (src->camera_params, width, height);
  GST_OBJECT_UNLOCK (src);

  return klass->set_camera_params (src);
}

static GstCaps *
gst_droid_cam_src_vidsrc_getcaps (GstPad * pad)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (src, "vidsrc getcaps");

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
gst_droid_cam_src_vidsrc_fixatecaps (GstPad * pad, GstCaps * caps)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstStructure *s;

  GST_LOG_OBJECT (src, "fixatecaps %" GST_PTR_FORMAT, caps);

  gst_caps_truncate (caps);

  s = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (s, "width", DEFAULT_IMG_WIDTH);
  gst_structure_fixate_field_nearest_int (s, "height", DEFAULT_IMG_HEIGHT);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", DEFAULT_FPS, 1);

  GST_DEBUG_OBJECT (src, "caps now is %" GST_PTR_FORMAT, caps);
}

static gboolean
gst_droid_cam_src_vidsrc_activatepush (GstPad * pad, gboolean active)
{
  GstDroidCamSrc *src;

  src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));

  GST_DEBUG_OBJECT (src, "vidsrc activatepush: %d", active);

  if (active) {
    gboolean started;

    /* First we do caps negotiation */
    if (!gst_droid_cam_src_vidsrc_negotiate (src)) {
      return FALSE;
    }

    /* Then we start our task */
    GST_PAD_STREAM_LOCK (pad);

    g_mutex_lock (&src->img_lock);
    src->img_task_running = TRUE;

    started = gst_pad_start_task (pad, gst_droid_cam_src_vidsrc_loop, pad);
    if (!started) {
      src->img_task_running = FALSE;

      g_mutex_unlock (&src->img_lock);

      GST_PAD_STREAM_UNLOCK (pad);

      GST_ERROR_OBJECT (src, "Failed to start task");
      gst_pad_stop_task (pad);

      return FALSE;
    }

    g_mutex_unlock (&src->img_lock);

    GST_PAD_STREAM_UNLOCK (pad);

  } else {
    GST_DEBUG_OBJECT (src, "stopping task");

    g_mutex_lock (&src->img_lock);

    src->img_task_running = FALSE;

    g_cond_signal (&src->img_cond);

    g_mutex_unlock (&src->img_lock);

    gst_pad_stop_task (pad);

    GST_DEBUG_OBJECT (src, "stopped task");
  }

  return TRUE;
}

gboolean
gst_img_src_pad_renegotiate (GstPad * pad)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (src, "renegotiate");

  return gst_droid_cam_src_vidsrc_negotiate (src);
}

static void
gst_droid_cam_src_vidsrc_loop (gpointer data)
{
  GstPad *pad = (GstPad *) data;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);
  GstBuffer *buffer;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (src, "loop");

  g_mutex_lock (&src->img_lock);

  if (src->image_renegotiate) {
    /* TODO: handle renegotiation */
  }

  if (!src->img_task_running) {
    g_mutex_unlock (&src->img_lock);

    GST_DEBUG_OBJECT (src, "task not running");
    return;
  }

  if (src->img_queue->length > 0) {
    buffer = g_queue_pop_head (src->img_queue);
    g_mutex_unlock (&src->img_lock);

    goto push_buffer;
  }

  g_cond_wait (&src->img_cond, &src->img_lock);

  if (!src->img_task_running) {
    g_mutex_unlock (&src->img_lock);

    GST_DEBUG_OBJECT (src, "task not running");
    return;
  }

  buffer = g_queue_pop_head (src->img_queue);
  g_mutex_unlock (&src->img_lock);

push_buffer:
  if (!klass->open_segment (src, src->vidsrc)) {
    GST_WARNING_OBJECT (src, "failed to push new segment");
  }

  klass->update_segment (src, buffer);

  ret = gst_pad_push (src->vidsrc, buffer);

  if (!gst_pad_push_event (src->vidsrc, gst_event_new_eos ())) {
    GST_WARNING_OBJECT (src, "failed to push EOS");
  }

  if (ret == GST_FLOW_UNEXPECTED) {
    /* Nothing */
  } else if (ret == GST_FLOW_NOT_LINKED || ret <= GST_FLOW_UNEXPECTED) {
    GST_ELEMENT_ERROR (src, STREAM, FAILED,
        ("Internal data flow error."),
        ("streaming task paused, reason %s (%d)", gst_flow_get_name (ret),
            ret));
  }
}

static gboolean
gst_droid_cam_src_vidsrc_negotiate (GstDroidCamSrc * src)
{
  GstCaps *caps;
  GstCaps *peer;
  GstCaps *common;
  gboolean ret;
  GstDroidCamSrcClass *klass;

  GST_DEBUG_OBJECT (src, "vidsrc negotiate");

  klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  caps = gst_droid_cam_src_vidsrc_getcaps (src->vidsrc);
  if (!caps || gst_caps_is_empty (caps)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT,
        ("Failed to get any supported caps"), (NULL));

    if (caps) {
      gst_caps_unref (caps);
    }

    ret = FALSE;
    goto out;
  }

  GST_LOG_OBJECT (src, "caps %" GST_PTR_FORMAT, caps);

  peer = gst_pad_peer_get_caps_reffed (src->vidsrc);

  if (!peer || gst_caps_is_empty (peer) || gst_caps_is_any (peer)) {
    if (peer) {
      gst_caps_unref (peer);
    }

    gst_caps_unref (caps);

    /* Use default. */
    caps = gst_caps_new_simple ("image/jpeg",
        "width", G_TYPE_INT, DEFAULT_IMG_WIDTH,
        "height", G_TYPE_INT, DEFAULT_IMG_HEIGHT,
        "framerate", GST_TYPE_FRACTION, DEFAULT_FPS, 1, NULL);

    GST_DEBUG_OBJECT (src, "using default caps %" GST_PTR_FORMAT, caps);

    ret = gst_pad_set_caps (src->vidsrc, caps);
    gst_caps_unref (caps);

    goto out;
  }

  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  common = gst_caps_intersect (caps, peer);

  GST_LOG_OBJECT (src, "caps intersection %" GST_PTR_FORMAT, common);

  gst_caps_unref (caps);
  gst_caps_unref (peer);

  if (gst_caps_is_empty (common)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("No common caps"), (NULL));

    gst_caps_unref (common);

    ret = FALSE;
    goto out;
  }

  if (!gst_caps_is_fixed (common)) {
    gst_pad_fixate_caps (src->vidsrc, common);
  }

  ret = gst_pad_set_caps (src->vidsrc, common);

  gst_caps_unref (common);

out:
  if (ret) {
    /* set camera parameters */
    ret = klass->set_camera_params (src);
  }

  return ret;
}

static const GstQueryType *
gst_droid_cam_src_vidsrc_query_type (GstPad * pad)
{
  GstElement *parent;
  GstElementClass *parent_class;
  const GstQueryType *queries;

  parent = GST_ELEMENT (gst_pad_get_parent (pad));
  if (!parent) {
    return NULL;
  }

  GST_DEBUG_OBJECT (parent, "vidsrc query type");

  parent_class = GST_ELEMENT_GET_CLASS (parent);
  queries = parent_class->get_query_types (parent);

  gst_object_unref (parent);

  return queries;
}

static gboolean
gst_droid_cam_src_vidsrc_query (GstPad * pad, GstQuery * query)
{
  GstElement *parent;
  GstElementClass *parent_class;
  gboolean ret;

  parent = GST_ELEMENT (gst_pad_get_parent (pad));
  if (!parent) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (parent, "vidsrc query");

  parent_class = GST_ELEMENT_GET_CLASS (parent);
  ret = parent_class->query (parent, query);

  gst_object_unref (parent);

  return ret;
}
#endif
