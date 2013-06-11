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
#include "gstvfsrcpad.h"
#include "gstimgsrcpad.h"

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
    GST_STATIC_CAPS (GST_NATIVE_BUFFER_NAME ","
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

static gboolean gst_droid_cam_src_setup_pipeline (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_set_callbacks (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_set_camera_params (GstDroidCamSrc * src);
static void gst_droid_cam_src_tear_down_pipeline (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_probe_camera (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_start_pipeline (GstDroidCamSrc * src);
static void gst_droid_cam_src_stop_pipeline (GstDroidCamSrc * src);

static void gst_droid_cam_src_start_capture (GstDroidCamSrc * src);
static void gst_droid_cam_src_stop_capture (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_start_image_capture_unlocked (GstDroidCamSrc *
    src);

enum
{
  PROP_0,
  PROP_CAMERA_DEVICE,
  PROP_MODE,
  PROP_READY_FOR_CAPTURE,
  N_PROPS,
};

enum
{
  /* action signals */
  START_CAPTURE_SIGNAL,
  STOP_CAPTURE_SIGNAL,
  /* emit signals */
  LAST_SIGNAL
};

static guint droidcamsrc_signals[LAST_SIGNAL];

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
  GstDroidCamSrcClass *droidcamsrc_class = (GstDroidCamSrcClass *) klass;

  droidcamsrc_class->set_camera_params = gst_droid_cam_src_set_camera_params;

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

  g_object_class_install_property (gobject_class, PROP_READY_FOR_CAPTURE,
      g_param_spec_boolean ("ready-for-capture", "Ready for capture",
          "Element is ready for starting another capture",
          TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  droidcamsrc_signals[START_CAPTURE_SIGNAL] =
      g_signal_new_class_handler ("start-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_droid_cam_src_start_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  droidcamsrc_signals[STOP_CAPTURE_SIGNAL] =
      g_signal_new_class_handler ("stop-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_droid_cam_src_stop_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
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
  src->events = NULL;

  src->capturing = FALSE;
  g_mutex_init (&src->capturing_mutex);

  src->image_renegotiate = TRUE;

  src->vfsrc = gst_vf_src_pad_new (&vfsrc_template,
      GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME);
  gst_element_add_pad (GST_ELEMENT (src), src->vfsrc);

  src->imgsrc = gst_img_src_pad_new (&imgsrc_template,
      GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME);
  gst_element_add_pad (GST_ELEMENT (src), src->imgsrc);
}

static void
gst_droid_cam_src_finalize (GObject * object)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (object);

  if (src->events) {
    g_list_foreach (src->events, (GFunc) gst_event_unref, NULL);
    g_list_free (src->events);
    src->events = NULL;
  }

  g_mutex_clear (&src->capturing_mutex);

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

    case PROP_READY_FOR_CAPTURE:
      g_value_set_boolean (value, !src->capturing);
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
gst_droid_cam_src_set_camera_params (GstDroidCamSrc * src)
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
      if (!gst_droid_cam_src_probe_camera (src)) {
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
      src->image_renegotiate = TRUE;
      /* TODO: is this the right thing to do? */
      src->capturing = FALSE;
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_droid_cam_src_tear_down_pipeline (src);

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

        GST_OBJECT_LOCK (src);
        src->events = g_list_append (src->events, event);
        GST_OBJECT_UNLOCK (src);
        event = NULL;

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
      gst_query_set_latency (query, TRUE, src->pool->buffer_duration,
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

static void
gst_droid_cam_src_start_capture (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "start capture");

  g_mutex_lock (&src->capturing_mutex);

  if (src->capturing) {
    GST_WARNING_OBJECT (src, "Capturing already ongoing");

    g_mutex_unlock (&src->capturing_mutex);

    /* notify camerabin2 that the capture failed */
    GST_ELEMENT_WARNING (src, RESOURCE, BUSY, (NULL), (NULL));

    return;
  }

  src->capturing = TRUE;
  g_object_notify (G_OBJECT (src), "ready-for-capture");

  if (src->mode == MODE_IMAGE) {
    if (!gst_droid_cam_src_start_image_capture_unlocked (src)) {
      src->capturing = FALSE;
      g_object_notify (G_OBJECT (src), "ready-for-capture");

      g_mutex_unlock (&src->capturing_mutex);

      /* notify camerabin2 that the capture failed */
      GST_ELEMENT_WARNING (src, RESOURCE, FAILED, (NULL), (NULL));

    } else {
      g_mutex_unlock (&src->capturing_mutex);
    }
  } else if (src->mode == MODE_VIDEO) {
    /* TODO: */
    g_mutex_unlock (&src->capturing_mutex);
  } else {
    g_mutex_unlock (&src->capturing_mutex);
    g_assert_not_reached ();
  }
}

static void
gst_droid_cam_src_stop_capture (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "stop capture");

  if (src->mode == MODE_IMAGE) {
    GST_DEBUG_OBJECT (src, "stop capture not needed for image mode");
    return;
  } else if (src->mode == MODE_VIDEO) {
    /* TODO: video mode */
  } else {
    g_assert_not_reached ();
  }
}

/* */
static gboolean
gst_droid_cam_src_start_image_capture_unlocked (GstDroidCamSrc * src)
{
  int err;

  GST_DEBUG_OBJECT (src, "start image capture unlocked");

  /* Do we need to renegotiate ? */
  if (src->image_renegotiate) {
    if (!gst_img_src_pad_renegotiate (src->imgsrc)) {
      GST_WARNING_OBJECT (src, "Failed to negotiate image capture caps");

      return FALSE;
    }

    src->image_renegotiate = FALSE;
  }

  /* start actual capturing */
  err = src->dev->ops->take_picture (src->dev);

  if (err != 0) {
    GST_WARNING_OBJECT (src, "failed to start image capture: %d", err);
    return FALSE;
  }

  return TRUE;
}
