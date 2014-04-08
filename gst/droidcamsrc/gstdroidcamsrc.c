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
#include "cameraparams.h"
#include <gst/video/video.h>
#include <gst/anativewindowbuffer/gstanativewindowbufferpool.h>
#include "enums.h"
#include "exif.h"
#include "gstvfsrcpad.h"
#include "gstimgsrcpad.h"
#include "gstvidsrcpad.h"
#include "gstphotoiface.h"

#define DEFAULT_CAMERA_DEVICE         0
#define DEFAULT_MODE                  MODE_IMAGE
#define DEFAULT_IMAGE_NOISE_REDUCTION TRUE
#define DEFAULT_MAX_ZOOM              10.0
#define DEFAULT_VIDEO_TORCH           FALSE

GST_DEBUG_CATEGORY_STATIC (droidcam_debug);
#define GST_CAT_DEFAULT droidcam_debug

static GstStaticPadTemplate vfsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,"
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ];"
        "video/x-raw(" GST_CAPS_FEATURE_MEMORY_A_NATIVE_WINDOW_BUFFER "),"
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ]"));

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
     GST_STATIC_CAPS ("video/x-raw,"
       "framerate = (fraction) [ 0, MAX ], "
       "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ];"
       "video/x-raw(memory:AndroidMetadata),"
       "framerate = (fraction) [ 0, MAX ], "
       "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ]"));
static void gst_droid_cam_src_finalize (GObject * object);
static void gst_droid_cam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_droid_cam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_droid_cam_src_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_droid_cam_src_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_droid_cam_src_query (GstElement * element,
    GstQuery * query);

static gboolean gst_droid_cam_src_setup_pipeline (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_set_callbacks (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_set_camera_params (GstDroidCamSrc * src);
static void gst_droid_cam_src_tear_down_pipeline (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_probe_camera (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_start_pipeline (GstDroidCamSrc * src);

static void gst_droid_cam_src_start_capture (GstDroidCamSrc * src);
static void gst_droid_cam_src_stop_capture (GstDroidCamSrc * src);

static gboolean gst_droid_cam_src_start_image_capture_unlocked (GstDroidCamSrc *
    src);
static gboolean gst_droid_cam_src_start_video_capture_unlocked (GstDroidCamSrc *
    src);
static void gst_droid_cam_src_stop_video_capture (GstDroidCamSrc * src);
static void gst_droid_cam_src_apply_image_noise_reduction (GstDroidCamSrc *
    src);

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

static void gst_droid_cam_src_set_recording_hint (GstDroidCamSrc * src,
    gboolean apply);

static void gst_droid_cam_src_send_capture_start (GstDroidCamSrc * src);
static void gst_droid_cam_src_send_capture_end (GstDroidCamSrc * src);
static void gst_droid_cam_src_send_message (GstDroidCamSrc * src,
    const gchar * msg_name, int status);
static void gst_droid_cam_src_adjust_video_torch (GstDroidCamSrc * src);
static gboolean gst_droid_cam_src_handle_roi_event (GstDroidCamSrc * src,
    GstEvent * event);

#define gst_droid_cam_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDroidCamSrc, gst_droid_cam_src,
    GST_TYPE_BIN, G_IMPLEMENT_INTERFACE (GST_TYPE_PHOTOGRAPHY,
        gst_photo_iface_photo_interface_init));

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
  guint x;
  guint y;
  guint w;
  guint h;
  guint p;
} RoiEntry;

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

  g_object_class_install_property (gobject_class, PROP_SENSOR_MOUNT_ANGLE,
      g_param_spec_enum ("sensor-mount-angle", "Sensor mount angle",
          "The orientation angle of camera sensor relative to the natural display orientation.",
          GST_TYPE_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE,
          GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_IMAGE_NOISE_REDUCTION,
      g_param_spec_boolean ("image-noise-reduction", "Image noise reduction",
          "HAL specific noise reduction for captured images",
          DEFAULT_IMAGE_NOISE_REDUCTION,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_ZOOM,
      g_param_spec_float ("max-zoom", "Maximum zoom level (note: may change "
          "depending on resolution/implementation)",
          "Digital zoom factor", 1.0f, G_MAXFLOAT,
          DEFAULT_MAX_ZOOM, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VIDEO_TORCH,
      g_param_spec_boolean ("video-torch", "Video torch",
          "Sets torch light on or off for video recording",
          DEFAULT_VIDEO_TORCH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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

  gst_element_class_set_static_metadata (element_class,
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

  GST_DEBUG_CATEGORY_INIT (droidcam_debug, "droidcam", 0,
      "Android camera source");
}

static void
gst_droid_cam_src_init (GstDroidCamSrc * src)
{
  GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_SOURCE);

  src->cam = NULL;
  src->dev = NULL;
  src->hwmod = NULL;
  src->cam_dev = NULL;
  src->user_camera_device = DEFAULT_CAMERA_DEVICE;
  src->camera_device = DEFAULT_CAMERA_DEVICE;
  src->mode = DEFAULT_MODE;
  src->camera_params = NULL;
  src->events = NULL;
  src->settings = gst_camera_settings_new ();
  src->image_noise_reduction = DEFAULT_IMAGE_NOISE_REDUCTION;
  src->max_zoom = DEFAULT_MAX_ZOOM;
  src->video_torch = DEFAULT_VIDEO_TORCH;
  src->min_ev_comp = 0;
  src->max_ev_comp = 0;
  src->ev_comp_step = 0.0;

  src->capturing = FALSE;
  g_mutex_init (&src->capturing_mutex);

  src->image_renegotiate = TRUE;
  src->video_renegotiate = TRUE;

  g_mutex_init (&src->params_lock);

  src->video_capture_status = VIDEO_CAPTURE_STOPPED;
  src->video_start_time = 0;

  src->pushed_video_frames = 0;
  src->num_video_frames = 0;
  g_mutex_init (&src->num_video_frames_lock);
  g_cond_init (&src->num_video_frames_cond);

  src->device_info[0].orientation =
      GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN;
  src->device_info[0].id = -1;
  src->device_info[1].orientation =
      GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN;
  src->device_info[1].id = -1;

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

  gst_vf_src_pad_init_window (&src->viewfinder_window);
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

  g_mutex_clear (&src->params_lock);

  g_mutex_clear (&src->capturing_mutex);

  g_cond_clear (&src->num_video_frames_cond);
  g_mutex_clear (&src->num_video_frames_lock);

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

    case PROP_READY_FOR_CAPTURE:
      g_value_set_boolean (value, !src->capturing);
      break;

    case PROP_SENSOR_MOUNT_ANGLE:
      g_value_set_enum (value,
          src->device_info[src->camera_device].orientation);
      break;

    case PROP_IMAGE_NOISE_REDUCTION:
      g_value_set_boolean (value, src->image_noise_reduction);
      break;

    case PROP_MAX_ZOOM:
      g_value_set_float (value, src->max_zoom);
      break;

    case PROP_VIDEO_TORCH:
      g_value_set_boolean (value, src->video_torch);
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
      gst_droid_cam_src_apply_image_noise_reduction (src);
      gst_droid_cam_src_adjust_video_torch (src);
      gst_droid_cam_src_set_recording_hint (src, TRUE);
      break;

    case PROP_IMAGE_NOISE_REDUCTION:
      src->image_noise_reduction = g_value_get_boolean (value);
      gst_droid_cam_src_apply_image_noise_reduction (src);
      break;

    case PROP_VIDEO_TORCH:
      src->video_torch = g_value_get_boolean (value);
      gst_droid_cam_src_adjust_video_torch (src);
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
  int id;
  gchar *cam_id = NULL;
  gchar *params = NULL;

  GST_DEBUG_OBJECT (src, "setup pipeline for camera %d", src->camera_device);

  src->camera_device = src->user_camera_device;

  id = src->device_info[src->camera_device].id;
  if (id == -1) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("failed to open camera device %d because it's not been detected",
            src->camera_device), (NULL));
    goto cleanup;
  }

  /* We assume camera id 0 is back and 1 is front */
  cam_id = g_strdup_printf ("%i", id);
  err = src->cam->common.methods->open (src->hwmod, cam_id, &src->cam_dev);
  g_free (cam_id);

  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("failed to open camera device %d: %d", src->camera_device, err),
        (NULL));
    goto cleanup;
  }

  src->dev = (camera_device_t *) src->cam_dev;

  g_mutex_lock (&src->params_lock);
  params = src->dev->ops->get_parameters (src->dev);
  src->camera_params = camera_params_from_string (params);
  if (src->dev->ops->put_parameters) {
    src->dev->ops->put_parameters (src->dev, params);
  } else {
    free (params);
  }
  g_mutex_unlock (&src->params_lock);

  gst_photo_iface_init_ev_comp (src);
  gst_photo_iface_settings_to_params (src);
  gst_droid_cam_src_set_recording_hint (src, TRUE);

  /* TODO: If we end up with a device with 1 camera then this will break. */
  if (src->camera_device == 0) {
    gst_droid_cam_src_apply_image_noise_reduction (src);
    gst_droid_cam_src_adjust_video_torch (src);
  }

  if (!gst_droid_cam_src_set_callbacks (src)) {
    goto cleanup;
  }

  src->viewfinder_orientation = src->device_info[src->camera_device].orientation;

  return TRUE;

cleanup:
  gst_droid_cam_src_tear_down_pipeline (src);
  return FALSE;
}

static gboolean
gst_droid_cam_src_probe_camera (GstDroidCamSrc * src)
{
  int err;
  int num_of_cameras;
  struct camera_info info;
  int x;

  GST_DEBUG_OBJECT (src, "probe camera");

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

  num_of_cameras = src->cam->get_number_of_cameras ();
  GST_INFO_OBJECT (src, "number of cameras: %d", num_of_cameras);

  if (num_of_cameras > 2) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("number of cameras is more than 2"),
        (NULL));
    goto cleanup;
  }

  for (x = 0; x < num_of_cameras; x++) {
    int dev;
    err = src->cam->get_camera_info (x, &info);
    if (err != 0) {
      GST_WARNING_OBJECT (src, "Error %d getting camera %d info", err, x);
      continue;
    }

    /* Now we have info. Let's fill our structs */
    if (info.facing == CAMERA_FACING_BACK) {
      dev = 0;
    } else if (info.facing == CAMERA_FACING_FRONT) {
      dev = 1;
    } else {
      GST_WARNING_OBJECT (src, "Unknown camera %d", x);
      continue;
    }

    src->device_info[dev].id = x;
    src->device_info[dev].orientation = info.orientation;

    GST_INFO_OBJECT (src, "camera %d with orientation %d", x, info.orientation);
  }

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

  GST_DEBUG_OBJECT (src, "set callbacks %p %p %p", src->dev, src, &src->viewfinder_window);

  src->dev->ops->set_callbacks (src->dev, gst_droid_cam_src_notify_callback,
      gst_droid_cam_src_data_callback,
      gst_droid_cam_src_data_timestamp_callback, gst_camera_memory_get, src);

  err = src->dev->ops->set_preview_window (src->dev, &src->viewfinder_window);

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

  GST_OBJECT_LOCK (src);
  params = camera_params_to_string (src->camera_params);
  GST_OBJECT_UNLOCK (src);

  GST_LOG_OBJECT (src, "params %s", params);

  g_mutex_lock (&src->params_lock);
  err = src->dev->ops->set_parameters (src->dev, params);
  free (params);
  g_mutex_unlock (&src->params_lock);

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

    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      g_mutex_lock (&src->capturing_mutex);

      if (src->mode == MODE_VIDEO && src->capturing) {
        GST_WARNING_OBJECT (src, "video capture still running running");
        g_mutex_unlock (&src->capturing_mutex);
        gst_droid_cam_src_stop_video_capture (src);
      } else {
        g_mutex_unlock (&src->capturing_mutex);
      }

      no_preroll = TRUE;

      src->dev->ops->stop_preview (src->dev);

      GST_PAD_STREAM_LOCK (src->vfsrc);
      if (src->viewfinder_pool) {
        gst_object_unref (src->viewfinder_pool);
        src->viewfinder_pool = NULL;
      }
      GST_PAD_STREAM_UNLOCK (src->vfsrc);

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
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_droid_cam_src_update_max_zoom (src);
      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (!gst_droid_cam_src_start_pipeline (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      src->image_renegotiate = TRUE;
      src->video_renegotiate = TRUE;
      /* TODO: is this the right thing to do? */
      /* TODO: do we need locking here? */
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
    case GST_EVENT_STREAM_START:
    case GST_EVENT_CAPS:
    case GST_EVENT_SEGMENT:
    case GST_EVENT_TOC:
    case GST_EVENT_SEGMENT_DONE:
    case GST_EVENT_GAP:
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    case GST_EVENT_SEEK:
    case GST_EVENT_STEP:
    case GST_EVENT_RECONFIGURE:
    case GST_EVENT_TOC_SELECT:
    case GST_EVENT_BUFFERSIZE:
    case GST_EVENT_SINK_MESSAGE:
      break;

    case GST_EVENT_EOS:
      ret = gst_pad_push_event (src->vfsrc, event);

      GST_DEBUG_OBJECT (src, "pushing event %p", event);

      event = NULL;
      ret = TRUE;

      break;

    case GST_EVENT_CUSTOM_UPSTREAM:{
      const GstStructure *s = gst_event_get_structure (event);
      if (s && gst_structure_has_name (s, "regions-of-interest")) {
        GST_INFO_OBJECT (src, "Got ROI event %p" GST_PTR_FORMAT, s);
        ret = gst_droid_cam_src_handle_roi_event (src, event);
        event = NULL;
        break;
      }
    }

    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_STICKY:
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

static gboolean
gst_droid_cam_src_query (GstElement * element, GstQuery * query)
{
  gboolean ret = TRUE;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (element);

  GST_DEBUG_OBJECT (src, "query");

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY: {
      GstClockTime duration;
      gint buffer_count;
      if (!src->dev) {
        GST_WARNING_OBJECT (src, "Can't give latency since device isn't open");
        ret = FALSE;
        break;
      }

      GST_PAD_STREAM_LOCK (src->vfsrc);

      duration = gst_util_uint64_scale_int (GST_SECOND,
          src->viewfinder_info.fps_d, src->viewfinder_info.fps_n);
      buffer_count = src->viewfinder_buffer_count;

      GST_PAD_STREAM_UNLOCK (src->vfsrc);

      gst_query_set_latency (query, TRUE, duration, buffer_count * duration);

      GST_DEBUG_OBJECT (src, "latency query result %" GST_PTR_FORMAT, query);

      ret = TRUE;
      break;
    }
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
  GST_DEBUG_OBJECT (src, "start pipeline");

  src->dev->ops->enable_msg_type (src->dev, CAMERA_MSG_ALL_MSGS);
  src->dev->ops->disable_msg_type (src->dev, CAMERA_MSG_PREVIEW_FRAME);
  src->dev->ops->disable_msg_type (src->dev, CAMERA_MSG_RAW_IMAGE);
  src->dev->ops->disable_msg_type (src->dev, CAMERA_MSG_RAW_IMAGE_NOTIFY);

  err = src->dev->ops->start_preview (src->dev);
  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not start camera: %d", err),
        (NULL));

    return FALSE;
  }

  return TRUE;
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
  int err = 0;
  gboolean ret;

  GST_DEBUG_OBJECT (src, "start video capture unlocked");

  gst_pad_set_active (src->vidsrc, TRUE);

  if (!gst_vid_src_pad_renegotiate (src->vidsrc)) {
    GST_WARNING_OBJECT (src, "Failed to negotiate video capture caps");

    return FALSE;
  }

  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not start camera: %d", err),
        (NULL));
    GST_WARNING_OBJECT (src, "failed to start preview: %d", err);

    ret = FALSE;
    goto out;
  }

  g_mutex_lock (&src->num_video_frames_lock);
  src->num_video_frames = 0;
  g_mutex_unlock (&src->num_video_frames_lock);


  GST_PAD_STREAM_LOCK (src->vidsrc);
  src->video_capture_status = VIDEO_CAPTURE_STARTING;
  GST_PAD_STREAM_UNLOCK (src->vidsrc);


  /* We need to reset focus mode because default for video recording is continuous-video */
  gst_photo_iface_update_focus_mode (src);

  err = src->dev->ops->start_recording (src->dev);
  if (err != 0) {
    GST_WARNING_OBJECT (src, "failed to start video recording: %d", err);

    ret = FALSE;
    goto out;
  }

  ret = TRUE;

out:

  GST_DEBUG_OBJECT (src, "done");

  return ret;
}

static void
gst_droid_cam_src_stop_video_capture (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "stop video capture");

  GST_PAD_STREAM_LOCK (src->vidsrc);
  src->video_capture_status = VIDEO_CAPTURE_STOPPING;
  GST_PAD_STREAM_UNLOCK (src->vidsrc);

  gst_pad_push_event (src->vidsrc, gst_event_new_eos ());
  gst_pad_set_active (src->vidsrc, FALSE);

  /*
   * We cannot stop if we have not received at least 1-3 frames
   * There is a race condition somewhere which will cause invalid pointer
   * to be passed to the kernel which will cause a panic and reboot:
   */
  g_mutex_lock (&src->num_video_frames_lock);

  while (src->num_video_frames < 4 && src->pushed_video_frames > 0) {
    GST_DEBUG_OBJECT (src, "waiting for more buffers to be pushed. Now: %d",
        src->num_video_frames);
    g_cond_wait (&src->num_video_frames_cond, &src->num_video_frames_lock);
  }

  GST_DEBUG_OBJECT (src, "done waiting");
  g_mutex_unlock (&src->num_video_frames_lock);

  /* Now we really stop. */
  src->dev->ops->stop_recording (src->dev);

  GST_DEBUG_OBJECT (src, "HAL stopped recording");

  GST_PAD_STREAM_LOCK (src->vidsrc);
  src->video_capture_status = VIDEO_CAPTURE_STOPPED;
  GST_PAD_STREAM_UNLOCK (src->vidsrc);

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
  GstMemory *memory;
  GstFlowReturn ret;
  GstTagList *tags;
  GstSegment segment;

  GST_DEBUG_OBJECT (src, "handle compressed image");

  gst_droid_cam_src_finish_capture (src);

  memory = gst_camera_memory_new (mem, index);
  buffer = gst_buffer_new ();

  gst_buffer_append_memory (buffer, memory);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.stop = -1;
  segment.position = 0;

  if (!gst_pad_push_event (src->vidsrc, gst_event_new_segment (&segment))) {
    GST_WARNING_OBJECT (src, "failed to push new segment");
  }

  tags = gst_droid_cam_src_get_exif_tags (buffer);
  if (tags) {
    GstEvent *event = gst_event_new_tag (tags);
    GST_DEBUG_OBJECT (src, "pushing tags %" GST_PTR_FORMAT, tags);
    if (!gst_pad_push_event (src->imgsrc, event)) {
      GST_WARNING_OBJECT (src, "Failed to push tags");
    }
  } else {
    GST_WARNING_OBJECT (src, "Failed to read exif tags from compressed JPEG");
  }

  ret = gst_pad_push (src->imgsrc, buffer);

  if (ret == GST_FLOW_NOT_LINKED || ret <= GST_FLOW_NOT_NEGOTIATED) {
    GST_ELEMENT_ERROR (src, STREAM, FAILED,
        ("Internal data flow error."),
        ("streaming task paused, reason %s (%d)", gst_flow_get_name (ret),
            ret));
  }
}

static gboolean
gst_droid_cam_src_finish_capture (GstDroidCamSrc * src)
{
  int err;

  GST_DEBUG_OBJECT (src, "finish capture");

  err = src->dev->ops->start_preview (src->dev);

  if (err != 0) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, ("Could not start camera: %d", err),
        (NULL));
    goto out;
  }

  GST_DEBUG_OBJECT (src, "finish capture done");

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

  GST_LOG_OBJECT (src, "data callback");
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
  GstDroidCamSrc *src = (GstDroidCamSrc *) user;

  GST_LOG_OBJECT (src, "data timestamp callback %lld", timestamp);

  switch (msg_type) {
    case CAMERA_MSG_VIDEO_FRAME: {
      int num_frames;
      GstBuffer *buffer;
      GstMemory *memory;
      GstClockTime start_time;
      VideoCaptureStatus status;
      GstFlowReturn ret;

      GST_PAD_STREAM_LOCK (src->vidsrc);

      g_mutex_lock (&src->num_video_frames_lock);
      num_frames = ++src->num_video_frames;
      g_mutex_unlock (&src->num_video_frames_lock);

      status = src->video_capture_status;

      if (status == VIDEO_CAPTURE_STARTING) {
        src->video_capture_status = VIDEO_CAPTURE_RUNNING;
        src->video_start_time = timestamp;
      }

      start_time = src->video_start_time;

      GST_PAD_STREAM_UNLOCK (src->vidsrc);

      switch (status) {
        case VIDEO_CAPTURE_STARTING: {
          GstSegment segment;
          gst_segment_init (&segment, GST_FORMAT_TIME);
          segment.start = 0;
          segment.stop = -1;
          segment.position = 0;

          if (!gst_pad_push_event (src->vidsrc, gst_event_new_segment (&segment))) {
            /* TODO: send an error and stop task? */
            GST_WARNING_OBJECT (src, "failed to push new segment");
          }
          break;
        }
        case VIDEO_CAPTURE_STOPPING:
        case VIDEO_CAPTURE_STOPPED:
        case VIDEO_CAPTURE_ERROR:
          if (num_frames == 4) {
            g_mutex_lock (&src->num_video_frames_lock);
            g_cond_signal (&src->num_video_frames_cond);
            g_mutex_unlock (&src->num_video_frames_lock);
          }
          return;
        default:
          break;
      }

      g_mutex_lock (&src->num_video_frames_lock);
      ++src->pushed_video_frames;
      g_mutex_unlock (&src->num_video_frames_lock);

      memory = gst_camera_memory_new_video (data, index, src);
      buffer = gst_buffer_new ();
      gst_buffer_append_memory (buffer, memory);

      GST_BUFFER_PTS (buffer) = timestamp - start_time;
      GST_BUFFER_OFFSET(buffer) = num_frames;
      GST_BUFFER_OFFSET_END(buffer) = num_frames + 1;

      ret = gst_pad_push (src->vidsrc, buffer);

      if (ret != GST_FLOW_OK) {
          GST_PAD_STREAM_LOCK (src->vidsrc);
          src->video_capture_status = VIDEO_CAPTURE_ERROR;
          GST_PAD_STREAM_UNLOCK (src->vidsrc);

          GST_ELEMENT_ERROR (src, STREAM, FAILED,
              ("Internal data flow error."),
              ("streaming task paused, reason %s (%d)", gst_flow_get_name (ret),
                  ret));

          gst_pad_push_event (src->vidsrc, gst_event_new_eos ());
      }
      break;
    }
    default:
      GST_WARNING_OBJECT (src, "Unhandled data timestamp callback %x", msg_type);
      break;
  }
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

  /* The HAL stops producing viewfinder frames if the recording-hint is
   * cleared after a recording. */
#if 0
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
#endif

  GST_OBJECT_UNLOCK (src);

  GST_DEBUG_OBJECT (src, "recording hint set for mode %d", src->mode);

  if (apply) {
    gst_droid_cam_src_set_camera_params (src);
  }
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

  s = gst_structure_new_empty (GST_DROID_CAM_SRC_CAPTURE_START);
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

  s = gst_structure_new_empty (GST_DROID_CAM_SRC_CAPTURE_END);
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
gst_droid_cam_src_apply_image_noise_reduction (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "apply image noise reduction %d in camera mode %d",
      src->image_noise_reduction, src->mode);

  if (!src->camera_params) {
    GST_WARNING_OBJECT (src, "Deferring image noise reduction setting");
    return;
  }

  if (src->camera_device != 0) {
    GST_WARNING_OBJECT (src,
        "Image noise reduction is only supported by primary camera");
    return;
  }

  GST_OBJECT_LOCK (src);

  if (src->mode != MODE_IMAGE) {
    GST_LOG_OBJECT (src, "Disabling image noise reduction in video mode");
    camera_params_set (src->camera_params, "denoise", "denoise-off");
  } else if (src->image_noise_reduction) {
    GST_LOG_OBJECT (src, "Enabling image noise reduction");
    camera_params_set (src->camera_params, "denoise", "denoise-on");
  } else {
    GST_LOG_OBJECT (src, "Disabling image noise reduction");
    camera_params_set (src->camera_params, "denoise", "denoise-off");
  }

  GST_OBJECT_UNLOCK (src);

  gst_droid_cam_src_set_camera_params (src);
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

  g_mutex_lock (&src->params_lock);
  params = src->dev->ops->get_parameters (src->dev);
  camera_params = camera_params_from_string (params);

  if (src->dev->ops->put_parameters) {
    src->dev->ops->put_parameters (src->dev, params);
  } else {
    free (params);
  }
  g_mutex_unlock (&src->params_lock);

  /* 0  -> 1.0
   * 1  -> 1.1
   * 2  -> 1.2
   * 10 -> 2.0
   * 60 -> 7.0
   */
  max_zoom = camera_params_get_int (camera_params, "max-zoom");
  if (max_zoom + 10 != (int) (src->max_zoom * 10)) {
    GST_DEBUG_OBJECT (src, "setting max_zoom to %f", src->max_zoom);
    src->max_zoom = (max_zoom + 10) / 10.0;

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

static void
gst_droid_cam_src_adjust_video_torch (GstDroidCamSrc * src)
{
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  GST_DEBUG_OBJECT (src, "adjust video torch");

  if (!src->camera_params) {
    GST_DEBUG_OBJECT (src,
        "Not fully initialized. Deferring video torch setting");
    return;
  }

  if (src->mode != MODE_VIDEO) {
    GST_DEBUG_OBJECT (src, "No video torch if not in video mode");
    return;
  }

  if (src->video_torch) {
    GST_OBJECT_LOCK (src);
    camera_params_set (src->camera_params, "flash-mode", "torch");
    GST_OBJECT_UNLOCK (src);
    if (!klass->set_camera_params (src)) {
      GST_WARNING_OBJECT (src, "Failed to set video torch");
      gst_photo_iface_update_flash_mode (src);
      src->video_torch = FALSE;
      g_object_notify (G_OBJECT (src), "video-torch");
    }
  } else {
    gst_photo_iface_update_flash_mode (src);
  }
}

static gboolean
gst_droid_cam_src_handle_roi_event (GstDroidCamSrc * src, GstEvent * event)
{
  guint width, height, objcount, i;
  guint supported_focus_regions, supported_metering_regions, supported_regions;
  const GValue *regions, *region;
  const GstStructure *rs;
  gboolean ret = FALSE;
  GArray *array = g_array_new (FALSE, FALSE, sizeof (RoiEntry));
  const GstStructure *s = gst_event_get_structure (event);
  gboolean reset = FALSE;
  gchar *focus_param = NULL, *metering_param = NULL;
  gfloat scaleX = 0.0, scaleY = 0.0;

  if (!gst_structure_get_uint (s, "frame-width", &width) ||
      !gst_structure_get_uint (s, "frame-height", &height)) {
    GST_WARNING_OBJECT (src, "missing frame width or height from ROI event");
    goto out;
  }

  scaleX = 2000.0 / width;
  scaleY = 2000.0 / height;

  GST_DEBUG_OBJECT (src, "X scale = %f, Y scale = %f", scaleX, scaleY);

  regions = gst_structure_get_value (s, "regions");
  if (!regions) {
    GST_WARNING_OBJECT (src, "ROI event missing regions data");
    goto out;
  }

  GST_OBJECT_LOCK (src);
  supported_focus_regions =
      camera_params_get_int (src->camera_params, "max-num-focus-areas");
  supported_metering_regions =
      camera_params_get_int (src->camera_params, "max-num-focus-areas");
  GST_OBJECT_UNLOCK (src);

  objcount = gst_value_list_get_size (regions);
  supported_regions = supported_focus_regions > supported_metering_regions
          ? supported_focus_regions : supported_metering_regions;
  if (objcount > supported_regions) {
    GST_DEBUG_OBJECT (src, "HAL supports only %d regions out of %d sent",
        supported_regions, objcount);
    objcount = supported_regions;
  }

  for (i = 0; i < objcount; i++) {
    region = gst_value_list_get_value (regions, i);
    rs = gst_value_get_structure (region);

    guint x, y, w, h, p;
    if (!gst_structure_get_uint (rs, "region-x", &x) ||
        !gst_structure_get_uint (rs, "region-y", &y) ||
        !gst_structure_get_uint (rs, "region-w", &w) ||
        !gst_structure_get_uint (rs, "region-h", &h)) {
      GST_WARNING_OBJECT (src, "ROI %d has incomplete information", i);
      continue;
    }

    if (!gst_structure_get_uint (rs, "region-priority", &p)) {
      p = 1;
    }

    GST_DEBUG_OBJECT (src, "ROI %d info: x=%d, y=%d, w=%d, h=%d, p=%d", i, x, y,
        w, h, p);

    RoiEntry roi;
    roi.x = x;
    roi.y = y;
    roi.w = w;
    roi.h = h;
    roi.p = p;

    g_array_append_val (array, roi);
  }

  if (array->len == 0) {
    GST_INFO_OBJECT (src, "empty ROI array. Doing nothing");
    goto out;
  }

  /* If we have any entry with priority set to 0 then we reset. */
  for (i = 0; i < array->len; i++) {
    RoiEntry entry = g_array_index (array, RoiEntry, i);
    GST_INFO_OBJECT (src, "ROI entry %d has p = 0", i);
    if (entry.p == 0) {
      reset = TRUE;
      break;
    }
  }

  if (reset) {
    GST_INFO_OBJECT (src, "resetting roi");
    GST_OBJECT_LOCK (src);
    camera_params_set (src->camera_params, "focus-areas", "(0, 0, 0, 0, 0)");
    GST_OBJECT_UNLOCK (src);
    goto update_and_out;
  }

  for (i = 0; i < array->len; i++) {
    RoiEntry entry = g_array_index (array, RoiEntry, i);
    gint x = (scaleX * entry.x),
        y = (scaleY * entry.y),
        r = (entry.x + entry.w) * scaleX, b = (entry.y + entry.h) * scaleY;

    gchar *str =
        g_strdup_printf ("(%d, %d, %d, %d, %d)", x - 1000, y - 1000, r - 1000,
        b - 1000, entry.p);
    if (focus_param && i < supported_focus_regions) {
      gchar *old_param = focus_param;
      focus_param = g_strjoin (",", old_param, src, NULL);
      g_free (old_param);
    } else if (i < supported_focus_regions) {
      focus_param = strdup (str);
    }
    if (metering_param && i < supported_metering_regions) {
      gchar *old_param = metering_param;
      metering_param = g_strjoin (",", old_param, src, NULL);
      g_free (old_param);
    } else if (i < supported_metering_regions) {
      metering_param = strdup (str);
    }
    g_free (str);
  }

  GST_OBJECT_LOCK (src);
  if (focus_param) {
    GST_DEBUG_OBJECT (src, "Setting focus roi param %s", focus_param);
    camera_params_set (src->camera_params, "focus-areas", focus_param);
    g_free (focus_param);
  }
  if (metering_param) {
    GST_DEBUG_OBJECT (src, "Setting metering roi param %s", metering_param);
    camera_params_set (src->camera_params, "metering-areas", metering_param);
    g_free (metering_param);
  }
  GST_OBJECT_UNLOCK (src);

update_and_out:
  ret = gst_droid_cam_src_set_camera_params (src);

out:
  g_array_unref (array);
  gst_event_unref (event);
  return ret;
}
