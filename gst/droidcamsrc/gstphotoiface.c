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
#include <gst/interfaces/photography.h>
#include <gst/basecamerabinsrc/gstbasecamerasrc.h>
#undef GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include "gstdroidcamsrc.h"
#include "cameraparams.h"

GST_DEBUG_CATEGORY_STATIC (droidphoto_debug);
#define GST_CAT_DEFAULT droidphoto_debug

static void
gst_photo_iface_implements_interface_init (GstImplementsInterfaceClass * klass);
static gboolean
gst_photo_iface_implements_iface_supported (GstImplementsInterface * iface,
    GType iface_type);
static void gst_photo_iface_photo_interface_init (GstPhotographyInterface *
    iface);

/* Flash */
static gboolean gst_photo_iface_get_flash_mode (GstPhotography * photo,
    GstFlashMode * flash);
static gboolean gst_photo_iface_set_flash_mode (GstPhotography * photo,
    GstFlashMode flash);
static GstFlashMode _gst_photo_iface_get_flash_mode (GstDroidCamSrc * src);
static gboolean _gst_photo_iface_set_flash_mode (GstDroidCamSrc * src,
    GstFlashMode flash, gboolean commit);

/* Focus */
static gboolean gst_photo_iface_get_focus_mode (GstPhotography * photo,
    GstFocusMode * focus);
static gboolean gst_photo_iface_set_focus_mode (GstPhotography * photo,
    GstFocusMode focus);
static GstFocusMode _gst_photo_iface_get_focus_mode (GstDroidCamSrc * src);
static gboolean _gst_photo_iface_set_focus_mode (GstDroidCamSrc * src,
    GstFocusMode focus, gboolean commit);

/* White balance */
static gboolean gst_photo_iface_get_white_balance_mode (GstPhotography * photo,
    GstWhiteBalanceMode * wb);
static gboolean gst_photo_iface_set_white_balance_mode (GstPhotography * photo,
    GstWhiteBalanceMode wb);
static GstWhiteBalanceMode _gst_photo_iface_get_white_balance_mode
    (GstDroidCamSrc * src);
static gboolean _gst_photo_iface_set_white_balance_mode (GstDroidCamSrc * src,
    GstWhiteBalanceMode wb, gboolean commit);

/* zoom */
static gboolean gst_photo_iface_get_zoom (GstPhotography * photo,
    gfloat * zoom);
static gboolean gst_photo_iface_set_zoom (GstPhotography * photo, gfloat zoom);
static gfloat _gst_photo_iface_get_zoom (GstDroidCamSrc * src);
static gboolean _gst_photo_iface_set_zoom (GstDroidCamSrc * src, gfloat zoom,
    gboolean commit);

/* Auto focus */
static void gst_photo_iface_set_autofocus (GstPhotography * photo, gboolean on);

void
gst_photo_iface_init (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_photo_iface_implements_interface_init,
    NULL,
    NULL,
  };

  static const GInterfaceInfo photo_iface_info = {
    (GInterfaceInitFunc) gst_photo_iface_photo_interface_init,
    NULL,
    NULL,
  };

  GST_DEBUG_CATEGORY_INIT (droidphoto_debug, "droidphoto", 0,
      "Android camera source photography interface");

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);

  g_type_add_interface_static (type, GST_TYPE_PHOTOGRAPHY, &photo_iface_info);
}

static void
gst_photo_iface_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_photo_iface_implements_iface_supported;
}

static gboolean
gst_photo_iface_implements_iface_supported (GstImplementsInterface * iface,
    GType iface_type)
{
  return iface_type == GST_TYPE_PHOTOGRAPHY;
}

static void
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

  iface->set_autofocus = gst_photo_iface_set_autofocus;

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

  // TODO: more
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

  // TODO: more
}

void
gst_photo_iface_settings_to_params (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "settings to params");

  _gst_photo_iface_set_flash_mode (src, src->photo_settings.flash_mode, FALSE);
  _gst_photo_iface_set_focus_mode (src, src->photo_settings.focus_mode, FALSE);
  _gst_photo_iface_set_white_balance_mode (src, src->photo_settings.wb_mode,
      FALSE);
  _gst_photo_iface_set_zoom (src, src->photo_settings.zoom, FALSE);

  // TODO: more
}

gboolean
gst_photo_iface_get_property (GstDroidCamSrc * src, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_FLASH_MODE:
      g_value_set_enum (value, _gst_photo_iface_get_flash_mode (src));
      return TRUE;

    case PROP_FOCUS_MODE:
      g_value_set_enum (value, _gst_photo_iface_get_focus_mode (src));
      return TRUE;

    case PROP_WB_MODE:
      g_value_set_enum (value, _gst_photo_iface_get_white_balance_mode (src));
      return TRUE;

    case PROP_ZOOM:
      g_value_set_float (value, _gst_photo_iface_get_zoom (src));
      return TRUE;
  }

  // TODO: more

  return FALSE;
}

gboolean
gst_photo_iface_set_property (GstDroidCamSrc * src, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_FLASH_MODE:
      _gst_photo_iface_set_flash_mode (src, g_value_get_enum (value), TRUE);
      return TRUE;

    case PROP_FOCUS_MODE:
      _gst_photo_iface_set_focus_mode (src, g_value_get_enum (value), TRUE);
      return TRUE;

    case PROP_WB_MODE:
      _gst_photo_iface_set_white_balance_mode (src, g_value_get_enum (value),
          TRUE);
      return TRUE;

    case PROP_ZOOM:
      _gst_photo_iface_set_zoom (src, g_value_get_float (value), TRUE);
      return TRUE;
  }

  // TODO: more

  return FALSE;
}

static gboolean
gst_photo_iface_get_flash_mode (GstPhotography * photo, GstFlashMode * flash)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_DEBUG_OBJECT (src, "get flash mode");

  *flash = _gst_photo_iface_get_flash_mode (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_flash_mode (GstPhotography * photo, GstFlashMode flash)
{
  return _gst_photo_iface_set_flash_mode (GST_DROID_CAM_SRC (photo), flash,
      TRUE);
}

static GstFlashMode
_gst_photo_iface_get_flash_mode (GstDroidCamSrc * src)
{
  GstFlashMode flash;

  GST_OBJECT_LOCK (src);

  flash = src->photo_settings.flash_mode;

  GST_DEBUG_OBJECT (src, "returning flash mode %i", flash);

  GST_OBJECT_UNLOCK (src);

  return flash;
}

static gboolean
_gst_photo_iface_set_flash_mode (GstDroidCamSrc * src, GstFlashMode flash,
    gboolean commit)
{
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  const char *val =
      gst_camera_settings_find_droid (src->settings->flash_mode, flash);

  GST_DEBUG_OBJECT (src, "set flash mode to %i (%s)", flash, val);

  if (!val) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "storing flash mode %i", flash);

  GST_OBJECT_LOCK (src);
  src->photo_settings.flash_mode = flash;

  if (!src->camera_params) {
    GST_OBJECT_UNLOCK (src);
    return TRUE;
  }

  camera_params_set (src->camera_params, "flash-mode", val);
  GST_OBJECT_UNLOCK (src);

  if (!commit) {
    return TRUE;
  }

  return klass->set_camera_params (src);
}

static gboolean
gst_photo_iface_get_focus_mode (GstPhotography * photo, GstFocusMode * focus)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_DEBUG_OBJECT (src, "get focus mode");

  *focus = _gst_photo_iface_get_focus_mode (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_focus_mode (GstPhotography * photo, GstFocusMode focus)
{
  return _gst_photo_iface_set_focus_mode (GST_DROID_CAM_SRC (photo), focus,
      TRUE);
}

static GstFocusMode
_gst_photo_iface_get_focus_mode (GstDroidCamSrc * src)
{
  GstFocusMode focus;

  GST_OBJECT_LOCK (src);

  focus = src->photo_settings.focus_mode;

  GST_DEBUG_OBJECT (src, "returning focus mode %i", focus);

  GST_OBJECT_UNLOCK (src);

  return focus;
}

static gboolean
_gst_photo_iface_set_focus_mode (GstDroidCamSrc * src,
    GstFocusMode focus, gboolean commit)
{
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  const char *val =
      gst_camera_settings_find_droid (src->settings->focus_mode, focus);

  GST_DEBUG_OBJECT (src, "set focus mode to %i (%s)", focus, val);

  if (!val) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "storing focus mode %i", focus);

  GST_OBJECT_LOCK (src);
  src->photo_settings.focus_mode = focus;

  if (!src->camera_params) {
    GST_OBJECT_UNLOCK (src);
    return TRUE;
  }

  /* Special handling for this focus mode */
  if (!strcmp (val, "continuous")) {
    if (src->mode == MODE_IMAGE) {
      camera_params_set (src->camera_params, "focus-mode",
          "continuous-picture");
    } else {
      camera_params_set (src->camera_params, "focus-mode", "continuous-video");
    }
  } else {
    camera_params_set (src->camera_params, "focus-mode", val);
  }

  GST_OBJECT_UNLOCK (src);

  if (!commit) {
    return TRUE;
  }

  return klass->set_camera_params (src);
}

void
gst_photo_iface_update_focus_mode (GstDroidCamSrc * src)
{
  _gst_photo_iface_set_focus_mode (src, src->photo_settings.focus_mode, TRUE);
}

static gboolean
gst_photo_iface_get_white_balance_mode (GstPhotography * photo,
    GstWhiteBalanceMode * wb)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_DEBUG_OBJECT (src, "get white balance mode");

  *wb = _gst_photo_iface_get_white_balance_mode (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_white_balance_mode (GstPhotography * photo,
    GstWhiteBalanceMode wb)
{
  return _gst_photo_iface_set_white_balance_mode (GST_DROID_CAM_SRC (photo), wb,
      TRUE);
}

static GstWhiteBalanceMode _gst_photo_iface_get_white_balance_mode
    (GstDroidCamSrc * src)
{
  GstWhiteBalanceMode wb;

  GST_OBJECT_LOCK (src);

  wb = src->photo_settings.wb_mode;

  GST_DEBUG_OBJECT (src, "returning white balance mode %i", wb);

  GST_OBJECT_UNLOCK (src);

  return wb;
}

static gboolean
_gst_photo_iface_set_white_balance_mode (GstDroidCamSrc * src,
    GstWhiteBalanceMode wb, gboolean commit)
{
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  const char *val =
      gst_camera_settings_find_droid (src->settings->white_balance_mode, wb);

  GST_DEBUG_OBJECT (src, "set white balance mode to %i (%s)", wb, val);

  if (!val) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "storing white balance mode %i", wb);

  GST_OBJECT_LOCK (src);
  src->photo_settings.wb_mode = wb;

  if (!src->camera_params) {
    GST_OBJECT_UNLOCK (src);
    return TRUE;
  }

  camera_params_set (src->camera_params, "whitebalance", val);
  GST_OBJECT_UNLOCK (src);

  if (!commit) {
    return TRUE;
  }

  return klass->set_camera_params (src);
}

static gboolean
gst_photo_iface_get_zoom (GstPhotography * photo, gfloat * zoom)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);

  GST_DEBUG_OBJECT (src, "get zoom");

  *zoom = _gst_photo_iface_get_zoom (src);

  return TRUE;
}

static gboolean
gst_photo_iface_set_zoom (GstPhotography * photo, gfloat zoom)
{
  return _gst_photo_iface_set_zoom (GST_DROID_CAM_SRC (photo), zoom, TRUE);
}

static gfloat
_gst_photo_iface_get_zoom (GstDroidCamSrc * src)
{
  gfloat zoom;

  GST_OBJECT_LOCK (src);

  zoom = src->photo_settings.zoom;

  GST_DEBUG_OBJECT (src, "returning zoom %f", zoom);

  GST_OBJECT_UNLOCK (src);

  return zoom;
}

static gboolean
_gst_photo_iface_set_zoom (GstDroidCamSrc * src, gfloat zoom, gboolean commit)
{
  GstDroidCamSrcClass *klass = GST_DROID_CAM_SRC_GET_CLASS (src);
  int droid_val = (zoom * 10) - 10;
  char *val = g_strdup_printf ("%i", droid_val);

  GST_DEBUG_OBJECT (src, "set zoom to %f (%s)", zoom, val);

  GST_OBJECT_LOCK (src);
  src->photo_settings.zoom = zoom;

  if (!src->camera_params) {
    GST_OBJECT_UNLOCK (src);
    g_free (val);
    return TRUE;
  }

  camera_params_set (src->camera_params, "zoom", val);
  g_free (val);

  GST_OBJECT_UNLOCK (src);

  if (!commit) {
    return TRUE;
  }

  return klass->set_camera_params (src);
}

static void
gst_photo_iface_set_autofocus (GstPhotography * photo, gboolean on)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (photo);
  GstFocusMode mode;

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
          gst_camera_settings_find_droid (src->settings->focus_mode, mode));
      break;
  }

  if (on) {
    gst_droid_cam_src_start_autofocus (src);
  } else {
    gst_droid_cam_src_stop_autofocus (src);
  }
}
