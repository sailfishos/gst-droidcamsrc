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

#ifndef __TEST_H__
#define __TEST_H__

#include <gst/gst.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  GMainLoop *loop;
  GstElement *src;
  GstElement *vf;
  GstElement *vf_csp;
  GstElement *vf_q;

  GstElement *fs;
  GstElement *fs_csp;
  GstElement *fs_q;

  GstElement *pipeline;

  gulong probe_id;

  int ret;
} TestPipeline;

TestPipeline *test_pipeline_new (int argc, char *argv[]);

int test_pipeline_exec (TestPipeline *pipeline, int timeout);

void test_pipeline_free (TestPipeline *pipeline);

G_END_DECLS

#endif /* __TEST_H__ */
