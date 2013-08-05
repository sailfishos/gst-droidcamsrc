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

#include "enums.h"

GType
gst_droid_cam_src_camera_device_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GEnumValue values[] = {
      {GST_DROID_CAM_SRC_CAMERA_DEVICE_PRIMARY,
          "GST_DROID_CAM_SRC_CAMERA_DEVICE_PRIMARY", "primary"},
      {GST_DROID_CAM_SRC_CAMERA_DEVICE_SECONDARY,
          "GST_DROID_CAM_SRC_CAMERA_DEVICE_SECONDARY", "secondary"},
      {0, NULL, NULL}
    };

    type =
        g_enum_register_static (g_intern_static_string
        ("GstDroidCamSrcCameraDevice"), values);
  }

  return type;
}

GType
gst_droid_cam_src_sensor_mount_angle_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GEnumValue values[] = {
      {GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN,
          "GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN", "unknown"},
      {GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_0,
          "GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_0", "0"},
      {GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_90,
          "GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_90", "90"},
      {GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_180,
          "GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_180", "180"},
      {GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_270,
          "GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_270", "270"},
      {0, NULL, NULL}
    };

    type =
        g_enum_register_static (g_intern_static_string
        ("GstDroidCamSrcSensorMountAngle"), values);
  }

  return type;
}
