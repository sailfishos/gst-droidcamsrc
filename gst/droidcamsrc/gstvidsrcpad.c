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

/* TODO: We have a mess of caps. It should viewfinder and video capture caps should be the same
 * but we don't try to enforce that yet */

GstPad *
gst_vid_src_pad_new (GstStaticPadTemplate * pad_template, const char *name)
{
  // TODO: better location for this
  GST_DEBUG_CATEGORY_INIT (droidvidsrc_debug, "droidvidsrc", 0,
      "Android camera video source pad");

  GstPad *pad = gst_pad_new_from_static_template (pad_template, name);

  gst_pad_set_setcaps_function (pad, gst_droid_cam_src_vidsrc_setcaps);
  gst_pad_set_getcaps_function (pad, gst_droid_cam_src_vidsrc_getcaps);

  gst_pad_set_fixatecaps_function (pad, gst_droid_cam_src_vidsrc_fixatecaps);
  gst_pad_set_activatepush_function (pad,
      gst_droid_cam_src_vidsrc_activatepush);

  gst_pad_set_query_type_function (pad, gst_droid_cam_src_vidsrc_query_type);
  gst_pad_set_query_function (pad, gst_droid_cam_src_vidsrc_query);

  /* TODO: install an event handler via gst_pad_set_event_function() to catch renegotiation. */

  return pad;
}

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
  camera_params_set_video_size (src->camera_params, width, height);
  GST_OBJECT_UNLOCK (src);

  /* TODO: We are not yet setting framerate */
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
    GST_LOG_OBJECT (src, "caps from camera parameters");
    caps = camera_params_get_video_caps (src->camera_params);
  } else {
    GST_LOG_OBJECT (src, "caps from template");
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

  gst_structure_fixate_field_nearest_int (s, "width", DEFAULT_VIDEO_WIDTH);
  gst_structure_fixate_field_nearest_int (s, "height", DEFAULT_VIDEO_HEIGHT);
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

    g_mutex_lock (&src->video_lock);
    src->video_task_running = TRUE;

    started = gst_pad_start_task (pad, gst_droid_cam_src_vidsrc_loop, pad);
    if (!started) {
      src->video_task_running = FALSE;

      g_mutex_unlock (&src->video_lock);

      GST_PAD_STREAM_UNLOCK (pad);

      GST_ERROR_OBJECT (src, "Failed to start task");
      gst_pad_stop_task (pad);

      return FALSE;
    }

    g_mutex_unlock (&src->video_lock);

    GST_PAD_STREAM_UNLOCK (pad);
  } else {
    GST_DEBUG_OBJECT (src, "stopping task");

    g_mutex_lock (&src->video_lock);

    src->video_task_running = FALSE;

    g_cond_signal (&src->video_cond);

    g_mutex_unlock (&src->video_lock);

    gst_pad_stop_task (pad);

    GST_DEBUG_OBJECT (src, "stopped task");
  }

  return TRUE;
}

gboolean
gst_vid_src_pad_renegotiate (GstPad * pad)
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
  GstBuffer *buffer;
  GstFlowReturn ret;
  gboolean stop_recording = FALSE;
  gboolean send_new_segment = FALSE;

  GST_LOG_OBJECT (src, "loop");

  /* TODO: caps renegotiation */
  g_mutex_lock (&src->video_lock);
  if (!src->video_task_running) {
    g_mutex_unlock (&src->video_lock);

    GST_DEBUG_OBJECT (src, "task not running");
    return;
  }

  g_cond_wait (&src->video_cond, &src->video_lock);

  if (!src->video_task_running) {
    g_mutex_unlock (&src->video_lock);

    GST_DEBUG_OBJECT (src, "task not running");
    return;
  }

  g_mutex_lock (&src->video_capture_status_lock);
  GST_LOG_OBJECT (src, "video capture status %d", src->video_capture_status);

  switch (src->video_capture_status) {
    case VIDEO_CAPTURE_ERROR:
      g_assert_not_reached ();
      break;
    case VIDEO_CAPTURE_STOPPED:
      /* This could still happen since main thread will signal us
       * while stopping videos:
       * 1) we saw the status change, performed EOS and stopped recording.
       * 2) we stopped the recording
       * 3) we got the signaled
       */

      GST_DEBUG_OBJECT (src, "video recording has been stopped already");
      g_mutex_unlock (&src->video_capture_status_lock);
      g_mutex_unlock (&src->video_lock);
      return;

    case VIDEO_CAPTURE_STARTING:
      send_new_segment = TRUE;
      break;

    case VIDEO_CAPTURE_RUNNING:
      break;

    case VIDEO_CAPTURE_STOPPING:
      stop_recording = TRUE;
      break;
  }

  g_mutex_unlock (&src->video_capture_status_lock);

  if (stop_recording) {
    goto stop_recording;
  }

  buffer = g_queue_pop_head (src->video_queue);
  g_mutex_unlock (&src->video_lock);

  if (send_new_segment) {
    GST_DEBUG_OBJECT (src, "sending new segment");
    if (!gst_pad_push_event (src->vidsrc, gst_event_new_new_segment (FALSE, 1.0,
                GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buffer), -1, 0))) {
      /* TODO: send an error and stop task? */
      GST_WARNING_OBJECT (src, "failed to push new segment");
    }

    g_mutex_lock (&src->video_capture_status_lock);
    src->video_capture_status = VIDEO_CAPTURE_RUNNING;
    g_mutex_unlock (&src->video_capture_status_lock);
  }

  GST_LOG_OBJECT (src, "pushing buffer %p", buffer);

  ret = gst_pad_push (src->vidsrc, buffer);

  if (ret != GST_FLOW_OK) {
    g_mutex_lock (&src->video_lock);

    g_mutex_lock (&src->video_capture_status_lock);
    src->video_capture_status = VIDEO_CAPTURE_ERROR;
    g_cond_signal (&src->video_capture_status_cond);
    g_mutex_unlock (&src->video_capture_status_lock);

    while (src->video_queue->length > 0) {
      buffer = g_queue_pop_head (src->video_queue);
      GST_DEBUG_OBJECT (src, "dropping buffer %p", buffer);
      gst_buffer_unref (buffer);
    }

    g_mutex_unlock (&src->video_lock);

    gst_pad_pause_task (src->vidsrc);

    GST_ELEMENT_ERROR (src, STREAM, FAILED,
        ("Internal data flow error."),
        ("streaming task paused, reason %s (%d)", gst_flow_get_name (ret),
            ret));

    /* We really need all elements to release the buffers. */
    gst_pad_push_event (src->vidsrc, gst_event_new_flush_start ());
    gst_pad_push_event (src->vidsrc, gst_event_new_flush_stop ());
    gst_pad_push_event (src->vidsrc, gst_event_new_eos ());
  }

  return;

stop_recording:
  GST_DEBUG_OBJECT (src, "stopping video recording");

  GST_DEBUG_OBJECT (src, "performing EOS on video branch");
  if (!gst_pad_push_event (src->vidsrc, gst_event_new_eos ())) {
    GST_WARNING_OBJECT (src, "failed to send EOS to video branch");
  }

  g_mutex_lock (&src->video_capture_status_lock);
  src->video_capture_status = VIDEO_CAPTURE_STOPPED;
  g_cond_signal (&src->video_capture_status_cond);
  g_mutex_unlock (&src->video_capture_status_lock);

  while (src->video_queue->length > 0) {
    buffer = g_queue_pop_head (src->video_queue);
    GST_DEBUG_OBJECT (src, "dropping buffer %p", buffer);
    gst_buffer_unref (buffer);
  }

  g_mutex_unlock (&src->video_lock);

  GST_DEBUG_OBJECT (src, "pushed %d video frames", src->num_video_frames);
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
    caps = gst_caps_new_simple (GST_DROID_CAM_SRC_VIDEO_CAPS_NAME,
        "width", G_TYPE_INT, DEFAULT_VIDEO_WIDTH,
        "height", G_TYPE_INT, DEFAULT_VIDEO_HEIGHT,
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

  GST_DEBUG_OBJECT (parent, "vidsrc query %" GST_PTR_FORMAT, query);

  parent_class = GST_ELEMENT_GET_CLASS (parent);
  ret = parent_class->query (parent, query);

  gst_object_unref (parent);

  return ret;
}
