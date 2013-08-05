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

#ifndef __ENUMS_H__
#define __ENUMS_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_DROID_CAM_SRC_CAMERA_DEVICE gst_droid_cam_src_camera_device_get_type ()
#define GST_TYPE_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE gst_droid_cam_src_sensor_mount_angle_get_type ()

GType gst_droid_cam_src_camera_device_get_type (void);
GType gst_droid_cam_src_sensor_mount_angle_get_type (void);

typedef enum
{
  GST_DROID_CAM_SRC_CAMERA_DEVICE_PRIMARY = 0,
  GST_DROID_CAM_SRC_CAMERA_DEVICE_SECONDARY = 1,
} GstDroidCamSrcCameraDevice;

typedef enum {
  GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_UNKNOWN = -1,
  GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_0 = 0,
  GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_90 = 90,
  GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_180 = 180,
  GST_DROID_CAM_SRC_SENSOR_MOUNT_ANGLE_270 = 270,
} GstDroidCamSrcSensorMountAngle;

G_END_DECLS

#endif /* __ENUMS_H__ */
