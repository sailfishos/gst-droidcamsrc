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

#ifndef __GST_DROID_CAM_SRC__
#define __GST_DROID_CAM_SRC__

#include <gst/gst.h>

#include "android/camera.h"
#include "gst/gstgralloc.h"

G_BEGIN_DECLS

#define GST_TYPE_DROID_CAM_SRC \
  (gst_droid_cam_src_get_type())
#define GST_DROID_CAM_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DROID_CAM_SRC,GstDroidCamSrc))
#define GST_DROID_CAM_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DROID_CAM_SRC,GstDroidCamSrcClass))
#define GST_IS_DROID_CAM_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DROID_CAM_SRC))
#define GST_IS_DROID_CAM_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DROID_CAM_SRC))

typedef struct _GstDroidCamSrc GstDroidCamSrc;
typedef struct _GstDroidCamSrcClass GstDroidCamSrcClass;

struct _GstDroidCamSrc {
  GstBin parent;

  GstGralloc *gralloc;
  struct hw_module_t *hwmod;
  camera_module_t *cam;

  struct hw_device_t *cam_dev;
  camera_device_t *dev;

  int camera_device;
};

struct _GstDroidCamSrcClass {
  GstBinClass parent_class;
};

GType gst_droid_cam_src_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_CAM_SRC__ */
