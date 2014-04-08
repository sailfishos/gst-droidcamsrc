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

struct camera_params;

struct camera_params *camera_params_from_string(const char *str);
void camera_params_update (struct camera_params *params, const char *str);
void camera_params_free(struct camera_params *params);
char *camera_params_to_string(struct camera_params *params);
void camera_params_dump(struct camera_params *params);
void camera_params_set(struct camera_params *params, const char *key, const char *val);
GstCaps *camera_params_get_viewfinder_caps (struct camera_params *params);
GstCaps *camera_params_get_capture_caps (struct camera_params *params);
void camera_params_set_viewfinder_size (struct camera_params *params, int width, int height);
void camera_params_set_capture_size (struct camera_params *params, int width, int height);
void camera_params_set_viewfinder_fps (struct camera_params *params, int fps);
void camera_params_set_viewfinder_format (struct camera_params *params, const gchar *format);
GstCaps *camera_params_get_video_caps (struct camera_params *params);
void camera_params_set_video_size (struct camera_params *params, int width, int height);
int camera_params_get_int (struct camera_params *params, const char *key);
void camera_params_set_int(struct camera_params *params, const char *key, int val);

G_END_DECLS

#endif /* __CAMERA_PARAMS_HH__ */
