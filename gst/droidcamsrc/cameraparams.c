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

#include "cameraparams.h"
#include "gstdroidcamsrc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


typedef struct _ColorFormat
{
  const gchar *param_format;
  const gchar *gstreamer_format;
} ColorFormat;

static const ColorFormat color_format_lookup[] = {
  { "yuv422sp", "NV16" },
  { "yuv420sp", "NV12" },
  { "yuv422i-yuyv", "YUY2" },
  { "yuv420p", "YV12" },
  { "nv12", "NV12" },
  { NULL, NULL }
};

static const gchar *
camera_params_convert_param_to_gstreamer_format(const gchar *param_format)
{
  const ColorFormat *entry;

  for (entry = color_format_lookup; entry->param_format; ++entry) {
    if (strcmp (param_format, entry->param_format) == 0)
      return entry->gstreamer_format;
  }

  return NULL;
}

static void
camera_params_append_format (const gchar *value, gpointer user_data)
{
  GValue *list = user_data;

  const gchar *format = camera_params_convert_param_to_gstreamer_format (value);

  if (format) {
    GValue val = G_VALUE_INIT;
    g_value_init (&val, G_TYPE_STRING);
    g_value_set_string (&val, format);
    gst_value_list_append_value (list, &val);
  }
}

static gboolean
camera_params_get_format_list (GstStructure *params, const gchar *key,
    GValue *list)
{
  return camera_params_foreach (params, key, camera_params_append_format, list);
}

const gchar *
camera_params_get_format (GstStructure *params, const gchar *key)
{
  const gchar *value = gst_structure_get_string (params, key);
  if (!value) {
    return NULL;
  }

  return camera_params_convert_param_to_gstreamer_format (value);
}

gboolean
camera_params_set_format (GstStructure *params, const gchar *key,
    const gchar *format)
{
  const ColorFormat *entry;

  for (entry = color_format_lookup; entry->param_format; ++entry) {
    if (strcmp (format, entry->gstreamer_format) == 0) {
      camera_params_set (params, key, entry->param_format);

      return TRUE;
    }
  }
  return FALSE;
}

static void
camera_params_append_fps (const gchar *value, gpointer user_data)
{
  GValue *list = user_data;

  int fps = atoi(value);

  if (fps) {
    GValue val = G_VALUE_INIT;
    g_value_init (&val, GST_TYPE_FRACTION);
    gst_value_set_fraction (&val, fps, 1);
    gst_value_list_append_value (list, &val);
  }
}

static gboolean
camera_params_get_fps_list (GstStructure *params, const char *key, GValue *list)
{
  return camera_params_foreach (params, key, camera_params_append_fps, list);
}

GstStructure *
camera_params_from_string (const char *str)
{
  GstStructure *params = gst_structure_new_empty ("camera-parameters");

  camera_params_update (params, str);

  return params;
}

static gint
camera_params_string_index_of (const gchar *string, gchar character)
{
  gint index;
  for (index = 0; string[index] != '\0'; ++index) {
    if (string[index] == character) {
      return index;
    }
  }
  return -1;
}

void
camera_params_update (GstStructure *params, const char *str)
{
  char *buffer = strdup(str);
  char *parameter = buffer;
  int length;

  while (TRUE) {
    gchar *key = parameter;

    length = camera_params_string_index_of (parameter, '=');
    if (length == -1) {
      break;
    }

    key[length] = '\0';
    parameter += length + 1;

    length = camera_params_string_index_of (parameter, ';');
    if (length == -1) {
      gst_structure_set (params, key, G_TYPE_STRING, parameter, NULL);
      break;
    } else {
      parameter[length] = '\0';
      gst_structure_set (params, key, G_TYPE_STRING, parameter, NULL);
      parameter += length + 1;
    }
  }

  free (buffer);
}

gboolean
camera_params_foreach (GstStructure *params, const gchar *key,
    CameraParamsFunc func, gpointer user_data)
{
  gchar *values = g_strdup (gst_structure_get_string (params, key));
  gchar *value = values;
  int length;

  if (!values) {
    return FALSE;
  }

  do {
    length = camera_params_string_index_of (value, ',');

    if (length != -1) {
      value[length] = '\0';
    }

    func (value, user_data);

    value += length + 1;
  } while (length != -1);

  g_free (values);

  return TRUE;
}

static gboolean
camera_params_accumulate_params_buffer_size (GQuark field_id,
            const GValue *value, gpointer user_data)
{
  gint *string_length = (gint *)user_data;
  const char *key_string = g_quark_to_string (field_id);
  const char *value_string = g_value_get_string (value);
  *string_length += strlen (key_string) + strlen (value_string) + 2;

  return TRUE;
}

static gboolean
camera_params_construct_params_buffer (GQuark field_id, const GValue *value, gpointer user_data)
{
  char **buffer = (char **)user_data;
  const char *key_string = g_quark_to_string (field_id);
  const char *value_string = g_value_get_string (value);
  gint parameter_length = strlen (key_string) + strlen (value_string) + 2;

  snprintf (*buffer, parameter_length + 1, "%s=%s;", key_string, value_string);

  *buffer += parameter_length;

  return TRUE;
}

char *
camera_params_to_string (GstStructure *params)
{
  char *string, *buffer;
  gint string_length = 0;
  gst_structure_foreach (params, camera_params_accumulate_params_buffer_size,
      &string_length);

  string = (char *)malloc (string_length + 1);
  buffer = string;
  gst_structure_foreach (params, camera_params_construct_params_buffer, &buffer);
  string[string_length - 1] = '\0';

  return string;
}

const gchar *
camera_params_get (GstStructure *params, const gchar *key)
{
  return gst_structure_get_string (params, key);
}

void
camera_params_set (GstStructure *params, const char *key,
    const char *val)
{
  gst_structure_set (params, key, G_TYPE_STRING, val, NULL);
}

int
camera_params_get_int (GstStructure *params, const char *key)
{
  const gchar *value = gst_structure_get_string (params, key);
  if (!value) {
    return 0;
  }

  return atoi (value);
}

void
camera_params_set_int (GstStructure *params, const char *key, int val)
{
  char buffer[32];
  snprintf (buffer, sizeof(buffer), "%d", val);

  camera_params_set (params, key, buffer);
}

void
camera_params_set_resolution (GstStructure *params, const char *key, int width,
    int height)
{
  char buffer[64];
  snprintf (buffer, sizeof(buffer), "%dx%d", width, height);

  camera_params_set (params, key, buffer);
}

static gboolean
camera_params_parse_resolution (const gchar *value, gint *width, gint *height)
{
  gchar *dimensions = g_strdup (value);
  gchar *dimension = dimensions;
  gint length = camera_params_string_index_of (dimension, 'x');

  if (length != -1) {
    dimension[length] = '\0';
    *width = atoi (dimension);

    dimension += length + 1;
    *height = atoi (dimension);
  }

  g_free (dimensions);

  return length != -1;
}

gboolean
camera_params_get_resolution (GstStructure *params, const char *key, gint *width,
    gint *height)
{
  const gchar *value = gst_structure_get_string (params, key);
  if (!value) {
    return FALSE;
  }

  return camera_params_parse_resolution (value, width, height);
}

static void
camera_params_append_video_structure (const gchar *value, gpointer user_data)
{
  GstCaps *caps = user_data;
  gint width, height;

  if (camera_params_parse_resolution (value, &width, &height)) {
      gst_caps_append_structure (caps, gst_structure_new ("video/x-raw",
          "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height,
          NULL));
    return;
  }
}

GstCaps *
camera_params_get_viewfinder_caps (GstStructure *params)
{
  GstCaps *caps = gst_caps_new_empty ();
  GValue fps_list = G_VALUE_INIT;
  GValue format_list = G_VALUE_INIT;
  g_value_init (&fps_list, GST_TYPE_LIST);
  g_value_init (&format_list, GST_TYPE_LIST);

  if (camera_params_get_fps_list (params, "preview-frame-rate-values", &fps_list)
      && camera_params_get_format_list (params, "video-frame-format", &format_list)
      && camera_params_foreach (params, "preview-size-values",
          camera_params_append_video_structure, caps)) {
    gst_caps_set_value (caps, "framerate", &fps_list);
    gst_caps_set_value (caps, "format", &format_list);
  }

  g_value_unset (&format_list);
  g_value_unset (&fps_list);

  return caps;
}

static void
camera_params_append_capture_structure (const gchar *value, gpointer user_data)
{
  GstCaps *caps = user_data;
  gint width, height;

  if (camera_params_parse_resolution (value, &width, &height)) {
      gst_caps_append_structure (caps, gst_structure_new ("image/jpeg",
          "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height,
          "framerate", GST_TYPE_FRACTION, 30, 1,
          NULL));
    return;
  }
}

GstCaps *
camera_params_get_capture_caps (GstStructure * params)
{
  GstCaps *caps = gst_caps_new_empty ();

  camera_params_foreach (params, "picture-size-values",
      camera_params_append_capture_structure, caps);

  return caps;
}

GstCaps *
camera_params_get_video_caps (GstStructure *params)
{
  GstCaps *caps = gst_caps_new_empty ();
  GValue fps_list = G_VALUE_INIT;
  GValue format_list = G_VALUE_INIT;
  g_value_init (&fps_list, GST_TYPE_LIST);
  g_value_init (&format_list, GST_TYPE_LIST);

  if (camera_params_get_fps_list (params, "preview-frame-rate-values", &fps_list)
      && camera_params_get_format_list (params, "video-frame-format", &format_list)
      && camera_params_foreach (params, "video-size-values",
          camera_params_append_video_structure, caps)) {
    gst_caps_set_value (caps, "framerate", &fps_list);
    gst_caps_set_value (caps, "format", &format_list);
  }

  if (!gst_caps_is_empty (caps)) {
    guint i, n;
    GstCaps *featureCaps = gst_caps_copy (caps);
    GstCapsFeatures *features = gst_caps_features_from_string
        ("memory:AndroidMetadata");

    gst_caps_set_features (featureCaps, 0, features);

    n = gst_caps_get_size (featureCaps);
    for (i = 1; i < n; ++i) {
      gst_caps_set_features (featureCaps, i,
          gst_caps_features_copy (features));
    }

    caps = gst_caps_merge (featureCaps, caps);
  }

  g_value_unset (&format_list);
  g_value_unset (&fps_list);

  return caps;
}
