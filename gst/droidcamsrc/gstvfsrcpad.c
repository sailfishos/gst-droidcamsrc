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

#include "gstvfsrcpad.h"
#include "gstdroidcamsrc.h"
#include "cameraparams.h"
#include <gst/video/video.h>
#include <gst/gstnativebuffer.h>

GST_DEBUG_CATEGORY_STATIC (droidvfsrc_debug);
#define GST_CAT_DEFAULT droidvfsrc_debug

static gboolean gst_droid_cam_src_vfsrc_activatepush (GstPad * pad,
    gboolean active);
static gboolean gst_droid_cam_src_vfsrc_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_droid_cam_src_vfsrc_getcaps (GstPad * pad);
static const GstQueryType *gst_droid_cam_src_vfsrc_query_type (GstPad * pad);
static gboolean gst_droid_cam_src_vfsrc_query (GstPad * pad, GstQuery * query);
static void gst_droid_cam_src_vfsrc_fixatecaps (GstPad * pad, GstCaps * caps);

static void gst_droid_cam_src_vfsrc_loop (gpointer data);
static gboolean gst_droid_cam_src_vfsrc_negotiate (GstDroidCamSrc * src);

GstPad *
gst_vf_src_pad_new (GstStaticPadTemplate * pad_template, const char *name)
{
  GST_DEBUG_CATEGORY_INIT (droidvfsrc_debug, "droidvfsrc", 0,
      "Android camera viewfinder source pad");

  GstPad *pad = gst_pad_new_from_static_template (pad_template, name);

  gst_pad_set_activatepush_function (pad, gst_droid_cam_src_vfsrc_activatepush);
  gst_pad_set_setcaps_function (pad, gst_droid_cam_src_vfsrc_setcaps);
  gst_pad_set_getcaps_function (pad, gst_droid_cam_src_vfsrc_getcaps);
  gst_pad_set_query_type_function (pad, gst_droid_cam_src_vfsrc_query_type);
  gst_pad_set_query_function (pad, gst_droid_cam_src_vfsrc_query);
  gst_pad_set_fixatecaps_function (pad, gst_droid_cam_src_vfsrc_fixatecaps);

  return pad;
}

static gboolean
gst_droid_cam_src_vfsrc_activatepush (GstPad * pad, gboolean active)
{
  GstDroidCamSrc *src;
  GstDroidCamSrcClass *klass;

  src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  GST_DEBUG_OBJECT (src, "vfsrc activatepush: %d", active);

  if (active) {
    gboolean started;
    src->send_new_segment = TRUE;

    /* First we do caps negotiation */
    if (!gst_droid_cam_src_vfsrc_negotiate (src)) {
      return FALSE;
    }

    /* Then we set camera parameters */
    if (!klass->set_camera_params (src)) {
      return FALSE;
    }

    /* Then we start our task */
    GST_PAD_STREAM_LOCK (pad);

    started = gst_pad_start_task (pad, gst_droid_cam_src_vfsrc_loop, pad);
    if (!started) {

      GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
      src->pool->flushing = TRUE;
      GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

      GST_PAD_STREAM_UNLOCK (pad);

      GST_ERROR_OBJECT (src, "Failed to start task");
      gst_pad_stop_task (pad);
      return FALSE;
    }

    GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
    src->pool->flushing = FALSE;
    GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

    GST_PAD_STREAM_UNLOCK (pad);
  } else {
    GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
    src->pool->flushing = TRUE;
    GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

    GST_DEBUG_OBJECT (src, "stopping task");

    gst_camera_buffer_pool_unlock_app_queue (src->pool);

    gst_pad_stop_task (pad);

    GST_DEBUG_OBJECT (src, "stopped task");
  }

  return TRUE;
}

static gboolean
gst_droid_cam_src_vfsrc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  int width, height;
  int fps_n, fps_d;
  int fps;

  GST_DEBUG_OBJECT (src, "vfsrc setcaps %" GST_PTR_FORMAT, caps);

  if (!caps || gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
    /* We are happy. */
    return TRUE;
  }

  if (!gst_video_format_parse_caps (caps, NULL, &width, &height)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("Failed to parse caps"), (NULL));
    return FALSE;
  }

  if (!gst_video_parse_caps_framerate (caps, &fps_n, &fps_d)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("Failed to parse caps framerate"),
        (NULL));
    return FALSE;
  }

  if (width == 0 || height == 0) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("Invalid dimensions"), (NULL));
    return FALSE;
  }

  fps = fps_n / fps_d;
  camera_params_set_viewfinder_size (src->camera_params, width, height);
  camera_params_set_viewfinder_fps (src->camera_params, fps);

  if (klass->set_camera_params (src)) {
    /* buffer pool needs to know about FPS */

    GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
    /* TODO: Make sure we are not overwriting a previous value. */
    src->pool->buffer_duration =
        gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
    src->pool->fps_n = fps_n;
    src->pool->fps_d = fps_d;
    GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

    return TRUE;
  }

  return FALSE;
}

static GstCaps *
gst_droid_cam_src_vfsrc_getcaps (GstPad * pad)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (src, "vfsrc getcaps");

  GST_OBJECT_LOCK (src);

  if (src->camera_params) {
    caps = camera_params_get_viewfinder_caps (src->camera_params);
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  GST_OBJECT_UNLOCK (src);

  GST_LOG_OBJECT (src, "returning %" GST_PTR_FORMAT, caps);

  return caps;
}

static const GstQueryType *
gst_droid_cam_src_vfsrc_query_type (GstPad * pad)
{
  GstElement *parent;
  GstElementClass *parent_class;
  const GstQueryType *queries;

  parent = GST_ELEMENT (gst_pad_get_parent (pad));
  if (!parent) {
    return NULL;
  }

  parent_class = GST_ELEMENT_GET_CLASS (parent);
  queries = parent_class->get_query_types (parent);

  gst_object_unref (parent);

  return queries;
}

static gboolean
gst_droid_cam_src_vfsrc_query (GstPad * pad, GstQuery * query)
{
  GstElement *parent;
  GstElementClass *parent_class;
  gboolean ret;

  parent = GST_ELEMENT (gst_pad_get_parent (pad));
  if (!parent) {
    return FALSE;
  }

  parent_class = GST_ELEMENT_GET_CLASS (parent);
  ret = parent_class->query (parent, query);

  gst_object_unref (parent);

  return ret;
}

static void
gst_droid_cam_src_vfsrc_fixatecaps (GstPad * pad, GstCaps * caps)
{
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

static void
gst_droid_cam_src_vfsrc_loop (gpointer data)
{
  GstPad *pad = (GstPad *) data;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstCameraBufferPool *pool = gst_camera_buffer_pool_ref (src->pool);
  GstNativeBuffer *buff;
  GstFlowReturn ret;
  GList *events = NULL;

  GST_DEBUG_OBJECT (src, "loop");

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  if (pool->flushing) {

    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

    goto pool_flushing;
  }

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  g_mutex_lock (&pool->app_lock);
  if (pool->app_queue->length > 0) {
    buff = g_queue_pop_head (pool->app_queue);

    g_mutex_unlock (&pool->app_lock);

    goto push_buffer;
  }

  GST_DEBUG_OBJECT (src, "empty app queue. waiting for buffer");
  g_cond_wait (&pool->app_cond, &pool->app_lock);

  if (pool->app_queue->length == 0) {
    /* pool is flushing. */
    g_mutex_unlock (&pool->app_lock);

    goto pool_flushing;
  }

  buff = g_queue_pop_head (pool->app_queue);

  g_mutex_unlock (&pool->app_lock);
  goto push_buffer;

pool_flushing:
  /* pause task. */
  gst_camera_buffer_pool_unref (src->pool);

  GST_DEBUG_OBJECT (src, "pool is flushing. pausing task");
  gst_pad_pause_task (pad);
  return;

push_buffer:
  /* push buffer */
  gst_camera_buffer_pool_unref (src->pool);
  if (G_UNLIKELY (src->send_new_segment)) {
    src->send_new_segment = FALSE;
    if (!gst_pad_push_event (src->vfsrc, gst_event_new_new_segment (FALSE, 1.0,
                GST_FORMAT_TIME, 0, -1, 0))) {
      GST_WARNING_OBJECT (src, "failed to push new segment");
    }
  }

  GST_OBJECT_LOCK (src);

  if (src->events) {
    events = src->events;
    src->events = NULL;
  }

  GST_OBJECT_UNLOCK (src);

  while (events) {
    GstEvent *ev = g_list_nth_data (events, 0);
    events = g_list_remove (events, ev);
    GST_DEBUG_OBJECT (src, "pushed event %" GST_PTR_FORMAT, ev);

    gst_pad_push_event (src->vfsrc, ev);
  }

  g_list_free (events);

  ret = gst_pad_push (pad, GST_BUFFER (buff));
  if (ret != GST_FLOW_OK) {
    goto pause;
  }
  return;

pause:
  GST_DEBUG_OBJECT (src, "pausing task. reason: %s", gst_flow_get_name (ret));
  gst_pad_pause_task (pad);

  if (ret == GST_FLOW_UNEXPECTED) {
    /* perform EOS */
    gst_pad_push_event (pad, gst_event_new_eos ());
  } else if (ret == GST_FLOW_NOT_LINKED || ret <= GST_FLOW_UNEXPECTED) {
    GST_ELEMENT_ERROR (src, STREAM, FAILED,
        ("Internal data flow error."),
        ("streaming task paused, reason %s (%d)", gst_flow_get_name (ret),
            ret));

    /* perform EOS */
    gst_pad_push_event (pad, gst_event_new_eos ());
  }

  return;
}

static gboolean
gst_droid_cam_src_vfsrc_negotiate (GstDroidCamSrc * src)
{
  GstCaps *caps;
  GstCaps *peer;
  GstCaps *common;
  gboolean ret;

  GST_DEBUG_OBJECT (src, "vfsrc negotiate");

  caps = gst_droid_cam_src_vfsrc_getcaps (src->vfsrc);
  if (!caps || gst_caps_is_empty (caps)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT,
        ("Failed to get any supported caps"), (NULL));

    if (caps) {
      gst_caps_unref (caps);
    }

    return FALSE;
  }

  GST_LOG_OBJECT (src, "caps %" GST_PTR_FORMAT, caps);

  peer = gst_pad_peer_get_caps_reffed (src->vfsrc);

  if (!peer || gst_caps_is_empty (peer) || gst_caps_is_any (peer)) {
    if (peer) {
      gst_caps_unref (peer);
    }

    gst_caps_unref (caps);

    /* Use default. */
    caps = gst_caps_new_simple (GST_NATIVE_BUFFER_NAME,
        "width", G_TYPE_INT, DEFAULT_VF_WIDTH,
        "height", G_TYPE_INT, DEFAULT_VF_HEIGHT,
        "framerate", GST_TYPE_FRACTION, DEFAULT_VF_FPS, 1, NULL);

    GST_DEBUG_OBJECT (src, "using default caps %" GST_PTR_FORMAT, caps);

    ret = gst_pad_set_caps (src->vfsrc, caps);
    gst_caps_unref (caps);

    return ret;
  }

  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  common = gst_caps_intersect (caps, peer);

  GST_LOG_OBJECT (src, "caps intersection %" GST_PTR_FORMAT, common);

  gst_caps_unref (caps);
  gst_caps_unref (peer);

  if (gst_caps_is_empty (common)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("No common caps"), (NULL));

    gst_caps_unref (common);

    return FALSE;
  }

  if (!gst_caps_is_fixed (common)) {
    gst_pad_fixate_caps (src->vfsrc, common);
  }

  ret = gst_pad_set_caps (src->vfsrc, common);

  gst_caps_unref (common);

  return ret;
}
