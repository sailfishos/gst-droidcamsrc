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
start_image_capture (gpointer user_data)
{
  TestPipeline *pipeline = (TestPipeline *) user_data;

  g_signal_emit_by_name (pipeline->src, "start-capture", NULL);

  return FALSE;                 /* Bye! */
}

int
main (int argc, char *argv[])
{
  TestPipeline *pipeline = test_pipeline_new (argc, argv);
  if (!pipeline) {
    return 1;
  }

  g_timeout_add (3000, start_image_capture, pipeline);

  return test_pipeline_exec (pipeline, 10000);
}
