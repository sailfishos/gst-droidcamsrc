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

#include "gstcamerasettings.h"
#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>

#define DROID_CAM_SRC_CONF "/etc/xdg/gstdroidcamsrc.conf"

static GHashTable *gst_camera_settings_load (GKeyFile * file,
    const char *group);

GstCameraSettings *
gst_camera_settings_new ()
{
  GstCameraSettings *settings = NULL;
  GKeyFile *file = NULL;
  GError *error = NULL;

  settings = g_malloc (sizeof (GstCameraSettings));
  memset (settings, 0x0, sizeof (GstCameraSettings));
  file = g_key_file_new ();

  if (!g_key_file_load_from_file (file, DROID_CAM_SRC_CONF, G_KEY_FILE_NONE,
          &error)) {
    GST_ERROR ("Error %d reading %s (%s)", error->code, DROID_CAM_SRC_CONF,
        error->message);
    goto out;
  }

  settings->flash_mode = gst_camera_settings_load (file, "flash-mode");
  settings->focus_mode = gst_camera_settings_load (file, "focus-mode");
  settings->white_balance_mode =
      gst_camera_settings_load (file, "white-balance-mode");

out:
  if (file) {
    g_key_file_free (file);
  }

  if (error) {
    g_error_free (error);
  }

  return settings;
}

void
gst_camera_settings_destroy (GstCameraSettings * settings)
{
  if (settings->flash_mode) {
    g_hash_table_destroy (settings->flash_mode);
    settings->flash_mode = NULL;
  }

  if (settings->focus_mode) {
    g_hash_table_destroy (settings->focus_mode);
    settings->focus_mode = NULL;
  }

  if (settings->white_balance_mode) {
    g_hash_table_destroy (settings->white_balance_mode);
    settings->white_balance_mode = NULL;
  }

  g_free (settings);
}

static GHashTable *
gst_camera_settings_load (GKeyFile * file, const char *group)
{
  gsize length = 0;
  char **keys = NULL;
  int x;

  GHashTable *table = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_free);

  keys = g_key_file_get_keys (file, group, &length, NULL);

  for (x = 0; x < length; x++) {
    char *key_str = keys[x];
    int key = atoi (keys[x]);
    gchar *value = g_key_file_get_string (file, group, key_str, NULL);

    g_hash_table_insert (table, GINT_TO_POINTER (key), value);
  }

  if (keys) {
    g_strfreev (keys);
  }

  return table;
}

const char *
gst_camera_settings_find_droid (GHashTable * table, int key)
{
  char *val;
  gboolean found;

  if (!table) {
    return NULL;
  }

  found =
      g_hash_table_lookup_extended (table, GINT_TO_POINTER (key), NULL,
      (gpointer *) & val);

  if (found) {
    return val;
  }

  return NULL;
}
