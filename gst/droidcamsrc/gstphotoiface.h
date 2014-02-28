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

#ifndef __GST_PHOTO_IFACE_H__
#define __GST_PHOTO_IFACE_H__

#include <gst/gst.h>
#include "gstdroidcamsrc.h"

G_BEGIN_DECLS

enum
{
  PROP_0,
  PROP_CAMERA_DEVICE,
  PROP_MODE,
  PROP_READY_FOR_CAPTURE,
  PROP_SENSOR_MOUNT_ANGLE,
  PROP_MAX_ZOOM,
  PROP_IMAGE_NOISE_REDUCTION,
  PROP_VIDEO_TORCH,

  /* photography */
  PROP_FLASH_MODE,
  PROP_FOCUS_MODE,
  PROP_WB_MODE,
  PROP_ZOOM,
  PROP_ISO_SPEED,
  PROP_EV_COMP,
  PROP_COLOR_TONE,
  PROP_SCENE_MODE,
  PROP_NOISE_REDUCTION,
  PROP_CAPABILITIES,
  PROP_APERTURE,
  PROP_EXPOSURE,
  PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
  PROP_IMAGE_PREVIEW_SUPPORTED_CAPS,
  PROP_FLICKER_MODE,
  PROP_COLOR_TEMPERATURE,
  PROP_WHITE_POINT,
  PROP_ANALOG_GAIN,
  PROP_EXPOSURE_MODE,
  PROP_LENS_FOCUS,
  PROP_MIN_EXPOSURE_TIME,
  PROP_MAX_EXPOSURE_TIME,

  /* end */
  N_PROPS,
};

void gst_photo_iface_photo_interface_init (GstPhotographyInterface * iface);
void gst_photo_iface_add_properties (GObjectClass * gobject_class);
void gst_photo_iface_init_settings (GstDroidCamSrc * src);
void gst_photo_iface_settings_to_params (GstDroidCamSrc * src,
    GstPhotographySettings * settings);
void gst_photo_iface_init_ev_comp (GstDroidCamSrc * src);

gboolean gst_photo_iface_get_property (GstDroidCamSrc * src, guint prop_id,
				       GValue * value, GParamSpec * pspec);
gboolean gst_photo_iface_set_property (GstDroidCamSrc * src, guint prop_id,
				       const GValue * value, GParamSpec * pspec);

void gst_photo_iface_update_focus_mode (GstDroidCamSrc * src);
void gst_photo_iface_update_flash_mode (GstDroidCamSrc * src);

G_END_DECLS

#endif /* __GST_PHOTO_IFACE_H__ */
