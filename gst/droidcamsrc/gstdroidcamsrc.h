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

#ifndef __GST_DROID_CAM_SRC_H__
#define __GST_DROID_CAM_SRC_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <hardware/camera.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#include <gst/interfaces/photography.h>
#undef GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include "gstcamerasettings.h"

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
#define GST_DROID_CAM_SRC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DROID_CAM_SRC, GstDroidCamSrcClass))

#define DEFAULT_VF_WIDTH                   640
#define DEFAULT_VF_HEIGHT                  480

#define DEFAULT_IMG_WIDTH                  640
#define DEFAULT_IMG_HEIGHT                 480

/* TODO: this should be VGA but our b0rked caps handling needs this. */
#define DEFAULT_VIDEO_WIDTH                1280
#define DEFAULT_VIDEO_HEIGHT               720

#define DEFAULT_FPS                        30

typedef enum {
  VIDEO_CAPTURE_ERROR = -1,
  VIDEO_CAPTURE_STARTING = 0,
  VIDEO_CAPTURE_RUNNING = 1,
  VIDEO_CAPTURE_STOPPING = 2,
  VIDEO_CAPTURE_STOPPED = 3,
} VideoCaptureStatus;

#define GST_DROID_CAM_SRC_CAPTURE_START "photo-capture-start"
#define GST_DROID_CAM_SRC_CAPTURE_END "photo-capture-end"

typedef struct _GstDroidCamSrc GstDroidCamSrc;
typedef struct _GstDroidCamSrcClass GstDroidCamSrcClass;

typedef struct _GstDroidCamSrcCameraInfo GstDroidCamSrcCameraInfo;
typedef struct _GstDroidCamSrcCrop GstDroidCamSrcCrop;

struct _GstDroidCamSrcCameraInfo {
  /* Sensor mount angle */
  int orientation;

  /* id used for open() call */
  int id;
};


struct _GstDroidCamSrcCrop {
  int left;
  int top;
  int right;
  int bottom;
};

struct _GstDroidCamSrc {
  GstBin parent;

  struct hw_module_t *hwmod;
  camera_module_t *cam;

  struct hw_device_t *cam_dev;
  camera_device_t *dev;

  GstStructure *camera_params;
  GMutex params_lock;

  gint user_camera_device;
  gint camera_device;
  gint mode;

  GList *events;

  GstPad *vfsrc;
  GstPad *imgsrc;
  GstPad *vidsrc;

  preview_stream_ops_t viewfinder_window;
  GstVideoInfo viewfinder_info;
  GstDroidCamSrcCrop viewfinder_crop;
  GstBufferPool *viewfinder_pool;
  int viewfinder_usage;
  int viewfinder_format;
  int viewfinder_orientation;
  int viewfinder_buffer_count;

  gboolean send_new_segment;

  gboolean capturing;
  GMutex capturing_mutex;

  gboolean image_renegotiate;
  gboolean video_renegotiate;

  VideoCaptureStatus video_capture_status;
  int64_t video_start_time;

  int pushed_video_frames;

  int num_video_frames;
  GMutex num_video_frames_lock;
  GCond num_video_frames_cond;

  gboolean capture_start_sent;
  gboolean capture_end_sent;

  GstDroidCamSrcCameraInfo device_info[2];

  /* photography interface bits */
  GstPhotographySettings photo_settings;
  gint *zoom_ratios;
  gint num_zoom_ratios;
  gfloat max_zoom;
  gboolean video_torch;

  GstCameraSettings *settings;

  gboolean image_noise_reduction;

  gfloat ev_comp_step;
};

struct _GstDroidCamSrcClass {
  GstBinClass parent_class;

  gboolean (* set_camera_params) (GstDroidCamSrc *src);
};

GType gst_droid_cam_src_get_type (void);

void gst_droid_cam_src_start_autofocus (GstDroidCamSrc * src);
void gst_droid_cam_src_stop_autofocus (GstDroidCamSrc * src);

G_END_DECLS

#endif /* __GST_DROID_CAM_SRC_H__ */
