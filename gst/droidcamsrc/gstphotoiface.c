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

#include "gstphotoiface.h"
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#include <gst/basecamerabinsrc/gstbasecamerasrc.h>
#undef GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include "gstdroidcamsrc.h"
#include "cameraparams.h"
#include "gstimgsrcpad.h"
#include "gstvfsrcpad.h"
#include <math.h>
#include <stdlib.h>

GST_DEBUG_CATEGORY_STATIC (droidphoto_debug);
#define GST_CAT_DEFAULT droidphoto_debug

/* Flash */
static gboolean gst_photo_iface_get_flash_mode (GstPhotography * photo,
    GstPhotographyFlashMode * flash);
static gboolean gst_photo_iface_set_flash_mode (GstPhotography * photo,
    GstPhotographyFlashMode flash);
static gboolean gst_photo_iface_set_flash_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyFlashMode flash, gboolean commit);

/* Focus */
static gboolean gst_photo_iface_get_focus_mode (GstPhotography * photo,
    GstPhotographyFocusMode * focus);
static gboolean gst_photo_iface_set_focus_mode (GstPhotography * photo,
    GstPhotographyFocusMode focus);
static gboolean gst_photo_iface_set_focus_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyFocusMode focus, gboolean commit);

/* White balance */
static gboolean gst_photo_iface_get_white_balance_mode (GstPhotography * photo,
    GstPhotographyWhiteBalanceMode * wb);
static gboolean gst_photo_iface_set_white_balance_mode (GstPhotography * photo,
    GstPhotographyWhiteBalanceMode wb);
static gboolean gst_photo_iface_set_white_balance_mode_unlocked
    (GstDroidCamSrc * src, GstPhotographyWhiteBalanceMode wb, gboolean commit);

/* zoom */
static gboolean gst_photo_iface_get_zoom (GstPhotography * photo,
    gfloat * zoom);
static gboolean gst_photo_iface_set_zoom (GstPhotography * photo, gfloat zoom);
static gboolean gst_photo_iface_set_zoom_unlocked (GstDroidCamSrc * src,
    gfloat zoom, gboolean commit);

/* ISO */
static gboolean gst_photo_iface_get_iso_speed (GstPhotography * photo,
    guint * iso);
static gboolean gst_photo_iface_set_iso_speed (GstPhotography * photo,
    guint iso);
static gboolean gst_photo_iface_set_iso_speed_unlocked (GstDroidCamSrc * src,
    guint iso, gboolean commit);

/* EV comp */
static gboolean gst_photo_iface_get_ev_compensation (GstPhotography * photo,
    gfloat * ev);
static gboolean gst_photo_iface_set_ev_compensation (GstPhotography * photo,
    gfloat ev);
static gboolean gst_photo_iface_set_ev_compensation_unlocked
    (GstDroidCamSrc * src, gfloat ev, gboolean commit);

/* Color tone mode */
static gboolean gst_photo_iface_get_color_tone_mode (GstPhotography * photo,
    GstPhotographyColorToneMode * mode);
static gboolean gst_photo_iface_set_color_tone_mode (GstPhotography * photo,
    GstPhotographyColorToneMode mode);
static gboolean gst_photo_iface_set_color_tone_mode_unlocked
    (GstDroidCamSrc * src, GstPhotographyColorToneMode mode, gboolean commit);

/* Scene mode */
static gboolean gst_photo_iface_get_scene_mode (GstPhotography * photo,
    GstPhotographySceneMode * mode);
static gboolean gst_photo_iface_set_scene_mode (GstPhotography * photo,
    GstPhotographySceneMode mode);
static gboolean gst_photo_iface_set_scene_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographySceneMode mode, gboolean commit);

/* Flicker reduction */
static gboolean gst_photo_iface_get_flicker_mode (GstPhotography * photo,
    GstPhotographyFlickerReductionMode * mode);
static gboolean gst_photo_iface_set_flicker_mode (GstPhotography * photo,
    GstPhotographyFlickerReductionMode mode);
static gboolean gst_photo_iface_set_flicker_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyFlickerReductionMode mode, gboolean commit);

/* Auto exposure */
static gboolean gst_photo_iface_set_exposure_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyExposureMode mode, gboolean commit);

/* Face detection */
static gboolean gst_photo_iface_set_face_detection_enabled_unlocked
    (GstDroidCamSrc * src, gboolean enabled, gboolean commit);

/* Capabilities */
static  GstPhotographyCaps gst_photo_iface_get_capabilities
    (GstPhotography * photo);
static  GstPhotographyCaps gst_photo_iface_get_capabilities_unlocked
    (GstDroidCamSrc *src);

/* Auto focus */
static void gst_photo_iface_set_autofocus (GstPhotography * photo, gboolean on);

static gboolean gst_photo_iface_get_config (GstPhotography * photo,
    GstPhotographySettings * config);
static gboolean gst_photo_iface_set_config (GstPhotography * photo,
    GstPhotographySettings * config);

static gboolean
gst_photo_iface_set_enum_parameter_unlocked (GstDroidCamSrc * src,
    GHashTable *table, const gchar *parameter, int enumeration, gboolean commit)
{
  gboolean ret = TRUE;
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  const char *val = gst_camera_settings_find_droid (table, enumeration);

  GST_DEBUG_OBJECT (src, "set %s to %i (%s)", parameter, enumeration, val);

  if (!val) {
    ret = FALSE;
  } else if (src->camera_params) {
    camera_params_set (src->camera_params, parameter, val);

    if (commit) {
      GST_OBJECT_UNLOCK (src);
      ret = klass->set_camera_params (src);
      GST_OBJECT_LOCK (src);
    }
  }

  return ret;
}

void
gst_photo_iface_photo_interface_init (GstPhotographyInterface * iface)
{
  iface->get_flash_mode = gst_photo_iface_get_flash_mode;
  iface->set_flash_mode = gst_photo_iface_set_flash_mode;
  iface->get_focus_mode = gst_photo_iface_get_focus_mode;
  iface->set_focus_mode = gst_photo_iface_set_focus_mode;
  iface->get_white_balance_mode = gst_photo_iface_get_white_balance_mode;
  iface->set_white_balance_mode = gst_photo_iface_set_white_balance_mode;
  iface->get_zoom = gst_photo_iface_get_zoom;
  iface->set_zoom = gst_photo_iface_set_zoom;
  iface->get_iso_speed = gst_photo_iface_get_iso_speed;
  iface->set_iso_speed = gst_photo_iface_set_iso_speed;
  iface->get_ev_compensation = gst_photo_iface_get_ev_compensation;
  iface->set_ev_compensation = gst_photo_iface_set_ev_compensation;
  iface->get_color_tone_mode = gst_photo_iface_get_color_tone_mode;
  iface->set_color_tone_mode = gst_photo_iface_set_color_tone_mode;
  iface->get_scene_mode = gst_photo_iface_get_scene_mode;
  iface->set_scene_mode = gst_photo_iface_set_scene_mode;
  iface->get_flicker_mode = gst_photo_iface_get_flicker_mode;
  iface->set_flicker_mode = gst_photo_iface_set_flicker_mode;
  iface->get_capabilities = gst_photo_iface_get_capabilities;
  iface->get_config = gst_photo_iface_get_config;
  iface->set_config = gst_photo_iface_set_config;

  iface->set_autofocus = gst_photo_iface_set_autofocus;


  GST_DEBUG_CATEGORY_INIT (droidphoto_debug, "droidphoto", 0,
      "Android camera source photography interface");

  // TODO: more
}

void
gst_photo_iface_init_settings (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "init settings");

  memset (&src->photo_settings, 0x0, sizeof (src->photo_settings));
  src->photo_settings.flash_mode = GST_PHOTOGRAPHY_FLASH_MODE_AUTO;
  src->photo_settings.focus_mode = GST_PHOTOGRAPHY_FOCUS_MODE_AUTO;
  src->photo_settings.wb_mode = GST_PHOTOGRAPHY_WB_MODE_AUTO;
  src->photo_settings.zoom = 1.0;
  src->photo_settings.iso_speed = 0;
  src->photo_settings.ev_compensation = 0.0;
  src->photo_settings.tone_mode = GST_PHOTOGRAPHY_COLOR_TONE_MODE_NORMAL;
  src->photo_settings.scene_mode = GST_PHOTOGRAPHY_SCENE_MODE_MANUAL;
  src->photo_settings.aperture = 0;
  src->photo_settings.exposure_time = 0;
  src->photo_settings.flicker_mode = GST_PHOTOGRAPHY_FLICKER_REDUCTION_AUTO;
  src->photo_settings.noise_reduction = 0;
  src->photo_settings.exposure_mode = GST_PHOTOGRAPHY_EXPOSURE_MODE_AUTO;
  src->photo_settings.color_temperature = 0;
  src->photo_settings.analog_gain = 0.0f;
  src->photo_settings.lens_focus = 0.0f;
  src->photo_settings.min_exposure_time = 0;
  src->photo_settings.max_exposure_time = 0;
}

void
gst_photo_iface_add_properties (GObjectClass * gobject_class)
{
  g_object_class_override_property (gobject_class, PROP_FLASH_MODE,
      GST_PHOTOGRAPHY_PROP_FLASH_MODE);

  g_object_class_override_property (gobject_class, PROP_FOCUS_MODE,
      GST_PHOTOGRAPHY_PROP_FOCUS_MODE);

  g_object_class_override_property (gobject_class, PROP_WB_MODE,
      GST_PHOTOGRAPHY_PROP_WB_MODE);

  g_object_class_override_property (gobject_class, PROP_ZOOM,
      GST_PHOTOGRAPHY_PROP_ZOOM);

  g_object_class_override_property (gobject_class, PROP_ISO_SPEED,
      GST_PHOTOGRAPHY_PROP_ISO_SPEED);

  g_object_class_override_property (gobject_class, PROP_EV_COMP,
      GST_PHOTOGRAPHY_PROP_EV_COMP);

  g_object_class_override_property (gobject_class, PROP_COLOR_TONE,
      GST_PHOTOGRAPHY_PROP_COLOR_TONE);

  g_object_class_override_property (gobject_class, PROP_SCENE_MODE,
      GST_PHOTOGRAPHY_PROP_SCENE_MODE);

  g_object_class_override_property (gobject_class, PROP_NOISE_REDUCTION,
      GST_PHOTOGRAPHY_PROP_NOISE_REDUCTION);

  g_object_class_override_property (gobject_class, PROP_CAPABILITIES,
      GST_PHOTOGRAPHY_PROP_CAPABILITIES);

  g_object_class_override_property (gobject_class, PROP_APERTURE,
      GST_PHOTOGRAPHY_PROP_APERTURE);

  g_object_class_override_property (gobject_class, PROP_EXPOSURE,
      GST_PHOTOGRAPHY_PROP_EXPOSURE_TIME);

  g_object_class_override_property (gobject_class, PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
      GST_PHOTOGRAPHY_PROP_IMAGE_CAPTURE_SUPPORTED_CAPS);

  g_object_class_override_property (gobject_class, PROP_IMAGE_PREVIEW_SUPPORTED_CAPS,
      GST_PHOTOGRAPHY_PROP_IMAGE_PREVIEW_SUPPORTED_CAPS);

  g_object_class_override_property (gobject_class, PROP_FLICKER_MODE,
      GST_PHOTOGRAPHY_PROP_FLICKER_MODE);

  g_object_class_override_property (gobject_class, PROP_COLOR_TEMPERATURE,
      GST_PHOTOGRAPHY_PROP_COLOR_TEMPERATURE);

  g_object_class_override_property (gobject_class, PROP_WHITE_POINT,
      GST_PHOTOGRAPHY_PROP_WHITE_POINT);

  g_object_class_override_property (gobject_class, PROP_ANALOG_GAIN,
      GST_PHOTOGRAPHY_PROP_ANALOG_GAIN);

  g_object_class_install_property (gobject_class, PROP_EXPOSURE_MODE,
      g_param_spec_enum (GST_PHOTOGRAPHY_PROP_EXPOSURE_MODE,
          "Exposure mode property",
          "Exposure mode determines how exposure time is derived",
          GST_TYPE_PHOTOGRAPHY_EXPOSURE_MODE,
          GST_PHOTOGRAPHY_EXPOSURE_MODE_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_override_property (gobject_class, PROP_LENS_FOCUS,
      GST_PHOTOGRAPHY_PROP_LENS_FOCUS);

  g_object_class_override_property (gobject_class, PROP_MIN_EXPOSURE_TIME,
      GST_PHOTOGRAPHY_PROP_MIN_EXPOSURE_TIME);

  g_object_class_override_property (gobject_class, PROP_MAX_EXPOSURE_TIME,
      GST_PHOTOGRAPHY_PROP_MAX_EXPOSURE_TIME);

  g_object_class_install_property (gobject_class, PROP_DETECT_FACES,
      g_param_spec_boolean ("detect-faces",
          "Face detection property",
          "Determines whether the camera will tag buffers with the location of "
          "detected faces",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
gst_photo_iface_settings_to_params (GstDroidCamSrc * src,
    GstPhotographySettings * settings)
{
  GST_DEBUG_OBJECT (src, "settings to params");

  gst_photo_iface_set_flash_mode_unlocked (src, settings->flash_mode, FALSE);
  gst_photo_iface_set_focus_mode_unlocked (src, settings->focus_mode, FALSE);
  gst_photo_iface_set_white_balance_mode_unlocked (src, settings->wb_mode,
      FALSE);
  gst_photo_iface_set_zoom_unlocked (src, settings->zoom, FALSE);
  gst_photo_iface_set_iso_speed_unlocked (src, settings->iso_speed, FALSE);
  gst_photo_iface_set_ev_compensation_unlocked (src, settings->ev_compensation,
      FALSE);
  gst_photo_iface_set_color_tone_mode_unlocked (src, settings->tone_mode,
      FALSE);
  gst_photo_iface_set_scene_mode_unlocked (src, settings->scene_mode, FALSE);
  gst_photo_iface_set_flicker_mode_unlocked (src, settings->flicker_mode,
      FALSE);
  gst_photo_iface_set_exposure_mode_unlocked (src, settings->exposure_mode,
      FALSE);
  gst_photo_iface_set_face_detection_enabled_unlocked (src, src->detect_faces,
      FALSE);
}

void
gst_photo_iface_init_ev_comp (GstDroidCamSrc * src)
{
  const gchar *value = camera_params_get (src->camera_params,
      "exposure-compensation-step");
  if (value) {
    src->ev_comp_step = atof (value);
  } else {
    src->ev_comp_step = 1.0f;
  }

  GST_DEBUG_OBJECT (src, "init ev comp: step size = %f", src->ev_comp_step);
}

gboolean
gst_photo_iface_get_property (GstDroidCamSrc * src, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  gboolean ret = TRUE;

  GST_OBJECT_LOCK (src);
  switch (prop_id) {
    case PROP_FLASH_MODE:
      g_value_set_enum (value, src->photo_settings.flash_mode);
      break;

    case PROP_FOCUS_MODE:
      g_value_set_enum (value, src->photo_settings.focus_mode);
      break;

    case PROP_WB_MODE:
      g_value_set_enum (value, src->photo_settings.wb_mode);
      break;

    case PROP_ZOOM:
      g_value_set_float (value, src->photo_settings.zoom);
      break;

    case PROP_ISO_SPEED:
      g_value_set_uint (value, src->photo_settings.iso_speed);
      break;

    case PROP_EV_COMP:
      g_value_set_float (value, src->photo_settings.ev_compensation);
      break;

    case PROP_COLOR_TONE:
      g_value_set_enum (value, src->photo_settings.tone_mode);
      break;

    case PROP_SCENE_MODE:
      g_value_set_enum (value, src->photo_settings.scene_mode);
      break;

    case PROP_NOISE_REDUCTION:
      g_value_set_enum (value, src->photo_settings.noise_reduction);
      break;

    case PROP_CAPABILITIES:
      g_value_set_enum (value, gst_photo_iface_get_capabilities_unlocked (src));
      break;

    case PROP_APERTURE:
      g_value_set_uint (value, src->photo_settings.aperture);
      break;

    case PROP_EXPOSURE:
      g_value_set_uint (value, src->photo_settings.exposure_time);
      break;

    case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:
      gst_value_set_caps (value, gst_img_src_pad_get_supported_caps_unlocked
          (src));
      break;

    case PROP_IMAGE_PREVIEW_SUPPORTED_CAPS:
      gst_value_set_caps (value, gst_vf_src_pad_get_supported_caps_unlocked
          (src));
      break;

    case PROP_FLICKER_MODE:
      g_value_set_enum (value, src->photo_settings.flicker_mode);
      break;

    case PROP_COLOR_TEMPERATURE:
      g_value_set_uint (value, src->photo_settings.color_temperature);
      break;

    case PROP_WHITE_POINT:
      g_value_take_boxed (value, g_array_new (FALSE, FALSE, sizeof(guint)));
      break;

    case PROP_ANALOG_GAIN:
      g_value_set_float (value, src->photo_settings.analog_gain);
      break;

    case PROP_EXPOSURE_MODE:
      g_value_set_enum (value, src->photo_settings.exposure_mode);
      break;

    case PROP_LENS_FOCUS:
      g_value_set_enum (value, src->photo_settings.lens_focus);
      break;

    case PROP_MIN_EXPOSURE_TIME:
      g_value_set_uint (value, src->photo_settings.min_exposure_time);
      break;

    case PROP_MAX_EXPOSURE_TIME:
      g_value_set_uint (value, src->photo_settings.max_exposure_time);
      break;

    case PROP_DETECT_FACES:
      g_value_set_boolean (value, src->detect_faces);
      break;

    default:
      ret = FALSE;
      break;
  }
  GST_OBJECT_UNLOCK (src);

  return ret;
}

gboolean
gst_photo_iface_set_property (GstDroidCamSrc * src, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  gboolean ret = FALSE;

  GST_OBJECT_LOCK (src);
  switch (prop_id) {
    case PROP_FLASH_MODE:
      ret = gst_photo_iface_set_flash_mode_unlocked (src,
          g_value_get_enum (value), TRUE);
      break;

    case PROP_FOCUS_MODE:
      ret = gst_photo_iface_set_focus_mode_unlocked (src,
          g_value_get_enum (value), TRUE);
      break;

    case PROP_WB_MODE:
      ret = gst_photo_iface_set_white_balance_mode_unlocked (src,
          g_value_get_enum (value), TRUE);
      break;

    case PROP_ZOOM:
      ret = gst_photo_iface_set_zoom_unlocked (src, g_value_get_float (value),
          TRUE);
      break;

    case PROP_ISO_SPEED:
      ret = gst_photo_iface_set_iso_speed_unlocked (src,
          g_value_get_uint (value), TRUE);
      break;

    case PROP_EV_COMP:
      ret = gst_photo_iface_set_ev_compensation_unlocked (src,
          g_value_get_float (value), TRUE);
      break;

    case PROP_COLOR_TONE:
      ret = gst_photo_iface_set_color_tone_mode_unlocked (src,
          g_value_get_enum (value), TRUE);
      break;

    case PROP_SCENE_MODE:
      ret = gst_photo_iface_set_scene_mode_unlocked (src,
          g_value_get_enum (value), TRUE);
      break;

    case PROP_FLICKER_MODE:
      ret = gst_photo_iface_set_flicker_mode_unlocked (src,
          g_value_get_enum (value), TRUE);
      break;

    case PROP_EXPOSURE_MODE:
      ret = gst_photo_iface_set_exposure_mode_unlocked (src,
          g_value_get_enum (value), TRUE);
      break;

    case PROP_DETECT_FACES:
      ret = gst_photo_iface_set_face_detection_enabled_unlocked (src,
          g_value_get_boolean (value), TRUE);
      break;
  }
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_get_flash_mode (GstPhotography * photo,
    GstPhotographyFlashMode * flash)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *flash = src->photo_settings.flash_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_flash_mode (GstPhotography * photo,
    GstPhotographyFlashMode flash)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret = gst_photo_iface_set_flash_mode_unlocked (src, flash, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

void
gst_photo_iface_update_flash_mode (GstDroidCamSrc * src)
{
  GST_OBJECT_LOCK (src);
  gst_photo_iface_set_flash_mode_unlocked (src, src->photo_settings.flash_mode,
      TRUE);
  GST_OBJECT_UNLOCK (src);
}

static gboolean
gst_photo_iface_set_flash_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyFlashMode flash, gboolean commit)
{
  gboolean ret = gst_photo_iface_set_enum_parameter_unlocked (src,
      src->settings->flash_mode, "flash-mode", flash, commit);
  if (ret) {
    src->photo_settings.flash_mode = flash;
  }
  return ret;
}

static gboolean
gst_photo_iface_get_focus_mode (GstPhotography * photo,
    GstPhotographyFocusMode * focus)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *focus = src->photo_settings.focus_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_focus_mode (GstPhotography * photo,
    GstPhotographyFocusMode focus)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret = gst_photo_iface_set_focus_mode_unlocked (src, focus, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_set_focus_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyFocusMode focus, gboolean commit)
{
  gboolean ret;
  GHashTable *table;

  if (src->mode == MODE_IMAGE) {
    table = src->settings->image_focus_mode;
  } else {
    table = src->settings->video_focus_mode;
  }

  ret = gst_photo_iface_set_enum_parameter_unlocked (src, table, "focus-mode",
      focus, commit);
  if (ret) {
    src->photo_settings.focus_mode = focus;
  }
  return ret;
}

void
gst_photo_iface_update_focus_mode (GstDroidCamSrc * src)
{
  GST_OBJECT_LOCK (src);
  gst_photo_iface_set_focus_mode_unlocked (src, src->photo_settings.focus_mode,
      TRUE);
  GST_OBJECT_UNLOCK (src);
}

static gboolean
gst_photo_iface_get_white_balance_mode (GstPhotography * photo,
    GstPhotographyWhiteBalanceMode * wb)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *wb = src->photo_settings.wb_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_white_balance_mode (GstPhotography * photo,
    GstPhotographyWhiteBalanceMode wb)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret = gst_photo_iface_set_white_balance_mode_unlocked (src, wb, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_set_white_balance_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyWhiteBalanceMode wb, gboolean commit)
{
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);
  gboolean ret;

  if (wb == GST_PHOTOGRAPHY_WB_MODE_MANUAL) {
    ret = TRUE;
    if (src->camera_params) {
      camera_params_set (src->camera_params, "auto-whitebalance-lock", "true");
    }
  } else {
    ret = gst_photo_iface_set_enum_parameter_unlocked (src,
      src->settings->white_balance_mode, "whitebalance", wb, FALSE);
    if (ret && src->camera_params) {
      camera_params_set (src->camera_params, "auto-whitebalance-lock", "false");
    }
  }

  if (ret && commit && src->camera_params) {
    GST_OBJECT_UNLOCK (src);
    ret = klass->set_camera_params (src);
    GST_OBJECT_LOCK (src);
  }

  if (ret) {
    src->photo_settings.wb_mode = wb;
  }
  return ret;
}

static gboolean
gst_photo_iface_get_zoom (GstPhotography * photo, gfloat * zoom)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *zoom = src->photo_settings.zoom;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_zoom (GstPhotography * photo, gfloat zoom)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret = gst_photo_iface_set_zoom_unlocked (src, zoom, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_set_zoom_unlocked (GstDroidCamSrc * src, gfloat zoom,
    gboolean commit)
{
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);
  gint zoom_ratio = round (zoom * 100);
  gint index = 0;
  gint index_difference = INT_MAX;
  gint i;
  gboolean ret = TRUE;

  if (!src->camera_params) {
    goto done;
  }

  if (!src->zoom_ratios) {
    ret = FALSE;
    goto done;
  }

  for (i = 0; i < src->num_zoom_ratios; ++i) {
    int difference = abs (src->zoom_ratios[i] - zoom_ratio);
    if (difference >= index_difference) {
        break;
    }
    index = i;
    index_difference = difference;
  }

  camera_params_set_int (src->camera_params, "zoom", index);

  if (commit) {
    GST_OBJECT_UNLOCK (src);
    ret = klass->set_camera_params (src);
    GST_OBJECT_LOCK (src);
  }

done:
  if (ret) {
    src->photo_settings.zoom = zoom;
  }

  return ret;
}

static gboolean
gst_photo_iface_get_iso_speed (GstPhotography * photo, guint * iso)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *iso = src->photo_settings.iso_speed;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_iso_speed (GstPhotography * photo, guint iso)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret = gst_photo_iface_set_iso_speed_unlocked (src, iso, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_set_iso_speed_unlocked (GstDroidCamSrc * src, guint iso,
    gboolean commit)
{
  gboolean ret = gst_photo_iface_set_enum_parameter_unlocked (src,
      src->settings->iso_speed, "iso", iso, commit);
  if (ret) {
    src->photo_settings.iso_speed = iso;
  }
  return ret;
}

static gboolean
gst_photo_iface_get_ev_compensation (GstPhotography * photo, gfloat * ev)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *ev = src->photo_settings.ev_compensation;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_ev_compensation (GstPhotography * photo, gfloat ev)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret = gst_photo_iface_set_ev_compensation_unlocked (src, ev, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_set_ev_compensation_unlocked (GstDroidCamSrc * src, gfloat ev,
    gboolean commit)
{
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);
  gboolean ret = TRUE;
  gint compensation = round (ev / src->ev_comp_step);

  GST_DEBUG_OBJECT (src, "set exposure-compensation to %f (%i)", ev,
      compensation);

  if (!src->camera_params) {
    goto done;
  }

  camera_params_set_int (src->camera_params, "exposure-compensation",
      round (ev / src->ev_comp_step));

  if (commit) {
    GST_OBJECT_UNLOCK (src);
    ret = klass->set_camera_params (src);;
    GST_OBJECT_LOCK (src);
  }

done:
  if (ret) {
    src->photo_settings.ev_compensation = ev;
  }

  return ret;
}

static void
gst_photo_iface_set_autofocus (GstPhotography * photo, gboolean on)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);
  GstPhotographyFocusMode mode;

  GST_OBJECT_LOCK (src);
  mode = src->photo_settings.focus_mode;
  GST_OBJECT_UNLOCK (src);

  switch (mode) {
    case GST_PHOTOGRAPHY_FOCUS_MODE_AUTO:
    case GST_PHOTOGRAPHY_FOCUS_MODE_MACRO:
    case GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL:
    case GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_EXTENDED:
      break;

    default:
      GST_WARNING_OBJECT (src,
          "Autofocus is not allowed with focus mode %d (%s)", mode,
          gst_camera_settings_find_droid (src->settings->image_focus_mode, mode));
      break;
  }

  if (on) {
    GST_DEBUG_OBJECT (src, "starting autofocus");
    gst_droid_cam_src_start_autofocus (src);
  } else {
    GST_DEBUG_OBJECT (src, "stopping autofocus");
    gst_droid_cam_src_stop_autofocus (src);
  }
}

/* Color tone mode */
static gboolean
gst_photo_iface_get_color_tone_mode (GstPhotography * photo,
    GstPhotographyColorToneMode * mode)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *mode = src->photo_settings.tone_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_color_tone_mode (GstPhotography * photo,
    GstPhotographyColorToneMode mode)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret = gst_photo_iface_set_color_tone_mode_unlocked (src, mode, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_set_color_tone_mode_unlocked (GstDroidCamSrc *src,
    GstPhotographyColorToneMode mode, gboolean commit)
{
  gboolean ret = gst_photo_iface_set_enum_parameter_unlocked (src,
      src->settings->tone_mode, "effect", mode, commit);
  if (ret) {
    src->photo_settings.tone_mode = mode;
  }
  return ret;
}

/* Scene mode */
static gboolean
gst_photo_iface_get_scene_mode (GstPhotography * photo,
    GstPhotographySceneMode * mode)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *mode = src->photo_settings.scene_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_scene_mode (GstPhotography * photo,
    GstPhotographySceneMode mode)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret =  gst_photo_iface_set_scene_mode_unlocked (src, mode, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_set_scene_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographySceneMode mode, gboolean commit)
{
  if (gst_photo_iface_set_enum_parameter_unlocked (src,
      src->settings->scene_mode, "scene-mode", mode, commit)) {
    src->photo_settings.scene_mode = mode;
    return TRUE;
  }
  return FALSE;
}

/* Flicker reduction mode */
static gboolean
gst_photo_iface_get_flicker_mode (GstPhotography * photo,
    GstPhotographyFlickerReductionMode * mode)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *mode = src->photo_settings.flicker_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_flicker_mode (GstPhotography * photo,
    GstPhotographyFlickerReductionMode mode)
{
  gboolean ret;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  ret = gst_photo_iface_set_flicker_mode_unlocked (src, mode, TRUE);
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_photo_iface_set_flicker_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyFlickerReductionMode mode, gboolean commit)
{
  gboolean ret = gst_photo_iface_set_enum_parameter_unlocked (src,
      src->settings->flicker_mode, "antibanding", mode, commit);
  if (ret) {
    src->photo_settings.flicker_mode = mode;
  }
  return ret;
}

/* Auto exposure */
static gboolean gst_photo_iface_set_exposure_mode_unlocked (GstDroidCamSrc * src,
    GstPhotographyExposureMode mode, gboolean commit)
{
  gboolean ret = gst_photo_iface_set_enum_parameter_unlocked (src,
      src->settings->exposure_mode, "auto-exposure-lock", mode, commit);
  if (ret) {
    src->photo_settings.exposure_mode = mode;
  }
  return ret;
}

/* Face detection */
static gboolean
gst_photo_iface_set_face_detection_enabled_unlocked (GstDroidCamSrc * src,
    gboolean enabled, gboolean commit)
{
  gboolean ret = gst_photo_iface_set_enum_parameter_unlocked (src,
      src->settings->face_detection, "face-detection", (enabled ? TRUE : FALSE),
      commit);
  if (ret) {
    GST_PAD_STREAM_LOCK (src->vfsrc);
    src->detect_faces = enabled;
    if (src->detect_faces) {
      src->num_detected_faces = 0;
    }
    GST_PAD_STREAM_UNLOCK (src->vfsrc);
  }
  return ret;
}

/* Capabilities */
static GstPhotographyCaps
gst_photo_iface_get_capabilities (GstPhotography * photo)
{
  GstPhotographyCaps caps;
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  caps = gst_photo_iface_get_capabilities_unlocked (src);
  GST_OBJECT_UNLOCK (src);

  return caps;
}

static  GstPhotographyCaps
gst_photo_iface_get_capabilities_unlocked (GstDroidCamSrc *src)
{
  guint caps = 0;
  const gchar *value;

  if (!src->camera_params) {
    return caps;
  }

  value = camera_params_get (src->camera_params, "exposure-step");
  if (value) {
    caps |= GST_PHOTOGRAPHY_CAPS_EV_COMP;
  }

  value = camera_params_get (src->camera_params, "iso-values");
  if (value && strcmp (value, "auto") != 0) {
    caps |= GST_PHOTOGRAPHY_CAPS_ISO_SPEED;
  }

  value = camera_params_get (src->camera_params, "whitebalance-values");
  if (value && strcmp (value, "auto") != 0) {
    caps |= GST_PHOTOGRAPHY_CAPS_WB_MODE;
  }

  value = camera_params_get (src->camera_params, "effect-values");
  if (value && strcmp (value, "none") != 0) {
    caps |= GST_PHOTOGRAPHY_CAPS_TONE;
  }

  value = camera_params_get (src->camera_params, "scene-mode-values");
  if (value && strcmp (value, "auto") != 0) {
    caps |= GST_PHOTOGRAPHY_CAPS_SCENE;
  }

  value = camera_params_get (src->camera_params, "flash-mode-values");
  if (value && strcmp (value, "off") != 0) {
    caps |= GST_PHOTOGRAPHY_CAPS_FLASH;
  }

  value = camera_params_get (src->camera_params, "zoom-supported");
  if (strcmp (value, "true") == 0) {
    caps |= GST_PHOTOGRAPHY_CAPS_ZOOM;
  }

  value = camera_params_get (src->camera_params, "focus-mode-values");
  if (value && strcmp (value, "infinity") != 0) {
    caps |= GST_PHOTOGRAPHY_CAPS_FOCUS;
  }

  value = camera_params_get (src->camera_params, "antibanding-values");
  if (value && strcmp (value, "auto") != 0) {
    caps |= GST_PHOTOGRAPHY_CAPS_FLICKER_REDUCTION;
  }

  return caps;
}

static gboolean
gst_photo_iface_get_config (GstPhotography * photo,
    GstPhotographySettings * config)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_OBJECT_LOCK (src);
  *config = src->photo_settings;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_config (GstPhotography * photo,
    GstPhotographySettings * config)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  gboolean ret;

  GST_OBJECT_LOCK (src);

  gst_photo_iface_settings_to_params (src, config);

  if (src->camera_params) {
    ret = klass->set_camera_params (src);
  } else {
    ret = TRUE;
  }

  GST_OBJECT_UNLOCK (src);

  return ret;
}
