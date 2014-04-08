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

#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <gst/gst.h>
#include <assert.h>
#include <gst/pbutils/encoding-profile.h>
#include <gst/pbutils/encoding-target.h>

#define MODE_IMAGE          1
#define MODE_VIDEO          2

typedef struct
{
  GstElement *bin;
  GstElement *src;
  GstElement *sink;
  GMainLoop *loop;
  int timeout;
  int images;
  int videos;
} Test;

static gboolean
start_image_capture (Test * test)
{
  if (!test->images) {
    g_print ("No more images. Quitting!\n");
    g_main_loop_quit (test->loop);
    return FALSE;
  }

  --test->images;
  g_print ("Starting image capture\n");
  g_signal_emit_by_name (test->bin, "start-capture", NULL);

  return FALSE;
}

static gboolean
stop_video_capture (Test * test)
{
  g_print ("Stopping video capture\n");
  g_signal_emit_by_name (test->bin, "stop-capture", NULL);

  return FALSE;
}

static gboolean
start_video_capture (Test * test)
{
  if (!test->videos) {
    g_print ("No more videos. Quitting!\n");
    g_main_loop_quit (test->loop);
    return FALSE;
  }

  --test->videos;
  g_print ("Starting video capture\n");
  g_signal_emit_by_name (test->bin, "start-capture", NULL);

  g_timeout_add (test->timeout * 1000, (GSourceFunc) stop_video_capture, test);

  return FALSE;
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  Test *test = (Test *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ELEMENT:{
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (test->bin)) {
        const GstStructure *structure = gst_message_get_structure (msg);
        if (gst_structure_has_name (structure, "image-done")) {
          const gchar *fname = gst_structure_get_string (structure, "filename");
          g_print ("captured image %s\n", fname);
          g_timeout_add (1000, (GSourceFunc) start_image_capture, test);
        } else {
          g_timeout_add (1000, (GSourceFunc) start_video_capture, test);
        }
      }

      break;
    }

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

static gboolean
parse_options (int argc, char *argv[], Test * test)
{
  int opt;
  static struct option options[] = {
    {"timeout", required_argument, 0, 0},
    {"image", required_argument, 0, 0},
    {"video", required_argument, 0, 0},
    {"help", no_argument, 0, 0},
    {0, 0, 0, 0}
  };

  while (1) {
    opt = getopt_long (argc, argv, "t:i:v:h", options, NULL);
    if (opt == -1) {
      break;
    }

    switch (opt) {
      case 't':
        test->timeout = atoi (optarg);
        break;

      case 'i':
        test->images = atoi (optarg);
        break;

      case 'v':
        test->videos = atoi (optarg);
        break;

      case '?':
        exit (1);

      case 'h':
        g_print
            ("Usage: %s [--image <number of images] [--video <number of videos>]\n",
            argv[0]);
        exit (0);

      default:
        g_printerr ("unknown option 0%o\n", opt);
        break;
    }
  }

  if (test->videos && test->images) {
    g_printerr ("Cannot do both images and videos\n");
    return FALSE;
  }

  if (test->timeout <= 0) {
    g_printerr ("Timeout must be greater than 0\n");
    return FALSE;
  }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  Test *test = g_malloc (sizeof (Test));
  memset (test, 0x0, sizeof (Test));
  test->timeout = 2;

  if (!parse_options (argc, argv, test)) {
    return 1;
  }

  gst_init (&argc, &argv);

  test->loop = g_main_loop_new (NULL, FALSE);

  test->src = gst_element_factory_make ("droidcamsrc", NULL);
  test->sink = gst_element_factory_make ("fakesink", NULL);
  test->bin = gst_element_factory_make ("camerabin", NULL);

  assert (test->src);
  assert (test->sink);
  assert (test->bin);

  GstEncodingProfile *video = video_profile ();
  assert (video);

  g_object_set (test->sink, "silent", TRUE, NULL);
  g_object_set (test->bin, "camera-source", test->src, "viewfinder-sink",
      test->sink, "flags", 0x00000001 | 0x00000002 | 0x00000004 | 0x00000008,
      "video-profile", video, NULL);

  if (test->videos) {
    g_object_set (test->bin, "mode", MODE_VIDEO, NULL);
    g_print ("Setting mode to video\n");
  } else if (test->images) {
    g_print ("Setting mode to image\n");
    g_object_set (test->bin, "mode", MODE_IMAGE, NULL);
  } else {
    g_print ("Not setting mode\n");
  }

  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (test->bin));
  gst_bus_add_watch (bus, bus_call, test);
  gst_object_unref (bus);

  GstStateChangeReturn ret =
      gst_element_set_state (test->bin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to start camerabin2\n");
    return 1;
  }

  if (test->images) {
    g_object_set (test->bin, "location", "cap_%d.jpg", NULL);
    g_timeout_add (test->timeout * 1000, (GSourceFunc) start_image_capture,
        test);
  } else if (test->videos) {
    g_object_set (test->bin, "location", "cap_%d.mp4", NULL);
    g_timeout_add (1000, (GSourceFunc) start_video_capture, test);
  } else {
    g_print ("Setting pipeline timeout to %i seconds\n", test->timeout);
    g_timeout_add (test->timeout * 1000, (GSourceFunc) g_main_loop_quit,
        test->loop);
  }

  g_main_loop_run (test->loop);
  gst_element_set_state (test->bin, GST_STATE_NULL);

  gst_object_unref (test->bin);

  g_main_loop_unref (test->loop);

  g_free (test);

  gst_deinit ();

  return 0;
}
