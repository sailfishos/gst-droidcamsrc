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
#include "gstvidsrcpad.h"
#include "gstphotoiface.h"

#define DEFAULT_CAMERA_DEVICE      0
#define DEFAULT_MODE               MODE_IMAGE
#define DEFAULT_VIDEO_METADATA     TRUE
#define DEFAULT_MAX_ZOOM           10.0

GST_DEBUG_CATEGORY_STATIC (droidcam_debug);
#define GST_CAT_DEFAULT droidcam_debug

static GstStaticPadTemplate vfsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_NATIVE_BUFFER_NAME ","
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "orientation-angle = {-1, 0, 90, 180, 270}"));

static GstStaticPadTemplate imgsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ]"));

static GstStaticPadTemplate vidsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_DROID_CAM_SRC_VIDEO_CAPS_NAME ","
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ] "));

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
static gboolean gst_droid_cam_src_open_segment (GstDroidCamSrc * src,
    GstPad * pad);
static void gst_droid_cam_src_update_segment (GstDroidCamSrc * src,
    GstBuffer * buffer);
static void gst_droid_cam_src_tear_down_pipeline (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_probe_camera (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_start_pipeline (GstDroidCamSrc * src);
static void gst_droid_cam_src_stop_pipeline (GstDroidCamSrc * src);

static void gst_droid_cam_src_start_capture (GstDroidCamSrc * src);
static void gst_droid_cam_src_stop_capture (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_flush_buffers (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_start_image_capture_unlocked (GstDroidCamSrc *
    src);
static gboolean gst_droid_cam_src_start_video_capture_unlocked (GstDroidCamSrc *
    src);
static void gst_droid_cam_src_stop_video_capture (GstDroidCamSrc * src);

static void gst_droid_cam_src_data_callback (int32_t msg_type,
    const camera_memory_t * mem, unsigned int index,
    camera_frame_metadata_t * metadata, void *user_data);

static void gst_droid_cam_src_data_timestamp_callback (int64_t timestamp,
    int32_t msg_type, const camera_memory_t * data, unsigned int index,
    void *user);

static void gst_droid_cam_src_notify_callback (int32_t msg_type,
    int32_t ext1, int32_t ext2, void *user);

static void gst_droid_cam_src_handle_compressed_image (GstDroidCamSrc * src,
    const camera_memory_t * mem, unsigned int index,
    camera_frame_metadata_t * metadata);

static gboolean gst_droid_cam_src_finish_capture (GstDroidCamSrc * src);
static void gst_droid_cam_src_update_max_zoom (GstDroidCamSrc * src);

#if 0
static void gst_droid_cam_src_set_recording_hint (GstDroidCamSrc * src,
    gboolean apply);
#endif

static void gst_droid_cam_src_free_video_buffer (gpointer data);
static void gst_droid_cam_src_send_capture_start (GstDroidCamSrc * src);
static void gst_droid_cam_src_send_capture_end (GstDroidCamSrc * src);
static void gst_droid_cam_src_boilerplate_init (GType type);
static void gst_droid_cam_src_send_message (GstDroidCamSrc * src,
    const gchar * msg_name, int status);

GST_BOILERPLATE_FULL (GstDroidCamSrc, gst_droid_cam_src, GstBin,
    GST_TYPE_BIN, gst_droid_cam_src_boilerplate_init);

enum
{
  /* action signals */
  START_CAPTURE_SIGNAL,
  STOP_CAPTURE_SIGNAL,
  /* emit signals */
  LAST_SIGNAL
};

static guint droidcamsrc_signals[LAST_SIGNAL];

typedef struct
{
  GstBuffer *buffer;
  GstDroidCamSrc *src;
} GstDroidCamSrcVideoBufferData;

static void
gst_droid_cam_src_boilerplate_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (droidcam_debug, "droidcam", 0,
      "Android camera source");

  gst_photo_iface_init (type);
}

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
  droidcamsrc_class->open_segment = gst_droid_cam_src_open_segment;
  droidcamsrc_class->update_segment = gst_droid_cam_src_update_segment;

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

  g_object_class_install_property (gobject_class, PROP_VIDEO_METADATA,
      g_param_spec_boolean ("video-metadata", "Video metadata",
          "Set output mode for video data",
          DEFAULT_VIDEO_METADATA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SENSOR_MOUNT_ANGLE,
      g_param_spec_enum ("sensor-mount-angle", "Sensor mount angle",
          "The orientation angle of camera sensor relative to the natural display orientation.",
          GST_TYPE_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE,
          GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_ZOOM,
      g_param_spec_float ("max-zoom", "Maximum zoom level (note: may change "
          "depending on resolution/implementation)",
          "Digital zoom factor", 1.0f, G_MAXFLOAT,
          DEFAULT_MAX_ZOOM, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_photo_iface_add_properties (gobject_class);

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
  src->user_camera_device = DEFAULT_CAMERA_DEVICE;
  src->camera_device = DEFAULT_CAMERA_DEVICE;
  src->mode = DEFAULT_MODE;
  src->video_metadata = DEFAULT_VIDEO_METADATA;
  src->pool = NULL;
  src->camera_params = NULL;
  src->events = NULL;
  src->settings = gst_camera_settings_new ();
  src->max_zoom = DEFAULT_MAX_ZOOM;

  gst_segment_init (&src->segment, GST_FORMAT_TIME);

  src->capturing = FALSE;
  g_mutex_init (&src->capturing_mutex);

  src->image_renegotiate = TRUE;
  src->video_renegotiate = TRUE;

  g_mutex_init (&src->img_lock);
  g_cond_init (&src->img_cond);
  src->img_task_running = FALSE;
  src->img_queue = g_queue_new ();

  src->video_task_running = FALSE;
  g_mutex_init (&src->video_lock);
  g_cond_init (&src->video_cond);
  src->video_queue = g_queue_new ();

  src->pushed_video_frames = 0;
  g_mutex_init (&src->pushed_video_frames_lock);
  g_cond_init (&src->pushed_video_frames_cond);

  src->video_capture_status = VIDEO_CAPTURE_STOPPED;
  g_mutex_init (&src->video_capture_status_lock);
  g_cond_init (&src->video_capture_status_cond);

  src->camera_sensor_orientation[0] =
      GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN;
  src->camera_sensor_orientation[1] =
      GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN;

  gst_photo_iface_init_settings (src);

  src->vfsrc = gst_vf_src_pad_new (&vfsrc_template,
      GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME);
  gst_element_add_pad (GST_ELEMENT (src), src->vfsrc);

  src->imgsrc = gst_img_src_pad_new (&imgsrc_template,
      GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME);
  gst_element_add_pad (GST_ELEMENT (src), src->imgsrc);

  src->vidsrc = gst_vid_src_pad_new (&vidsrc_template,
      GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME);
  gst_element_add_pad (GST_ELEMENT (src), src->vidsrc);
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

  g_mutex_clear (&src->img_lock);
  g_cond_clear (&src->img_cond);

  g_queue_free_full (src->img_queue, (GDestroyNotify) gst_buffer_unref);

  g_mutex_clear (&src->video_lock);
  g_cond_clear (&src->video_cond);
  g_queue_free (src->video_queue);

  g_mutex_clear (&src->pushed_video_frames_lock);
  g_cond_clear (&src->pushed_video_frames_cond);

  g_mutex_clear (&src->video_capture_status_lock);
  g_cond_clear (&src->video_capture_status_cond);

  gst_camera_settings_destroy (src->settings);
  src->settings = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droid_cam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (object);

  if (gst_photo_iface_get_property (src, prop_id, value, pspec)) {
    return;
  }

  switch (prop_id) {
    case PROP_CAMERA_DEVICE:
      g_value_set_enum (value, src->camera_device);
      break;

    case PROP_MODE:
      g_value_set_enum (value, src->mode);
      break;

    case PROP_VIDEO_METADATA:
      g_value_set_boolean (value, src->video_metadata);
      break;

    case PROP_READY_FOR_CAPTURE:
      g_value_set_boolean (value, !src->capturing);
      break;

    case PROP_SENSOR_MOUNT_ANGLE:
      g_value_set_enum (value,
          src->camera_sensor_orientation[src->camera_device]);
      break;

    case PROP_MAX_ZOOM:
      g_value_set_float (value, src->max_zoom);
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

  if (gst_photo_iface_set_property (src, prop_id, value, pspec)) {
    return;
  }

  switch (prop_id) {
    case PROP_CAMERA_DEVICE:
      src->user_camera_device = g_value_get_enum (value);
      if (!src->dev) {
        src->camera_device = src->user_camera_device;
      }

      break;

    case PROP_MODE:
      src->mode = g_value_get_enum (value);
      gst_photo_iface_update_focus_mode (src);

#if 0
      gst_droid_cam_src_set_recording_hint (src, TRUE);
#endif
      break;

    case PROP_VIDEO_METADATA:
      src->video_metadata = g_value_get_boolean (value);
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

  src->camera_device = src->user_camera_device;
  /* We assume camera id 0 is back and 1 is front */
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

  gst_photo_iface_settings_to_params (src);

  if (!gst_droid_cam_src_set_callbacks (src)) {
    goto cleanup;
  }

  GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
  src->pool->orientation = src->camera_sensor_orientation[src->camera_device];
  GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

  return TRUE;

cleanup:
  gst_droid_cam_src_tear_down_pipeline (src);
  return FALSE;
}

static gboolean
gst_droid_cam_src_probe_camera (GstDroidCamSrc * src)
{
  int err;
  struct camera_info info;

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

  /* We assume camera 0 is primary and camera 1 is secondary */
  memset (src->camera_sensor_orientation, -1, 2);
  err = src->cam->get_camera_info (0, &info);
  if (err != 0) {
    GST_WARNING_OBJECT (src, "Error %d getting camera 0 info", err);
  } else {
    src->camera_sensor_orientation[0] = info.orientation;
  }

  err = src->cam->get_camera_info (1, &info);
  if (err != 0) {
    GST_WARNING_OBJECT (src, "Error %d getting camera 1 info", err);
  } else {
    src->camera_sensor_orientation[1] = info.orientation;
  }

  GST_DEBUG_OBJECT (src, "Camera orientation: 0 = %d, 1 = %d",
      src->camera_sensor_orientation[0], src->camera_sensor_orientation[1]);

  g_object_notify (G_OBJECT (src), "sensor-mount-angle");

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

  src->dev->ops->set_callbacks (src->dev, gst_droid_cam_src_notify_callback,
      gst_droid_cam_src_data_callback,
      gst_droid_cam_src_data_timestamp_callback, gst_camera_memory_get, src);

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

  // TODO: locking

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

static gboolean
gst_droid_cam_src_open_segment (GstDroidCamSrc * src, GstPad * pad)
{
  GstEvent *event;

  GST_DEBUG_OBJECT (src, "open segment");

  event = gst_event_new_new_segment_full (FALSE,
      src->segment.rate, src->segment.applied_rate, src->segment.format,
      src->segment.start, src->segment.duration, src->segment.time);

  return gst_pad_push_event (pad, event);
}

static void
gst_droid_cam_src_update_segment (GstDroidCamSrc * src, GstBuffer * buffer)
{
  gint64 position;

  GST_LOG_OBJECT (src, "update segment");

  position = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);

  GST_OBJECT_LOCK (src);
  gst_segment_set_last_stop (&src->segment, src->segment.format, position);
  GST_OBJECT_UNLOCK (src);
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
      g_mutex_lock (&src->capturing_mutex);

      if (src->mode == MODE_VIDEO && src->capturing) {
        GST_WARNING_OBJECT (src, "video capture still running running");
        g_mutex_unlock (&src->capturing_mutex);
        gst_droid_cam_src_stop_video_capture (src);
      } else {
        g_mutex_unlock (&src->capturing_mutex);
      }

      gst_droid_cam_src_stop_pipeline (src);
      no_preroll = TRUE;
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      src->image_renegotiate = TRUE;
      src->video_renegotiate = TRUE;
      /* TODO: is this the right thing to do? */
      /* TODO: do we need locking here? */
      src->capturing = FALSE;
      gst_segment_init (&src->segment, GST_FORMAT_TIME);
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
      GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
      src->pool->flushing = TRUE;
      GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

      gst_camera_buffer_pool_unlock_app_queue (src->pool);

      GST_PAD_STREAM_LOCK (src->vfsrc);
      gst_pad_pause_task (src->vfsrc);
      GST_PAD_STREAM_UNLOCK (src->vfsrc);

      ret = gst_pad_push_event (src->vfsrc, event);

      GST_DEBUG_OBJECT (src, "pushing event %p", event);

      event = NULL;
      ret = TRUE;

      break;

    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_UPSTREAM:
    case GST_EVENT_UNKNOWN:
    case GST_EVENT_TAG:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    case GST_EVENT_CUSTOM_BOTH:
    case GST_EVENT_CUSTOM_BOTH_OOB:
      if (GST_EVENT_IS_UPSTREAM (event) && !GST_EVENT_IS_DOWNSTREAM (event)) {
        /* Drop upstream only events */
        break;
      } else if (GST_EVENT_IS_SERIALIZED (event)) {
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

      ret = TRUE;
      break;
  }

  if (event) {
    GST_DEBUG_OBJECT (src, "dropping event %p", event);

    gst_event_unref (event);
  }

  GST_DEBUG_OBJECT (src, "send event returning %d", ret);
  return ret;
}

static const GstQueryType *
gst_droid_cam_src_get_query_types (GstElement * element)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_LATENCY,
    GST_QUERY_FORMATS,
    0
  };

  GST_DEBUG_OBJECT (element, "query types");

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

      GST_DEBUG_OBJECT (src, "latency query result %" GST_PTR_FORMAT, query);

      ret = TRUE;
      break;

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 1, GST_FORMAT_TIME, NULL);
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
#if 0
  int mode;
#endif

  GST_DEBUG_OBJECT (src, "start pipeline");

  src->dev->ops->enable_msg_type (src->dev, CAMERA_MSG_ALL_MSGS);
  src->dev->ops->disable_msg_type (src->dev, CAMERA_MSG_PREVIEW_FRAME);
  src->dev->ops->disable_msg_type (src->dev, CAMERA_MSG_RAW_IMAGE);
  src->dev->ops->disable_msg_type (src->dev, CAMERA_MSG_RAW_IMAGE_NOTIFY);

#if 0
  mode = src->mode;

  /* We always start in image mode otherwise Android HAL will barf */
  src->mode = MODE_IMAGE;
  gst_droid_cam_src_set_recording_hint (src, FALSE);
#endif

  err = src->dev->ops->start_preview (src->dev);
  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not start camera: %d", err),
        (NULL));

    return FALSE;
  }
#if 0
  if (mode != src->mode) {
    src->mode = mode;
    GST_DEBUG_OBJECT (src, "setting camera mode to %d after starting pipeline",
        mode);
    gst_droid_cam_src_set_recording_hint (src, TRUE);
  }
#endif

  gst_droid_cam_src_update_max_zoom (src);

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
  gboolean started;

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

  switch (src->mode) {
    case MODE_IMAGE:
      started = gst_droid_cam_src_start_image_capture_unlocked (src);
      break;

    case MODE_VIDEO:
      started = gst_droid_cam_src_start_video_capture_unlocked (src);
      break;

    default:
      started = FALSE;
      break;
  }

  if (!started) {
    src->capturing = FALSE;
    g_object_notify (G_OBJECT (src), "ready-for-capture");

    g_mutex_unlock (&src->capturing_mutex);

    /* notify camerabin2 that the capture failed */
    GST_ELEMENT_WARNING (src, RESOURCE, FAILED, (NULL), (NULL));

  } else {
    g_mutex_unlock (&src->capturing_mutex);
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
    g_mutex_lock (&src->capturing_mutex);
    if (!src->capturing) {
      g_mutex_unlock (&src->capturing_mutex);
      GST_WARNING_OBJECT (src, "video capture not running");
      return;
    }

    g_mutex_unlock (&src->capturing_mutex);

    gst_droid_cam_src_stop_video_capture (src);

  } else {
    g_assert_not_reached ();
  }
}

static gboolean
gst_droid_cam_src_flush_buffers (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "flush buffers");

  /* unlock our pad */
  GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
  src->pool->flushing = TRUE;
  GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

  gst_camera_buffer_pool_unlock_app_queue (src->pool);

  /* task has been paused by now */
  GST_PAD_STREAM_LOCK (src->vfsrc);
  GST_PAD_STREAM_UNLOCK (src->vfsrc);

  gst_pad_stop_task (src->vfsrc);

  /* clear any pending events */
  GST_OBJECT_LOCK (src);
  g_list_free_full (src->events, (GDestroyNotify) gst_event_unref);
  src->events = NULL;
  GST_OBJECT_UNLOCK (src);

  gst_camera_buffer_pool_drain_app_queue (src->pool);

  return TRUE;
}

/* with capturing_lock */
static gboolean
gst_droid_cam_src_start_image_capture_unlocked (GstDroidCamSrc * src)
{
  int err;

  GST_DEBUG_OBJECT (src, "start image capture unlocked");

  /* Do we need to renegotiate ? */
  if (src->image_renegotiate) {
    if (!gst_img_src_pad_renegotiate (src->imgsrc)) {
      GST_WARNING_OBJECT (src, "Failed to negotiate image capture caps");
      /* TODO: update max_zoom when we renegotiate */
      return FALSE;
    }

    src->image_renegotiate = FALSE;
  }

  /* First we need to flush the viewfinder branch of the pipeline: */
  if (!gst_droid_cam_src_flush_buffers (src)) {
    return FALSE;
  }

  /* enable shutter message */
  src->dev->ops->enable_msg_type (src->dev, CAMERA_MSG_SHUTTER);

  /* reset those */
  src->capture_start_sent = FALSE;
  src->capture_end_sent = FALSE;

  /* start actual capturing */
  err = src->dev->ops->take_picture (src->dev);

  if (err != 0) {
    GST_WARNING_OBJECT (src, "failed to start image capture: %d", err);
    return FALSE;
  }

  return TRUE;
}

/* with capturing_lock */
static gboolean
gst_droid_cam_src_start_video_capture_unlocked (GstDroidCamSrc * src)
{
  int err;
  gboolean ret;

  GST_DEBUG_OBJECT (src, "start video capture unlocked");

  /* Do we need to renegotiate ? */
  if (src->video_renegotiate) {
    if (!gst_vid_src_pad_renegotiate (src->vidsrc)) {
      GST_WARNING_OBJECT (src, "Failed to negotiate video capture caps");

      return FALSE;
    }

    src->video_renegotiate = FALSE;
  }

  /* First we need to flush the viewfinder branch of the pipeline: */
  if (!gst_droid_cam_src_flush_buffers (src)) {
    return FALSE;
  }

  src->dev->ops->stop_preview (src->dev);
  gst_camera_buffer_pool_clear (src->pool);
  err = src->dev->ops->start_preview (src->dev);
  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not start camera: %d", err),
        (NULL));
    GST_WARNING_OBJECT (src, "failed to start preview: %d", err);

    ret = FALSE;
    goto out;
  }

  GST_DEBUG_OBJECT (src, "setting metadata storage in video buffers to %i",
      src->video_metadata);
  err =
      src->dev->ops->store_meta_data_in_buffers (src->dev, src->video_metadata);
  if (err != 0) {
    GST_WARNING_OBJECT (src,
        "failed to enable meta data storage in video buffers: %d", err);

    ret = FALSE;
    goto out;
  }

  g_mutex_lock (&src->pushed_video_frames_lock);
  src->pushed_video_frames = 0;
  g_mutex_unlock (&src->pushed_video_frames_lock);
  src->num_video_frames = 0;

  g_mutex_lock (&src->video_capture_status_lock);
  src->video_capture_status = VIDEO_CAPTURE_STARTING;
  g_mutex_unlock (&src->video_capture_status_lock);

  err = src->dev->ops->start_recording (src->dev);
  if (err != 0) {
    GST_WARNING_OBJECT (src, "failed to start video recording: %d");

    ret = FALSE;
    goto out;
  }

  /* We need to reset focus mode because default for video recording is continuous-video */
  gst_photo_iface_update_focus_mode (src);

  GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
  src->pool->flushing = FALSE;
  GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

  if (!gst_vf_src_pad_start_task (src->vfsrc)) {
    GST_ERROR_OBJECT (src, "Failed to start viewfinder task");
    GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
    src->pool->flushing = TRUE;
    GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "done");

  return TRUE;

out:
  GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
  src->pool->flushing = TRUE;
  GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

  if (!gst_vf_src_pad_start_task (src->vfsrc)) {
    GST_ERROR_OBJECT (src, "Failed to start viewfinder task");
  }

  return ret;
}

static void
gst_droid_cam_src_stop_video_capture (GstDroidCamSrc * src)
{
  /*
   * We cannot simply stop recording and process all the remaining frames.
   * Android HAL will free all the memory handed to us upon stopping
   * recording.
   * The only reasonable way to stop recording is to make sure no frames are still
   * held by any elements and then only we can stop recording.
   */

  GST_DEBUG_OBJECT (src, "stop video capture");

  /*
   * First we tell gst_droid_cam_src_data_timestamp_callback() and
   * our vidsrc pad loop function that we are stopping video recording.
   */

  g_mutex_lock (&src->video_capture_status_lock);
  src->video_capture_status = VIDEO_CAPTURE_STOPPING;

  /*
   * We are discarding all new buffers and the queue might be empty.
   * We will signal our vidsrc loop thread.
   */

  g_mutex_lock (&src->video_lock);
  g_cond_signal (&src->video_cond);
  g_mutex_unlock (&src->video_lock);

  g_cond_wait (&src->video_capture_status_cond,
      &src->video_capture_status_lock);

  GST_LOG_OBJECT (src, "video_capture_status is now %i",
      src->video_capture_status);

  g_assert (src->video_capture_status == VIDEO_CAPTURE_STOPPED
      || src->video_capture_status == VIDEO_CAPTURE_ERROR);

  g_mutex_unlock (&src->video_capture_status_lock);

  /* Make sure the video queue is empty */
  g_mutex_lock (&src->video_lock);
  while (src->video_queue->length > 0) {
    gst_buffer_unref (g_queue_pop_head (src->video_queue));
  }

  g_mutex_unlock (&src->video_lock);

  /* Make sure all buffers have been returned to us */
  g_mutex_lock (&src->pushed_video_frames_lock);

  while (src->pushed_video_frames > 0) {
    GST_DEBUG_OBJECT (src, "Waiting for pushed_video_frames to reach 0 from %i",
        src->pushed_video_frames);
    g_cond_wait (&src->pushed_video_frames_cond,
        &src->pushed_video_frames_lock);
  }

  g_mutex_unlock (&src->pushed_video_frames_lock);

  /* Now we really stop. */
  src->dev->ops->stop_recording (src->dev);

  GST_LOG_OBJECT (src, "HAL stopped recording");

  /* And finally: */
  g_mutex_lock (&src->capturing_mutex);

  src->capturing = FALSE;
  g_object_notify (G_OBJECT (src), "ready-for-capture");

  g_mutex_unlock (&src->capturing_mutex);
}

static void
gst_droid_cam_src_handle_compressed_image (GstDroidCamSrc * src,
    const camera_memory_t * mem, unsigned int index,
    camera_frame_metadata_t * metadata)
{
  GstBuffer *buffer;
  void *data;
  int size;
  GstCaps *caps;
  GstClock *clock;
  GstClockTime timestamp;

  GST_DEBUG_OBJECT (src, "handle compressed image");

  data = gst_camera_memory_get_data (mem, index, &size);

  if (!data) {
    GST_ELEMENT_ERROR (src, LIBRARY, ENCODE, ("Empty data from camera HAL"),
        (NULL));
    goto stop;
  }

  buffer = gst_buffer_new_and_alloc (size);
  caps = gst_pad_get_negotiated_caps (src->imgsrc);
  if (!caps) {
    GST_WARNING_OBJECT (src, "No negotiated caps on imgsrc pad");
  } else {
    GST_LOG_OBJECT (src, "setting caps to %" GST_PTR_FORMAT, caps);
    gst_buffer_set_caps (buffer, caps);
    gst_caps_unref (caps);
  }

  memcpy (GST_BUFFER_DATA (buffer), data, size);
  GST_BUFFER_SIZE (buffer) = size;

  GST_OBJECT_LOCK (src);
  clock = GST_ELEMENT_CLOCK (src);

  if (clock) {
    timestamp = gst_clock_get_time (clock) - GST_ELEMENT (src)->base_time;
  } else {
    timestamp = GST_CLOCK_TIME_NONE;
  }

  GST_BUFFER_TIMESTAMP (buffer) = timestamp;

  GST_LOG_OBJECT (src, "buffer timestamp set to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  g_mutex_lock (&src->img_lock);

  g_queue_push_tail (src->img_queue, buffer);
  g_cond_signal (&src->img_cond);

  g_mutex_unlock (&src->img_lock);

  GST_OBJECT_UNLOCK (src);
  goto invoke_finish;

stop:
  g_mutex_lock (&src->img_lock);

  src->img_task_running = FALSE;
  g_cond_signal (&src->img_cond);

  g_mutex_unlock (&src->img_lock);

  gst_pad_stop_task (src->imgsrc);

  GST_DEBUG_OBJECT (src, "stopped imgsrc task");

invoke_finish:
  /*
   * TODO: If we ever discover that we cannot invoke gst_droid_cam_src_finish_capture()
   * from the data callback then we can use that.
   */
#if 0
  g_timeout_add_full (G_PRIORITY_HIGH, 0,
      (GSourceFunc) gst_droid_cam_src_finish_capture, src, NULL);
#endif
  gst_droid_cam_src_finish_capture (src);
}

static gboolean
gst_droid_cam_src_finish_capture (GstDroidCamSrc * src)
{
  int err;
  gboolean started;

  GST_DEBUG_OBJECT (src, "finish capture");

  /* Here we can safely get rid of any buffers held by the app
   * because hal has returned back all the buffers */
  gst_camera_buffer_pool_clear (src->pool);

  err = src->dev->ops->start_preview (src->dev);

  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not start camera: %d", err),
        (NULL));
    goto out;
  }

  GST_DEBUG_OBJECT (src, "finish capture done");

  GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
  src->pool->flushing = FALSE;
  GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

  started = gst_vf_src_pad_start_task (src->vfsrc);

  if (!started) {
    GST_ERROR_OBJECT (src, "Failed to start task");
    GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
    src->pool->flushing = TRUE;
    GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);
  }

out:
  g_mutex_lock (&src->capturing_mutex);

  src->capturing = FALSE;
  g_object_notify (G_OBJECT (src), "ready-for-capture");

  g_mutex_unlock (&src->capturing_mutex);

  return FALSE;                 /* Don't call us again */
}

static void
gst_droid_cam_src_data_callback (int32_t msg_type, const camera_memory_t * mem,
    unsigned int index, camera_frame_metadata_t * metadata, void *user_data)
{
  GstDroidCamSrc *src = (GstDroidCamSrc *) user_data;

  GST_DEBUG_OBJECT (src, "data callback");
  switch (msg_type) {
    case CAMERA_MSG_COMPRESSED_IMAGE:
      gst_droid_cam_src_send_capture_end (src);
      gst_droid_cam_src_handle_compressed_image (src, mem, index, metadata);
      break;

    default:
      GST_WARNING_OBJECT (src, "unknown message 0x%x from HAL", msg_type);
      break;
  }
}

static void
gst_droid_cam_src_data_timestamp_callback (int64_t timestamp,
    int32_t msg_type, const camera_memory_t * data,
    unsigned int index, void *user)
{
  GstDroidCamSrc *src;
  void *video_data;
  int size;
  GstBuffer *buff;
  GstClock *clock;
  GstClockTime ts;
  GstClockTime duration;
  GstCaps *caps;
  GstDroidCamSrcVideoBufferData *malloc_data;
  gboolean drop_buffer = FALSE;

  src = (GstDroidCamSrc *) user;

  GST_DEBUG_OBJECT (src, "data timestamp callback");

  video_data = gst_camera_memory_get_data (data, index, &size);
  /* TODO: this is bad for production */
  g_assert (video_data != NULL);

  GST_LOG_OBJECT (src, "received video data %p of size %i", video_data, size);

  g_mutex_lock (&src->video_capture_status_lock);
  if (src->video_capture_status > VIDEO_CAPTURE_RUNNING
      || src->video_capture_status == VIDEO_CAPTURE_ERROR) {
    drop_buffer = TRUE;
  }

  g_mutex_unlock (&src->video_capture_status_lock);

  if (drop_buffer) {
    GST_DEBUG_OBJECT (src, "video recording is stopping. Dropping buffer %p",
        video_data);
    src->dev->ops->release_recording_frame (src->dev, video_data);
    return;
  }

  /* TODO: We will ignore the provided timestamp for now */

  g_mutex_lock (&src->pushed_video_frames_lock);
  ++src->pushed_video_frames;
  GST_LOG_OBJECT (src, "pushed video frames is now %i",
      src->pushed_video_frames);
  g_mutex_unlock (&src->pushed_video_frames_lock);

  buff = gst_buffer_new ();

  GST_BUFFER_OFFSET (buff) = src->num_video_frames++;
  GST_BUFFER_OFFSET_END (buff) = src->num_video_frames;

  GST_CAMERA_BUFFER_POOL_LOCK (src->pool);
  duration = src->pool->buffer_duration;
  GST_CAMERA_BUFFER_POOL_UNLOCK (src->pool);

  GST_OBJECT_LOCK (src);
  clock = GST_ELEMENT_CLOCK (src);

  if (clock) {
    ts = gst_clock_get_time (clock) - GST_ELEMENT (src)->base_time;
  } else {
    ts = GST_CLOCK_TIME_NONE;
  }

  GST_OBJECT_UNLOCK (src);

  GST_BUFFER_DURATION (buff) = duration;

  if (ts > duration) {
    ts -= duration;
  }

  GST_BUFFER_TIMESTAMP (buff) = ts;
  GST_BUFFER_SIZE (buff) = size;

  malloc_data = g_slice_new (GstDroidCamSrcVideoBufferData);
  malloc_data->buffer = buff;
  malloc_data->src = src;

  caps = gst_pad_get_negotiated_caps (src->vidsrc);
  gst_buffer_set_caps (buff, caps);
  gst_caps_unref (caps);
  GST_BUFFER_DATA (buff) = video_data;
  GST_BUFFER_MALLOCDATA (buff) = (gpointer) malloc_data;
  GST_BUFFER_FREE_FUNC (buff) = gst_droid_cam_src_free_video_buffer;

  GST_LOG_OBJECT (src, "added buffer %p", buff);

  g_mutex_lock (&src->video_lock);
  g_queue_push_tail (src->video_queue, buff);
  g_cond_signal (&src->video_cond);
  g_mutex_unlock (&src->video_lock);
}

static void
gst_droid_cam_src_notify_callback (int32_t msg_type,
    int32_t ext1, int32_t ext2, void *user)
{
  GstDroidCamSrc *src;

  src = (GstDroidCamSrc *) user;

  GST_DEBUG_OBJECT (src, "notify callback: 0x%x, %i, %i", msg_type, ext1, ext2);

  /* TODO: more messages and error messages */
  switch (msg_type) {
    case CAMERA_MSG_SHUTTER:
      src->dev->ops->disable_msg_type (src->dev, CAMERA_MSG_SHUTTER);
      gst_droid_cam_src_send_capture_start (src);
      break;

    case CAMERA_MSG_FOCUS:
      if (ext1) {
        gst_droid_cam_src_send_message (src,
            GST_PHOTOGRAPHY_AUTOFOCUS_DONE,
            GST_PHOTOGRAPHY_FOCUS_STATUS_SUCCESS);
      } else {
        gst_droid_cam_src_send_message (src,
            GST_PHOTOGRAPHY_AUTOFOCUS_DONE, GST_PHOTOGRAPHY_FOCUS_STATUS_FAIL);
      }

      break;

    case CAMERA_MSG_FOCUS_MOVE:
      GST_LOG_OBJECT (src, "focus move message: %i", ext1);
      gst_droid_cam_src_send_message (src, "focus-move", ext1);
      break;

    case CAMERA_MSG_ERROR:
      GST_ELEMENT_ERROR (src, LIBRARY, FAILED, ("Error 0x%x from HAL", ext1),
          (NULL));
      break;

    default:
      GST_WARNING_OBJECT (src, "unknown message 0x%x", msg_type);
      break;
  }
}

#if 0
static void
gst_droid_cam_src_set_recording_hint (GstDroidCamSrc * src, gboolean apply)
{
  GST_DEBUG_OBJECT (src, "set recording hint");

  GST_OBJECT_LOCK (src);

  if (!src->camera_params) {
    GST_OBJECT_UNLOCK (src);
    GST_DEBUG_OBJECT (src,
        "Not fully initialized. Deferring setting recording-hint");
    return;
  }

  switch (src->mode) {
    case MODE_IMAGE:
      camera_params_set (src->camera_params, "recording-hint", "false");
      break;
    case MODE_VIDEO:
      camera_params_set (src->camera_params, "recording-hint", "true");
      break;
    default:
      GST_OBJECT_UNLOCK (src);
      GST_WARNING_OBJECT (src, "Unknown camera mode %d", src->mode);
      return;
  }

  GST_OBJECT_UNLOCK (src);

  GST_DEBUG_OBJECT (src, "recording hint set for mode %d", src->mode);

  if (apply) {
    gst_droid_cam_src_set_camera_params (src);
  }
}
#endif

static void
gst_droid_cam_src_free_video_buffer (gpointer data)
{
  GstDroidCamSrcVideoBufferData *malloc_data =
      (GstDroidCamSrcVideoBufferData *) data;
  GstDroidCamSrc *src = malloc_data->src;
  GstBuffer *buffer = malloc_data->buffer;

  GST_LOG_OBJECT (src, "free video buffer %p", buffer);

  src->dev->ops->release_recording_frame (src->dev, GST_BUFFER_DATA (buffer));

  g_mutex_lock (&src->pushed_video_frames_lock);
  --src->pushed_video_frames;
  GST_LOG_OBJECT (src, "pushed video frames is now %i",
      src->pushed_video_frames);
  g_cond_signal (&src->pushed_video_frames_cond);

  g_mutex_unlock (&src->pushed_video_frames_lock);

  g_slice_free (GstDroidCamSrcVideoBufferData, data);
}

static void
gst_droid_cam_src_send_capture_start (GstDroidCamSrc * src)
{
  GstStructure *s;
  GstMessage *msg;

  if (src->capture_start_sent) {
    GST_DEBUG_OBJECT (src, "%s message already sent",
        GST_DROID_CAM_SRC_CAPTURE_START);
    return;
  }

  GST_DEBUG_OBJECT (src, "Sending %s message", GST_DROID_CAM_SRC_CAPTURE_START);

  s = gst_structure_new (GST_DROID_CAM_SRC_CAPTURE_START, NULL);
  msg = gst_message_new_element (GST_OBJECT (src), s);

  if (!gst_element_post_message (GST_ELEMENT (src), msg)) {
    GST_WARNING_OBJECT (src,
        "This element has no bus, therefore no message sent!");
  }

  GST_LOG_OBJECT (src, "%s message sent", GST_DROID_CAM_SRC_CAPTURE_START);

  src->capture_start_sent = TRUE;
}

static void
gst_droid_cam_src_send_capture_end (GstDroidCamSrc * src)
{
  GstStructure *s;
  GstMessage *msg;

  if (src->capture_end_sent) {
    GST_DEBUG_OBJECT (src, "%s message already sent",
        GST_DROID_CAM_SRC_CAPTURE_END);
    return;
  }

  GST_DEBUG_OBJECT (src, "Sending %s message", GST_DROID_CAM_SRC_CAPTURE_END);

  s = gst_structure_new (GST_DROID_CAM_SRC_CAPTURE_END, NULL);
  msg = gst_message_new_element (GST_OBJECT (src), s);

  if (!gst_element_post_message (GST_ELEMENT (src), msg)) {
    GST_WARNING_OBJECT (src,
        "This element has no bus, therefore no message sent!");
  }

  GST_LOG_OBJECT (src, "%s message sent", GST_DROID_CAM_SRC_CAPTURE_END);


  src->capture_end_sent = TRUE;
}

void
gst_droid_cam_src_start_autofocus (GstDroidCamSrc * src)
{
  int err;

  GST_DEBUG_OBJECT (src, "start autofocus");

  if (!src->dev) {
    GST_WARNING_OBJECT (src, "camera device not opened");
    return;
  }

  err = src->dev->ops->auto_focus (src->dev);
  if (err != 0) {
    GST_WARNING_OBJECT (src, "Error %d starting autofocus", err);
    return;
  }
}

void
gst_droid_cam_src_stop_autofocus (GstDroidCamSrc * src)
{
  int err;

  GST_DEBUG_OBJECT (src, "stop autofocus");

  if (!src->dev) {
    GST_WARNING_OBJECT (src, "camera device not opened");
    return;
  }

  err = src->dev->ops->cancel_auto_focus (src->dev);
  if (err != 0) {
    GST_WARNING_OBJECT (src, "Error %d stopping autofocus", err);
    return;
  }
}

static void
gst_droid_cam_src_send_message (GstDroidCamSrc * src,
    const gchar * msg_name, int status)
{
  GstStructure *s;
  GstMessage *msg;

  GST_DEBUG_OBJECT (src, "send message %s %d", msg_name, status);

  s = gst_structure_new (msg_name, "status", G_TYPE_INT, status, NULL);
  msg = gst_message_new_element (GST_OBJECT (src), s);

  if (!gst_element_post_message (GST_ELEMENT (src), msg)) {
    GST_WARNING_OBJECT (src,
        "This element has no bus, therefore no message sent!");
  }

  GST_LOG_OBJECT (src, "%s message sent", msg_name);
}

static void
gst_droid_cam_src_update_max_zoom (GstDroidCamSrc * src)
{
  gchar *params;
  struct camera_params *camera_params;
  int max_zoom;
  GParamSpec *pspec;
  GParamSpecFloat *pspec_f;
  gboolean zoom_changed = FALSE;

  /* It's really bad how we get the value of max-zoom but at least it works. */
  GST_DEBUG_OBJECT (src, "update max zoom");

  if (!src->dev) {
    GST_DEBUG_OBJECT (src, "camera not open");
    return;
  }

  params = src->dev->ops->get_parameters (src->dev);
  camera_params = camera_params_from_string (params);

  if (src->dev->ops->put_parameters) {
    src->dev->ops->put_parameters (src->dev, params);
  } else {
    free (params);
  }

  max_zoom = camera_params_get_int (camera_params, "max-zoom") + 1;
  if (max_zoom + 1 != src->max_zoom * 10) {
    GST_DEBUG_OBJECT (src, "setting max_zoom to %f", src->max_zoom);
    src->max_zoom = (max_zoom + 1) / 10.0;

    g_object_notify (G_OBJECT (src), "max-zoom");

    zoom_changed = TRUE;
  }

  camera_params_free (camera_params);

  if (!zoom_changed) {
    return;
  }

  /* update gobject param spec */
  pspec =
      g_object_class_find_property (G_OBJECT_CLASS (GST_DROID_CAM_SRC_GET_CLASS
          (src)), "zoom");
  if (pspec && (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_FLOAT)) {
    pspec_f = G_PARAM_SPEC_FLOAT (pspec);
    pspec_f->maximum = src->max_zoom;

    /* TODO: Check that the current zoom value is less than the maximum zoom value. */
  } else {
    GST_WARNING_OBJECT (src, "failed to update maximum zoom value");
  }
}
