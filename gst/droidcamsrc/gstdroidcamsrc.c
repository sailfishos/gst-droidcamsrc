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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include "gstdroidcamsrc.h"
#include "gstcameramemory.h"

#include <hardware/hardware.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#include <gst/basecamerabinsrc/gstbasecamerasrc.h>
#undef GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <stdlib.h>
#include <gst/gstnativebuffer.h>
#include "cameraparams.h"
#include <gst/video/video.h>
#include "enums.h"

#define DEFAULT_WIDTH         640
#define DEFAULT_HEIGHT        480
#define DEFAULT_FPS           30

#define DEFAULT_CAMERA_DEVICE 0
#define DEFAULT_MODE          MODE_IMAGE

GST_DEBUG_CATEGORY_STATIC (droidcam_debug);
#define GST_CAT_DEFAULT droidcam_debug

#define gst_droid_cam_src_debug_init(ignored_parameter)                                      \
  GST_DEBUG_CATEGORY_INIT (droidcam_debug, "droidcam", 0, "Android camera source"); \

GST_BOILERPLATE_FULL (GstDroidCamSrc, gst_droid_cam_src, GstBin,
    GST_TYPE_BIN, gst_droid_cam_src_debug_init);

static GstStaticPadTemplate vfsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-android-buffer, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ] "));

static GstStaticPadTemplate imgsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ] "));

static GstStaticPadTemplate vidsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_droid_cam_src_finalize (GObject * object);
static void gst_droid_cam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_droid_cam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_droid_cam_src_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_droid_cam_src_send_event (GstElement * element,
    GstEvent * event);
static const GstQueryType *gst_droid_cam_src_get_query_types (GstElement *
    element);
static gboolean gst_droid_cam_src_query (GstElement * element,
    GstQuery * query);

static gboolean gst_droid_cam_src_construct_pipeline (GstDroidCamSrc * src);
static void gst_droid_cam_src_destruct_pipeline (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_setup_pipeline (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_set_callbacks (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_set_params (GstDroidCamSrc * src);
static void gst_droid_cam_src_tear_down_pipeline (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_probe_camera (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_start_pipeline (GstDroidCamSrc * src);
static void gst_droid_cam_src_stop_pipeline (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_vfsrc_activatepush (GstPad * pad,
    gboolean active);
static gboolean gst_droid_cam_src_vfsrc_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_droid_cam_src_vfsrc_getcaps (GstPad * pad);
static const GstQueryType *gst_droid_cam_src_vfsrc_query_type (GstPad * pad);
static gboolean gst_droid_cam_src_vfsrc_query (GstPad * pad, GstQuery * query);
static void gst_droid_cam_src_vfsrc_loop (gpointer data);
static gboolean gst_droid_cam_src_vfsrc_negotiate (GstDroidCamSrc * src);

enum
{
  PROP_0,
  PROP_CAMERA_DEVICE,
  PROP_MODE,
  N_PROPS,
};

static void
gst_droid_cam_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "Android camera source",
      "Source/Video/Device",
      "Android camera HAL source",
      "Mohammed Hassan <mohammed.hassan@jollamobile.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vfsrc_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&imgsrc_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vidsrc_template));
}

static void
gst_droid_cam_src_class_init (GstDroidCamSrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_droid_cam_src_finalize;
  gobject_class->get_property = gst_droid_cam_src_get_property;
  gobject_class->set_property = gst_droid_cam_src_set_property;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droid_cam_src_change_state);
  element_class->send_event = GST_DEBUG_FUNCPTR (gst_droid_cam_src_send_event);
  element_class->get_query_types =
      GST_DEBUG_FUNCPTR (gst_droid_cam_src_get_query_types);
  element_class->query = GST_DEBUG_FUNCPTR (gst_droid_cam_src_query);

  g_object_class_install_property (gobject_class, PROP_CAMERA_DEVICE,
      g_param_spec_enum ("camera-device", "Camera device",
          "Defines which camera device should be used",
          GST_TYPE_DROID_CAM_SRC_CAMERA_DEVICE,
          DEFAULT_CAMERA_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "The capture mode (still image capture or video recording)",
          gst_camerabin_mode_get_type (),
          DEFAULT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_droid_cam_src_init (GstDroidCamSrc * src, GstDroidCamSrcClass * gclass)
{
  GST_OBJECT_FLAG_SET (src, GST_ELEMENT_IS_SOURCE);

  src->gralloc = NULL;
  src->cam = NULL;
  src->dev = NULL;
  src->hwmod = NULL;
  src->cam_dev = NULL;
  src->camera_device = DEFAULT_CAMERA_DEVICE;
  src->mode = DEFAULT_MODE;
  src->pool = NULL;
  src->camera_params = NULL;

  src->vfsrc =
      gst_pad_new_from_static_template (&vfsrc_template,
      GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME);

  gst_pad_set_activatepush_function (src->vfsrc,
      gst_droid_cam_src_vfsrc_activatepush);
  gst_pad_set_setcaps_function (src->vfsrc, gst_droid_cam_src_vfsrc_setcaps);
  gst_pad_set_getcaps_function (src->vfsrc, gst_droid_cam_src_vfsrc_getcaps);
  gst_pad_set_query_type_function (src->vfsrc,
      gst_droid_cam_src_vfsrc_query_type);
  gst_pad_set_query_function (src->vfsrc, gst_droid_cam_src_vfsrc_query);

  gst_element_add_pad (GST_ELEMENT (src), src->vfsrc);
}

static void
gst_droid_cam_src_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droid_cam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (object);

  switch (prop_id) {
    case PROP_CAMERA_DEVICE:
      g_value_set_enum (value, src->camera_device);
      break;

    case PROP_MODE:
      g_value_set_enum (value, src->mode);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droid_cam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (object);

  switch (prop_id) {
    case PROP_CAMERA_DEVICE:
      src->camera_device = g_value_get_enum (value);
      break;

    case PROP_MODE:
      src->mode = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
gst_droid_cam_src_construct_pipeline (GstDroidCamSrc * src)
{
  // TODO:

  return TRUE;
}

static void
gst_droid_cam_src_destruct_pipeline (GstDroidCamSrc * src)
{
  // TODO:
}

static gboolean
gst_droid_cam_src_setup_pipeline (GstDroidCamSrc * src)
{
  int err = 0;
  gchar *cam_id = NULL;
  gchar *params = NULL;

  GST_DEBUG_OBJECT (src, "setup pipeline");

  GST_DEBUG_OBJECT (src, "Attempting to open camera %d", src->camera_device);

  cam_id = g_strdup_printf ("%i", src->camera_device);
  err = src->cam->common.methods->open (src->hwmod, cam_id, &src->cam_dev);
  g_free (cam_id);

  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("failed to open camera device %d: %d", src->camera_device, err),
        (NULL));
    goto cleanup;
  }

  src->dev = (camera_device_t *) src->cam_dev;

  params = src->dev->ops->get_parameters (src->dev);
  src->camera_params = camera_params_from_string (params);

  if (src->dev->ops->put_parameters) {
    src->dev->ops->put_parameters (src->dev, params);
  } else {
    free (params);
  }

  if (!gst_droid_cam_src_set_callbacks (src)) {
    goto cleanup;
  }

  return TRUE;

cleanup:
  gst_droid_cam_src_tear_down_pipeline (src);
  return FALSE;
}

static gboolean
gst_droid_cam_src_probe_camera (GstDroidCamSrc * src)
{
  int err;

  GST_DEBUG_OBJECT (src, "probe camera");

  src->gralloc = gst_gralloc_new ();
  if (!src->gralloc) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not initialize gralloc"),
        (NULL));
    goto cleanup;
  }

  src->pool = gst_camera_buffer_pool_new (GST_ELEMENT (src), src->gralloc);

  err =
      hw_get_module (CAMERA_HARDWARE_MODULE_ID,
      (const hw_module_t **) &src->hwmod);
  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not get camera handle: %d",
            err), (NULL));
    goto cleanup;
  }

  if (src->hwmod->module_api_version < HARDWARE_DEVICE_API_VERSION (0, 0)
      || src->hwmod->module_api_version > HARDWARE_DEVICE_API_VERSION (1, 0xFF)) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Unknown camera API version 0x%x",
            src->hwmod->module_api_version), (NULL));
    goto cleanup;
  }

  src->cam = (camera_module_t *) src->hwmod;

  if (src->cam->get_number_of_cameras () != 2) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("number of cameras is not 2"),
        (NULL));
    goto cleanup;
  }

  return TRUE;

cleanup:
  gst_droid_cam_src_tear_down_pipeline (src);
  return FALSE;
}

static void
gst_droid_cam_src_tear_down_pipeline (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "tear down pipeline");

  if (src->dev) {
    src->cam_dev->close (src->cam_dev);
    src->cam_dev = NULL;
  }

  if (src->pool) {
    gst_camera_buffer_pool_unref (src->pool);
    src->pool = NULL;
  }

  if (src->gralloc) {
    gst_gralloc_unref (src->gralloc);
    src->gralloc = NULL;
  }

  if (src->camera_params) {
    camera_params_free (src->camera_params);
    src->camera_params = NULL;
  }

  src->hwmod = NULL;
  src->cam = NULL;
  src->dev = NULL;
}

static gboolean
gst_droid_cam_src_set_callbacks (GstDroidCamSrc * src)
{
  int err;

  GST_DEBUG_OBJECT (src, "set callbacks");

  /* TODO: Complete this when we know what we need */
  src->dev->ops->set_callbacks (src->dev, NULL, // notify_cb
      NULL,                     // data_cb
      NULL,                     // data_cb_timestamp
      gst_camera_memory_get, src);

  err = src->dev->ops->set_preview_window (src->dev, &src->pool->window);

  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not set camera preview window: %d", err), (NULL));
  }

  return TRUE;
}

static gboolean
gst_droid_cam_src_set_params (GstDroidCamSrc * src)
{
  int err;
  gchar *params;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (src, "set params");

  params = camera_params_to_string (src->camera_params);
  err = src->dev->ops->set_parameters (src->dev, params);
  free (params);

  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not set camera parameters: %d", err), (NULL));
    ret = FALSE;
  }

  return ret;
}

static GstStateChangeReturn
gst_droid_cam_src_change_state (GstElement * element, GstStateChange transition)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  gboolean no_preroll = FALSE;

  GST_DEBUG_OBJECT (src, "State change: %s -> %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_droid_cam_src_construct_pipeline (src)
          || !gst_droid_cam_src_probe_camera (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_droid_cam_src_setup_pipeline (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
      } else {
        no_preroll = TRUE;
      }

      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (!gst_droid_cam_src_start_pipeline (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      break;

    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE) {
    goto out;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    /* We cannot exit here because we still need to handle state change below. */

    GST_DEBUG_OBJECT (src, "parent failed state transition");
  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_droid_cam_src_stop_pipeline (src);
      no_preroll = TRUE;
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_droid_cam_src_tear_down_pipeline (src);
      gst_droid_cam_src_destruct_pipeline (src);

      break;

    default:
      break;
  }

  if (no_preroll && ret == GST_STATE_CHANGE_SUCCESS) {
    ret = GST_STATE_CHANGE_NO_PREROLL;
  }

out:
  GST_DEBUG_OBJECT (src, "State_change return: %s",
      gst_element_state_change_return_get_name (ret));
  return ret;
}

static gboolean
gst_droid_cam_src_send_event (GstElement * element, GstEvent * event)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (element);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (src, "send event (%p) %" GST_PTR_FORMAT, event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_NEWSEGMENT:
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    case GST_EVENT_SEEK:
    case GST_EVENT_STEP:
    case GST_EVENT_BUFFERSIZE:
    case GST_EVENT_SINK_MESSAGE:
      break;

    case GST_EVENT_EOS:
      gst_camera_buffer_pool_unlock_app_queue (src->pool);

      GST_PAD_STREAM_LOCK (src->vfsrc);
      gst_pad_pause_task (src->vfsrc);
      GST_PAD_STREAM_UNLOCK (src->vfsrc);

      ret = gst_pad_push_event (src->vfsrc, event);

      GST_DEBUG_OBJECT (src, "pushing event %p", event);

      event = NULL;

      break;

    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_UPSTREAM:
    case GST_EVENT_UNKNOWN:
    case GST_EVENT_TAG:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    case GST_EVENT_CUSTOM_BOTH:
    case GST_EVENT_CUSTOM_BOTH_OOB:
      if (GST_EVENT_IS_SERIALIZED (event)) {
        GST_DEBUG_OBJECT (src, "queueing event %p", event);
        /* TODO: */
      } else {
        ret = gst_pad_push_event (src->vfsrc, event);
        GST_DEBUG_OBJECT (src, "pushing event %p", event);
        event = NULL;
      }

      break;
  }

  if (event) {
    GST_DEBUG_OBJECT (src, "dropping event %p", event);

    gst_event_unref (event);
  }

  return ret;
}

static const GstQueryType *
gst_droid_cam_src_get_query_types (GstElement * element)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_LATENCY,
    0
  };

  return query_types;
}

static gboolean
gst_droid_cam_src_query (GstElement * element, GstQuery * query)
{
  gboolean ret = TRUE;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (element);

  GST_DEBUG_OBJECT (src, "query");

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
      if (!src->dev) {
        GST_WARNING_OBJECT (src, "Can't give latency since device isn't open");
        ret = FALSE;
        break;
      }

      GST_CAMERA_BUFFER_POOL_LOCK (src->pool);

      if (src->pool->buffer_duration == GST_CLOCK_TIME_NONE) {
        GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

        GST_WARNING_OBJECT (src,
            "Can't give latency since frame duration isn't known");
        ret = FALSE;

        break;
      }

      GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);
      gst_query_set_latency (query, TRUE, 0,
          src->pool->count * src->pool->buffer_duration);

      ret = TRUE;
      break;

    default:
      ret = FALSE;
      break;
  }

  return ret;
}

static gboolean
gst_droid_cam_src_start_pipeline (GstDroidCamSrc * src)
{
  int err;

  GST_DEBUG_OBJECT (src, "start pipeline");

  src->dev->ops->enable_msg_type (src->dev, CAMERA_MSG_ALL_MSGS);
  err = src->dev->ops->start_preview (src->dev);
  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not start camera: %d", err),
        (NULL));

    return FALSE;
  }

  return TRUE;
}

static void
gst_droid_cam_src_stop_pipeline (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "stop pipeline");

  src->dev->ops->stop_preview (src->dev);

  /* TODO: Not sure this is correct */
  gst_camera_buffer_pool_unlock_hal_queue (src->pool);
}

static gboolean
gst_droid_cam_src_vfsrc_activatepush (GstPad * pad, gboolean active)
{
  GstDroidCamSrc *src;

  src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));

  GST_DEBUG_OBJECT (src, "vfsrc activatepush: %d", active);

  if (active) {
    gboolean started;
    src->send_new_segment = TRUE;

    /* First we do caps negotiation */
    if (!gst_droid_cam_src_vfsrc_negotiate (src)) {
      return FALSE;
    }

    /* Then we set camera parameters */
    if (!gst_droid_cam_src_set_params (src)) {
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
    caps = gst_static_pad_template_get_caps (&vfsrc_template);
  }

  GST_OBJECT_UNLOCK (src);

  GST_DEBUG_OBJECT (src, "returning %" GST_PTR_FORMAT, caps);

  return caps;
}

static const GstQueryType *
gst_droid_cam_src_vfsrc_query_type (GstPad * pad)
{
  return gst_droid_cam_src_get_query_types (GST_PAD_PARENT (pad));
}

static gboolean
gst_droid_cam_src_vfsrc_query (GstPad * pad, GstQuery * query)
{
  return gst_droid_cam_src_query (GST_PAD_PARENT (pad), query);
}

static gboolean
gst_droid_cam_src_vfsrc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  int width, height;
  int fps_n, fps_d;
  gdouble fps;

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

  if (gst_droid_cam_src_set_params (src)) {
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

  GST_DEBUG_OBJECT (src, "caps %" GST_PTR_FORMAT, caps);

  peer = gst_pad_peer_get_caps_reffed (src->vfsrc);

  if (!peer || gst_caps_is_empty (peer) || gst_caps_is_any (peer)) {
    if (peer) {
      gst_caps_unref (peer);
    }

    gst_caps_unref (caps);

    /* Use default. */
    caps = gst_caps_new_simple ("video/x-android-buffer",
        "width", G_TYPE_INT, DEFAULT_WIDTH,
        "height", G_TYPE_INT, DEFAULT_HEIGHT,
        "framerate", GST_TYPE_FRACTION, DEFAULT_FPS, 1, NULL);

    GST_DEBUG_OBJECT (src, "using default caps %" GST_PTR_FORMAT, caps);

    ret = gst_pad_set_caps (src->vfsrc, caps);
    gst_caps_unref (caps);

    return ret;
  }

  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  common = gst_caps_intersect (caps, peer);

  GST_DEBUG_OBJECT (src, "caps intersection %" GST_PTR_FORMAT, common);

  gst_caps_unref (caps);
  gst_caps_unref (peer);

  if (gst_caps_is_empty (common)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("No common caps"), (NULL));

    gst_caps_unref (common);

    return FALSE;
  }

  if (!gst_caps_is_fixed (common)) {
    /* TODO: set a fixate caps function */
    gst_pad_fixate_caps (src->vfsrc, common);
  }

  ret = gst_pad_set_caps (src->vfsrc, common);

  gst_caps_unref (common);

  return ret;
}

static void
gst_droid_cam_src_vfsrc_loop (gpointer data)
{
  GstPad *pad = (GstPad *) data;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (GST_OBJECT_PARENT (pad));
  GstCameraBufferPool *pool = gst_camera_buffer_pool_ref (src->pool);
  GstNativeBuffer *buff;
  GstFlowReturn ret;

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
