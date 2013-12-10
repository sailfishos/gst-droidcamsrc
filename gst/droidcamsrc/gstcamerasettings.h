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

#ifndef __GST_CAMERA_SETTINGS_H__
#define __GST_CAMERA_SETTINGS_H__

#include <glib.h>

typedef struct {
  GHashTable *flash_mode;
  GHashTable *focus_mode;
  GHashTable *white_balance_mode;
  GHashTable *iso_speed;
  GHashTable *colour_tone_mode;
} GstCameraSettings;

GstCameraSettings *gst_camera_settings_new ();
void gst_camera_settings_destroy (GstCameraSettings *settings);
const char *gst_camera_settings_find_droid (GHashTable *table, int key);

#endif /* __GST_CAMERA_SETTINGS_H__ */
