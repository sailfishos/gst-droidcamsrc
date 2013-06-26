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

#include <gst/gst.h>
#include <assert.h>
#include <gst/pbutils/encoding-profile.h>
#include <gst/pbutils/encoding-target.h>

typedef struct
{
  GstElement *bin;
  GstElement *src;
  GstElement *sink;
  GMainLoop *loop;
} Test;

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  Test *test = (Test *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_printerr ("Error: %s (%s)\n", error->message, debug);
      g_error_free (error);

      if (debug) {
        g_free (debug);
      }

      g_main_loop_quit (test->loop);
      break;
    }

    case GST_MESSAGE_WARNING:{
      gchar *debug;
      GError *error;

      gst_message_parse_warning (msg, &error, &debug);

      g_printerr ("Warning: %s (%s)\n", error->message, debug);
      g_error_free (error);

      if (debug) {
        g_free (debug);
      }

      break;
    }

    default:
      break;
  }

  return TRUE;
}

GstEncodingProfile *
video_profile ()
{
  GError *error = NULL;
  GstEncodingTarget *target = gst_encoding_target_load_from_file ("dummy.gep",
      &error);
  if (!target) {
    g_printerr ("Failed to load encoding profile: %s\n", error->message);
    g_error_free (error);

    return NULL;
  }

  GstEncodingProfile *profile =
      gst_encoding_target_get_profile (target, "video-raw");
  if (!profile) {
    g_printerr ("Failed to load encoding profile\n");
    gst_encoding_target_unref (target);
    return NULL;
  }

  return profile;
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  Test *test = g_malloc (sizeof (Test));
  test->loop = g_main_loop_new (NULL, FALSE);

  test->src = gst_element_factory_make ("droidcamsrc", NULL);
  test->sink = gst_element_factory_make ("fakesink", NULL);
  test->bin = gst_element_factory_make ("camerabin2", NULL);

  assert (test->src && test->sink && test->bin);

  GstEncodingProfile *video = video_profile ();
  assert (video);

  g_object_set (test->sink, "silent", TRUE, NULL);
  g_object_set (test->bin, "camera-source", test->src, "viewfinder-sink",
      test->sink, "flags", 0x00000001 | 0x00000002 | 0x00000004 | 0x00000008,
      "video-profile", video, NULL);

  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (test->bin));
  gst_bus_add_watch (bus, bus_call, test);
  gst_object_unref (bus);

  GstStateChangeReturn ret =
      gst_element_set_state (test->bin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to start camerabin2\n");
    return 1;
  }

  g_timeout_add (5000, (GSourceFunc) g_main_loop_quit, test->loop);

  g_main_loop_run (test->loop);
  gst_element_set_state (test->bin, GST_STATE_NULL);

  return 0;
}
