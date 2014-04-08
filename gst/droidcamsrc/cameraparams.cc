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
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <cstring>
#include <cstdlib>
#include "gstdroidcamsrc.h"

G_BEGIN_DECLS;

struct camera_params
{
  std::map < std::string, std::vector < std::string > >items;
};

struct ColorFormat
{
  const gchar *param_format;
  const gchar *gstreamer_format;
};

static const ColorFormat color_format_lookup[] = {
  { "yuv422sp", "NV16" },
  { "yuv420sp", "NV12" },
  { "yuv422i-yuyv", "YUY2" },
  { "yuv420p", "YV12" },
  { "nv12", "NV12" },
  { NULL, NULL }
};

static const gchar *camera_params_get_gstreamer_format(const gchar *param_format)
{
  const ColorFormat *format;

  for (format = color_format_lookup; format->param_format; ++format) {
      if (strcmp (param_format, format->param_format) == 0)
        return format->gstreamer_format;
  }

  return NULL;
}

static const gchar *camera_params_get_param_format(const gchar *gstreamer_format)
{
  const ColorFormat *format;

  for (format = color_format_lookup; format->param_format; ++format) {
      if (strcmp (gstreamer_format, format->gstreamer_format) == 0)
        return format->param_format;
  }

  return NULL;
}

static bool
camera_params_get_fps_list (struct camera_params *params, GValue& output)
{
  std::map < std::string, std::vector < std::string > >::iterator fps_iter =
    params->items.find ("preview-frame-rate-values");

  if (fps_iter == params->items.end()) {
    return false;
  }

  for (std::vector < std::string >::iterator fps = fps_iter->second.begin ();
       fps != fps_iter->second.end (); fps++) {

    int f = atoi ((*fps).c_str ());

    if (!f) {
      continue;
    }

    GValue val = G_VALUE_INIT;
    g_value_init (&val, GST_TYPE_FRACTION);
    gst_value_set_fraction (&val, f, 1);
    gst_value_list_append_value (&output, &val);
  }

  return true;
}

static bool
camera_params_get_format_list (struct camera_params *params, GValue& output)
{
  std::map < std::string, std::vector < std::string > >::iterator format_iter =
    params->items.find ("video-frame-format");
  bool ret = false;

  if (format_iter == params->items.end()) {
    return false;
  }

  for (std::vector < std::string >::iterator it = format_iter->second.begin ();
       it != format_iter->second.end (); it++) {
    const gchar *format = camera_params_get_gstreamer_format ((*it).c_str ());

    if (!format)
      continue;

    ret = true;

    GValue val = G_VALUE_INIT;
    g_value_init (&val, G_TYPE_STRING);
    g_value_set_string (&val, format);
    gst_value_list_append_value (&output, &val);
  }

  return ret;
}

static void
camera_params_parse_size (const char *size, int *width, int *height)
{
  gchar *width_str = g_strdup (size);
  gchar *height_str;

  for (height_str = width_str; *height_str; ++height_str) {
    if (*height_str == 'x') {
        *height_str = '\0';
        ++height_str;
        break;
      }
  }

  *width = atoi (width_str);
  *height = atoi (height_str);

  g_free (width_str);
}

struct camera_params *
camera_params_from_string (const char *str)
{
  struct camera_params *params = new struct camera_params;

  camera_params_update (params, str);

  return params;
}

void
camera_params_update (struct camera_params *params, const char *str)
{
  std::string s (str);

  std::stringstream stream;
  stream.str (s);
  std::string item;
  std::map < std::string, std::vector < std::string > >items;

  while (getline (stream, item, ';')) {
    std::string key, value;
    std::vector < std::string > values;
    std::stringstream i (item);

    if (!getline (i, key, '=')) {
      continue;
    }

    while (getline (i, value, ',')) {
      values.push_back (value);
    }

    if (values.size () == 0) {
      continue;
    }

    items.insert (std::pair < std::string, std::vector < std::string > >(key,
            values));
  }

  params->items = items;
}

void
camera_params_free (struct camera_params *params)
{
  delete params;
}

void
to_stream (struct camera_params *params, std::stringstream & stream, char sep)
{
  std::map < std::string, std::vector < std::string > >::iterator end =
      params->items.end ();
  --end;

  for (std::map < std::string, std::vector < std::string > >::iterator iter =
      params->items.begin (); iter != params->items.end (); iter++) {

    stream << iter->first << "=";

    for (unsigned x = 0; x < iter->second.size (); x++) {
      if (x != 0) {
        stream << ",";
      }

      stream << iter->second[x];
    }

    if (iter != end) {
      stream << sep;
    }
  }
}

char *
camera_params_to_string (struct camera_params *params)
{
  std::stringstream s;

  to_stream (params, s, ';');

  return strdup (s.str ().c_str ());
}

void
camera_params_dump (struct camera_params *params)
{
  std::stringstream s;

  to_stream (params, s, '\n');

  std::cout << s.str () << std::endl;
}

void
camera_params_set (struct camera_params *params, const char *key,
    const char *val)
{
  std::vector < std::string > values;

  values.push_back (val);

  std::map < std::string, std::vector < std::string > >::iterator iter =
      params->items.find (key);
  if (iter != params->items.end ()) {
    params->items.erase (iter);
  }

  params->items.insert (std::pair < std::string,
      std::vector < std::string > >(key, values));
}

GstCaps *
camera_params_get_viewfinder_caps (struct camera_params *params)
{
  GValue fps_list = G_VALUE_INIT;
  GValue format_list = G_VALUE_INIT;
  g_value_init (&fps_list, GST_TYPE_LIST);
  g_value_init (&format_list, GST_TYPE_LIST);

  std::map < std::string, std::vector < std::string > >::iterator sizes =
      params->items.find ("preview-size-values");

  if (sizes == params->items.end () || !camera_params_get_fps_list (params, fps_list)
      || !camera_params_get_format_list (params, format_list)) {
    return gst_caps_new_empty ();
  }

  GstCaps *caps = gst_caps_new_empty ();

  for (std::vector < std::string >::iterator size = sizes->second.begin ();
      size != sizes->second.end (); size++) {
    int width;
    int height;
    camera_params_parse_size (size->c_str (), &width, &height);

    if (!width || !height) {
      continue;
    }

    GstStructure *s = gst_structure_new ("video/x-raw",
					 "width", G_TYPE_INT, width,
					 "height", G_TYPE_INT, height,
					 NULL);

    gst_caps_append_structure (caps, s);
  }

  gst_caps_set_value (caps, "framerate", &fps_list);
  gst_caps_set_value (caps, "format", &format_list);

  return caps;
}

GstCaps *
camera_params_get_capture_caps (struct camera_params * params)
{
  std::map < std::string, std::vector < std::string > >::iterator sizes =
      params->items.find ("picture-size-values");

  if (sizes == params->items.end ()) {
    return gst_caps_new_empty ();
  }

  GstCaps *caps = gst_caps_new_empty ();

  for (std::vector < std::string >::iterator size = sizes->second.begin ();
      size != sizes->second.end (); size++) {
    std::stringstream stream;
    stream.str (*size);
    std::vector < std::string > d;
    std::string item;

    while (getline (stream, item, 'x')) {
      d.push_back (item);
    }

    if (d.size () != 2) {
      continue;
    }

    int width = atoi (d[0].c_str ());
    int height = atoi (d[1].c_str ());

    if (!width || !height) {
      continue;
    }
    // TODO: hardcoded structure name
    // TODO: what to set framerate to ?
    GstStructure *s = gst_structure_new ("image/jpeg",
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        NULL);

    gst_caps_append_structure (caps, s);
  }

  caps = gst_caps_simplify (caps);

  return caps;
}

void
camera_params_set_viewfinder_size (struct camera_params *params, int width,
    int height)
{
  std::stringstream stream;
  stream << width << "x" << height;

  camera_params_set (params, "preview-size", stream.str ().c_str ());
}

void
camera_params_set_capture_size (struct camera_params *params, int width,
    int height)
{
  std::stringstream stream;
  stream << width << "x" << height;

  camera_params_set (params, "picture-size", stream.str ().c_str ());
}

void
camera_params_set_viewfinder_fps (struct camera_params *params, int fps)
{
  std::stringstream stream;
  stream << fps;
  camera_params_set (params, "preview-frame-rate", stream.str ().c_str ());
}


void
camera_params_set_viewfinder_format (struct camera_params *params,
    const gchar *format)
{
  const gchar *param_format = camera_params_get_param_format (format);

  if (param_format) {
    camera_params_set (params, "preview-format", param_format);
  }
}

GstCaps *
camera_params_get_video_caps (struct camera_params *params)
{
  const gchar *gst_format = NULL;
  GValue fps_list = G_VALUE_INIT;
  g_value_init (&fps_list, GST_TYPE_LIST);

  std::map < std::string, std::vector < std::string > >::iterator sizes =
      params->items.find ("video-size-values");

  if (sizes == params->items.end () || !camera_params_get_fps_list (params, fps_list)) {
    return gst_caps_new_empty ();
  }

  std::map < std::string, std::vector < std::string > >::iterator format =
      params->items.find ("video-frame-format");
  if (format != params->items.end () && format->second.size () > 0) {
      gst_format = camera_params_get_gstreamer_format(
          format->second.front (). c_str());
  }

  GstCaps *caps = gst_caps_new_empty ();

  for (std::vector < std::string >::iterator size = sizes->second.begin ();
      size != sizes->second.end (); size++) {
    std::stringstream stream;
    stream.str (*size);
    std::vector < std::string > d;
    std::string item;

    while (getline (stream, item, 'x')) {
      d.push_back (item);
    }

    if (d.size () != 2) {
      continue;
    }

    int width = atoi (d[0].c_str ());
    int height = atoi (d[1].c_str ());

    if (!width || !height) {
      continue;
    }

    GstStructure *s = gst_structure_new ("video/x-raw",
                                         "format", G_TYPE_STRING, gst_format,
					 "width", G_TYPE_INT, width,
					 "height", G_TYPE_INT, height,
					 NULL);
    gst_structure_set_value (s, "framerate", &fps_list);
    caps = gst_caps_merge_structure (caps, s);
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

  return caps;
}

void
camera_params_set_video_size (struct camera_params *params, int width,
    int height)
{
  std::stringstream stream;
  stream << width << "x" << height;

  camera_params_set (params, "video-size", stream.str ().c_str ());
}

int
camera_params_get_int (struct camera_params *params, const char *key)
{
  std::map < std::string, std::vector < std::string > >::iterator iter =
      params->items.find (key);
  if (iter->second.size () == 0) {
    return 0;
  }

  return atoi (iter->second[0].c_str ());
}

void
camera_params_set_int (struct camera_params *params, const char *key, int val)
{
  std::stringstream stream;
  stream << val;

  camera_params_set (params, key, stream.str ().c_str ());
}

G_END_DECLS;
