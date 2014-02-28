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

#ifndef __CAMERA_PARAMS_HH__
#define __CAMERA_PARAMS_HH__

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

GstStructure *camera_params_from_string(const gchar *str);
void camera_params_update (GstStructure *params, const gchar *str);
char *camera_params_to_string(GstStructure *params);

const gchar *camera_params_get (GstStructure *params, const gchar *key);
void camera_params_set (GstStructure *params, const gchar *key, const gchar *val);

int camera_params_get_int (GstStructure *params, const gchar *key);
void camera_params_set_int(GstStructure *params, const gchar *key, int val);

gboolean camera_params_get_resolution (GstStructure *params, const gchar *key,
    gint *width, gint *height);
void camera_params_set_resolution (GstStructure *params, const gchar *key,
    int width, int height);

const gchar *camera_params_get_format (GstStructure *params, const gchar *key);
gboolean camera_params_set_format (GstStructure *params, const gchar *key,
    const gchar *format);

typedef void (*CameraParamsFunc)(const gchar *value, gpointer user_data);
gboolean camera_params_foreach (GstStructure *params, const gchar *key,
    CameraParamsFunc func, gpointer user_data);

GstCaps *camera_params_get_viewfinder_caps (GstStructure *params);
GstCaps *camera_params_get_capture_caps (GstStructure *params);
GstCaps *camera_params_get_video_caps (GstStructure *params);

G_END_DECLS

#endif /* __CAMERA_PARAMS_HH__ */
