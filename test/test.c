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

#include "test.h"
#define CAMERA_SRC "droidcamsrc"
#define VIDEO_SINK "hwcsink"

static gboolean
timeout_cb (gpointer user_data)
{
  TestPipeline *pipeline = (TestPipeline *) user_data;

  gst_element_send_event (pipeline->pipeline, gst_event_new_eos ());

  g_main_loop_quit (pipeline->loop);

  return FALSE;                 /* Bye! */
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  TestPipeline *pipeline = (TestPipeline *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      pipeline->ret = 0;
      g_main_loop_quit (pipeline->loop);
      break;

    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      if (debug) {
        g_free (debug);
      }

      pipeline->ret = 1;
      g_main_loop_quit (pipeline->loop);
      break;
    }

    case GST_MESSAGE_WARNING:{
      gchar *debug;
      GError *error;

      gst_message_parse_warning (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Warning: %s\n", error->message);
      g_error_free (error);

      if (debug) {
        g_free (debug);
      }

      pipeline->ret = 1;
      g_main_loop_quit (pipeline->loop);
      break;
    }

    default:
      break;
  }

  return TRUE;
}

TestPipeline *
test_pipeline_new (int argc, char *argv[])
{
  GstBus *bus;
  TestPipeline *pipeline;

  gst_init (&argc, &argv);

  pipeline = g_new0 (TestPipeline, 1);
  pipeline->loop = g_main_loop_new (NULL, FALSE);

  pipeline->src = gst_element_factory_make (CAMERA_SRC, NULL);
  if (!pipeline->src) {
    g_printerr ("Failed to create %s element", CAMERA_SRC);
    goto error;
  }

  pipeline->vf_csp = gst_element_factory_make ("capsfilter", NULL);
  if (!pipeline->vf_csp) {
    g_printerr ("Failed to create viewfinder capsfilter element");
    goto error;
  }

  pipeline->vf = gst_element_factory_make (VIDEO_SINK, NULL);
  if (!pipeline->vf) {
    g_printerr ("Failed to create %s element", VIDEO_SINK);
    goto error;
  }

  pipeline->pipeline = gst_pipeline_new (NULL);

  gst_bin_add_many (GST_BIN (pipeline->pipeline), pipeline->src,
      pipeline->vf_csp, pipeline->vf, NULL);

  if (!gst_element_link_pads (pipeline->src, "vfsrc", pipeline->vf_csp, "sink")) {
    g_printerr ("Failed to link %s to capsfilter", CAMERA_SRC);
    goto error;
  }

  if (!gst_element_link (pipeline->vf_csp, pipeline->vf)) {
    g_printerr ("Failed to link elements");
    goto error;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline->pipeline));
  gst_bus_add_watch (bus, bus_call, pipeline);
  gst_object_unref (bus);

  return pipeline;

error:
  test_pipeline_free (pipeline);
  pipeline = NULL;
  return NULL;
}

void
test_pipeline_free (TestPipeline * pipeline)
{
  g_main_loop_unref (pipeline->loop);

  if (pipeline->pipeline) {
    /* Bin will destroy all its child elements. */
    gst_object_unref (pipeline->pipeline);
    g_free (pipeline);
    return;
  }

  gst_object_unref (pipeline->src);
  gst_object_unref (pipeline->vf_csp);
  gst_object_unref (pipeline->vf);
  g_free (pipeline);
}

int
test_pipeline_exec (TestPipeline * pipeline, int timeout)
{
  if (gst_element_set_state (pipeline->pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to set pipeline state to PLAYING");
    return 1;
  }

  g_timeout_add (timeout, timeout_cb, pipeline);

  g_main_loop_run (pipeline->loop);

  gst_element_set_state (pipeline->pipeline, GST_STATE_NULL);

  test_pipeline_free (pipeline);

  gst_deinit ();

  return pipeline->ret;
}
