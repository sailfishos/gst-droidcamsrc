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

static gboolean
vidsrc_data_probe (GstPad * pad, GstMiniObject * obj, gpointer user_data)
{
  GstElement *elem = (GstElement *) user_data;

  gst_element_set_locked_state (elem, FALSE);

  gst_element_set_state (elem, GST_STATE_PLAYING);

  return TRUE;                  /* keep data. */
}

static gboolean
start_video_capture (gpointer user_data)
{
  TestPipeline *pipeline = (TestPipeline *) user_data;

  g_signal_emit_by_name (pipeline->src, "start-capture", NULL);

  return FALSE;                 /* Bye! */
}

static gboolean
stop_video_capture (gpointer user_data)
{
  TestPipeline *pipeline = (TestPipeline *) user_data;

  g_signal_emit_by_name (pipeline->src, "stop-capture", NULL);

  return FALSE;                 /* Bye! */
}

int
main (int argc, char *argv[])
{
  TestPipeline *pipeline = test_pipeline_new (argc, argv);
  if (!pipeline) {
    return 1;
  }

  g_object_set (pipeline->src, "mode", 2, NULL);

  GstElement *filesink = gst_element_factory_make ("filesink", NULL);
  gst_element_set_locked_state (filesink, TRUE);
  g_object_set (filesink, "location", "foo.yuv", NULL);

  gst_bin_add (GST_BIN (pipeline->pipeline), filesink);

  if (!gst_element_link_pads (pipeline->src, "vidsrc", filesink, "sink")) {
    g_printerr ("Failed to link filesink");
    return 1;
  }

  GstPad *pad = gst_element_get_static_pad (pipeline->src, "vidsrc");
  gst_pad_add_data_probe (pad, G_CALLBACK (vidsrc_data_probe), filesink);
  gst_object_unref (pad);

  g_timeout_add (5000, start_video_capture, pipeline);
  g_timeout_add (15000, stop_video_capture, pipeline);

  return test_pipeline_exec (pipeline, 20000);
}
