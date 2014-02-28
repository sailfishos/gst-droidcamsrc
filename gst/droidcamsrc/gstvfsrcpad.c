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

#include "gstvfsrcpad.h"
#include "gstdroidcamsrc.h"
#include "cameraparams.h"
#include <gst/video/video.h>
#include <gst/anativewindowbuffer/gstanativewindowbufferpool.h>

GST_DEBUG_CATEGORY_STATIC (droidvfsrc_debug);
#define GST_CAT_DEFAULT droidvfsrc_debug

#define MIN_UNDEQUEUED_BUFFER_COUNT 2

#define container_of(ptr, type, member) ({ \
      const typeof( ((type *)0)->member ) *__mptr = (ptr); (type *)( (char *)__mptr - offsetof(type,member) );})

typedef struct _GstDroidCamBufferMeta GstDroidCamBufferMeta;

static GType gst_droid_cam_buffer_meta_api_get_type (void);
#define GST_DROID_CAM_BUFFER_META_API_TYPE (gst_droid_cam_buffer_meta_api_get_type())
static const GstMetaInfo * gst_droid_cam_buffer_meta_get_info (void);
#define GST_DROID_CAM_BUFFER_META_INFO (gst_droid_cam_buffer_meta_get_info())

#define gst_buffer_get_droid_cam_buffer_meta(b) ((GstDroidCamBufferMeta*)gst_buffer_get_meta((b),GST_DROID_CAM_BUFFER_META_API_TYPE))

struct _GstDroidCamBufferMeta {
  GstMeta meta;

  buffer_handle_t handle;
  GstBuffer *buffer;
};

static GType
gst_droid_cam_buffer_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] =
      { GST_META_TAG_VIDEO_STR, GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstDroidCamBufferMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static const GstMetaInfo *
gst_droid_cam_buffer_meta_get_info (void)
{
  static const GstMetaInfo *droid_cam_buffer_meta_info = NULL;

  if (g_once_init_enter (&droid_cam_buffer_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_DROID_CAM_BUFFER_META_API_TYPE, "GstDroidCamBufferMeta",
        sizeof (GstDroidCamBufferMeta), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) NULL, (GstMetaTransformFunction) NULL);
    g_once_init_leave (&droid_cam_buffer_meta_info, meta);
  }
  return droid_cam_buffer_meta_info;
}

static GstBuffer *
gst_droid_cam_buffer_meta_get_buffer (buffer_handle_t *handle)
{
  GstDroidCamBufferMeta *meta = container_of (handle, GstDroidCamBufferMeta,
      handle);
  return meta->buffer;
}

static ANativeWindowBuffer_t *
gst_droid_cam_buffer_meta_get_native_buffer (GstBuffer *buffer)
{
  GstMemory *mem;

  if (gst_buffer_n_memory (buffer) != 1
      || !(mem = gst_buffer_peek_memory (buffer, 0))
      || g_strcmp0 (mem->allocator->mem_type,
          GST_A_NATIVE_WINDOW_BUFFER_MEMORY_TYPE) != 0) {
    return NULL;
  }

  return gst_a_native_window_buffer_memory_get_buffer (mem);
}

static buffer_handle_t *
gst_droid_cam_buffer_meta_get_handle(GstBuffer *buffer)
{
  GstDroidCamBufferMeta *meta = gst_buffer_get_droid_cam_buffer_meta (
      buffer);

  if (!meta) {
    ANativeWindowBuffer_t *native_buffer
        = gst_droid_cam_buffer_meta_get_native_buffer(buffer);

    if (!native_buffer) {
      return NULL;
    }

    meta = (GstDroidCamBufferMeta *) gst_buffer_add_meta (buffer,
        GST_DROID_CAM_BUFFER_META_INFO, NULL);
    meta->buffer = buffer;
    meta->handle = native_buffer->handle;

    GST_META_FLAG_SET ((GstMeta *) meta, GST_META_FLAG_POOLED);
  }

  return &meta->handle;
}

static int
gst_droid_cam_buffer_meta_get_stride(GstBuffer *buffer)
{
  ANativeWindowBuffer_t *native_buffer
      = gst_droid_cam_buffer_meta_get_native_buffer(buffer);

  if (!native_buffer) {
    return 0;
  }

  return native_buffer->stride;
}

static gboolean gst_droid_cam_src_vfsrc_activatemode (GstPad * pad,
    GstObject *parent, GstPadMode mode, gboolean active);
static gboolean gst_droid_cam_src_vfsrc_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_droid_cam_src_vfsrc_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static gboolean gst_droid_cam_src_vfsrc_negotiate (GstDroidCamSrc * src);

GstPad *
gst_vf_src_pad_new (GstStaticPadTemplate * pad_template, const char *name)
{
  GST_DEBUG_CATEGORY_INIT (droidvfsrc_debug, "droidvfsrc", 0,
      "Android camera viewfinder source pad");

  GstPad *pad = gst_pad_new_from_static_template (pad_template, name);

  gst_pad_set_activatemode_function (pad, gst_droid_cam_src_vfsrc_activatemode);
  gst_pad_set_event_function (pad, gst_droid_cam_src_vfsrc_event);
  gst_pad_set_query_function (pad, gst_droid_cam_src_vfsrc_query);

  return pad;
}

GstCaps *
gst_vf_src_pad_get_supported_caps_unlocked (GstDroidCamSrc *src)
{
  GstCaps *caps;

  if (src->camera_params) {
    caps = camera_params_get_viewfinder_caps (src->camera_params);
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (src->vfsrc));
  }

  return caps;
}

static gboolean
gst_droid_cam_src_vfsrc_activatemode (GstPad * pad, GstObject *parent,
    GstPadMode mode, gboolean active)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (parent);


  if (mode != GST_PAD_MODE_PUSH) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "vfsrc activatepush: %d", active);

  if (active) {
    gchar *orientation;
    GstTagList *taglist;
    GstEvent *event = gst_pad_get_sticky_event (pad, GST_EVENT_STREAM_START, 0);

    if (event) {
      gst_event_unref (event);
    } else {
      gchar *stream_id = gst_pad_create_stream_id (pad, GST_ELEMENT_CAST(src),
          "viewfinder");
      event = gst_event_new_stream_start (stream_id);
      gst_pad_push_event (pad, event);
      g_free (stream_id);
    }

    src->send_new_segment = TRUE;

    /* First we do caps negotiation */
    if (!gst_droid_cam_src_vfsrc_negotiate (src)) {
      return FALSE;
    }

    switch (src->viewfinder_orientation) {
      case 90:
        orientation = "rotate-90";
        break;
      case 180:
        orientation = "rotate-180";
        break;
      case 270:
        orientation = "rotate-270";
        break;
      default:
        orientation = "rotate-0";
        break;
    }

    taglist = gst_tag_list_new (GST_TAG_IMAGE_ORIENTATION, orientation, NULL);
    gst_pad_push_event (pad, gst_event_new_tag (taglist));
  }

  return TRUE;
}

static gboolean gst_droid_cam_src_vfsrc_event (GstPad * pad,
    GstObject * parent, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    default:
      return FALSE;
  }
}

static gboolean
gst_droid_cam_src_vfsrc_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstDroidCamSrc *src = GST_DROID_CAM_SRC (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS: {
      GstCaps *caps;

      GST_OBJECT_LOCK (src);
      caps = gst_vf_src_pad_get_supported_caps_unlocked (src);
      GST_OBJECT_UNLOCK (src);

      GST_LOG_OBJECT (src, "queried caps %" GST_PTR_FORMAT, caps);

      gst_query_set_caps_result (query, caps);
      return TRUE;
    }
    default:
      return FALSE;
    }
}

static gboolean
gst_droid_cam_src_vfsrc_negotiate (GstDroidCamSrc * src)
{
  GstCaps *caps;
  GstCaps *peer;
  gboolean ret = FALSE;
  GstDroidCamSrcClass *klass;
  GstVideoInfo info;

  GST_DEBUG_OBJECT (src, "vfsrc negotiate");

  klass = GST_DROID_CAM_SRC_GET_CLASS (src);

  GST_OBJECT_LOCK (src);
  caps = camera_params_get_viewfinder_caps (src->camera_params);
  GST_OBJECT_UNLOCK (src);

  if (!caps || gst_caps_is_empty (caps)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT,
        ("Failed to get any supported caps"), (NULL));

    if (caps) {
      gst_caps_unref (caps);
    }

    ret = FALSE;
    goto out;
  }

  GST_LOG_OBJECT (src, "caps %" GST_PTR_FORMAT, caps);

  peer = gst_pad_peer_query_caps (src->vfsrc, caps);
  if (peer) {
    gst_caps_unref (caps);
    caps = peer;
  }

  if (gst_caps_is_empty (caps)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("No common caps"), (NULL));

    gst_caps_unref (caps);

    ret = FALSE;
    goto out;
  }

  if (!gst_caps_is_fixed (caps)) {
    GstStructure *s;

    caps = gst_caps_truncate (caps);

    s = gst_caps_get_structure (caps, 0);

    gst_structure_fixate_field_nearest_int (s, "width", DEFAULT_VF_WIDTH);
    gst_structure_fixate_field_nearest_int (s, "height", DEFAULT_VF_HEIGHT);
    gst_structure_fixate_field_nearest_fraction (s, "framerate", DEFAULT_FPS, 1);
    gst_structure_fixate_field (s, "format");
  }

  ret = gst_video_info_from_caps (&info, caps);
  if (ret) {
    GST_OBJECT_LOCK (src);
    camera_params_set_resolution (src->camera_params, "preview-size",
        info.width, info.height);
    camera_params_set_int (src->camera_params, "preview-frame-rate",
        info.fps_n / info.fps_d);
    camera_params_set_format (src->camera_params, "preview-format",
        info.finfo->name);
    GST_OBJECT_UNLOCK (src);

    GST_PAD_STREAM_LOCK (src->vfsrc);
    src->viewfinder_info.fps_n = info.fps_n;
    src->viewfinder_info.fps_d = info.fps_d;
    GST_PAD_STREAM_UNLOCK (src->vfsrc);
  }

  gst_caps_unref (caps);

out:
  if (ret) {
    /* set camera parameters */
    ret = klass->set_camera_params (src);
  }

  return ret;
}

static GstDroidCamSrc *
gst_vf_src_get_droid_cam_src (const preview_stream_ops_t *ops)
{
  return container_of (ops, GstDroidCamSrc, viewfinder_window);
}

static int
gst_droid_cam_src_vfsrc_dequeue_buffer (preview_stream_ops_t * window,
    buffer_handle_t ** handle, int * stride)
{
  GstDroidCamSrc *src = gst_vf_src_get_droid_cam_src (window);
  GstBufferPool *pool;
  GstBuffer *buffer;
  GstBufferPoolAcquireParams acquire_params;
  GstFlowReturn ret;

  GST_LOG_OBJECT (src, "vfsrc dequeue_buffer");

  GST_PAD_STREAM_LOCK (src->vfsrc);

  pool = src->viewfinder_pool;
  if (!pool) {
    GstCaps *caps = gst_video_info_to_caps (&src->viewfinder_info);
    GstQuery *query;
    GstStructure *config;
    gint usage = 0;
    guint min = 0, max = 2, size = src->viewfinder_info.size;
    gint buffer_count = src->viewfinder_buffer_count;
    gint viewfinder_usage = src->viewfinder_usage;
    gint format = src->viewfinder_format;

    GST_PAD_STREAM_UNLOCK (src->vfsrc);

    if (!gst_pad_push_event (src->vfsrc, gst_event_new_caps (caps))) {
      GST_DEBUG_OBJECT (src, "peer rejected caps");
      gst_caps_unref (caps);
      return -1;
    }

    query = gst_query_new_allocation (caps, TRUE);
    if (!gst_pad_peer_query (src->vfsrc, query)) {
      GST_DEBUG_OBJECT (src, "peer provided no allocation params");
    }

    if (!pool) {
      pool = gst_a_native_window_buffer_pool_new ();
    } else {
      gst_buffer_pool_set_active (pool, FALSE);
    }

    config = gst_buffer_pool_get_config (pool);
    gst_structure_get_int (config, "usage", &usage);
    usage |= viewfinder_usage;

    min = MAX (min, buffer_count);
    max = MAX (max, buffer_count);

    gst_buffer_pool_config_set_params (config, caps, size, min, max);
    gst_structure_set (config, "usage", G_TYPE_INT, usage, NULL);
    gst_structure_set (config, "format", G_TYPE_INT, format, NULL);

    gst_caps_unref (caps);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_WARNING_OBJECT (src, "Failed to set buffer pool configuration");
      gst_object_unref (pool);
      return -1;
    }

    GST_PAD_STREAM_LOCK (src->vfsrc);

    if (!src->viewfinder_pool) {
      src->viewfinder_pool = pool;
    } else {
      GST_WARNING ("concurrent pool allocation");
      // It seems unlikely but if two allocation events have taken place
      // concurrently discard the second pool
      gst_object_unref (pool);
      pool = src->viewfinder_pool;
    }
  }

  gst_object_ref (GST_OBJECT(pool));

  GST_PAD_STREAM_UNLOCK (src->vfsrc);

  gst_buffer_pool_set_active (pool, TRUE);

  acquire_params.format = GST_FORMAT_UNDEFINED;
  acquire_params.start = 0;
  acquire_params.stop = 0;
  acquire_params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, &acquire_params);

  gst_object_unref (pool);

  if (ret == GST_FLOW_OK) {
    *handle = gst_droid_cam_buffer_meta_get_handle (buffer);
    if (!*handle) {
      GST_WARNING_OBJECT (src, "Buffer pool returned invalid buffer.");
      gst_buffer_unref (buffer);
      return -1;
    }
    *stride = gst_droid_cam_buffer_meta_get_stride (buffer);

    return 0;
  } else if (ret == GST_FLOW_EOS) {
    GST_DEBUG_OBJECT (src, "No buffer, EOS");
    return -1;
  } else {
    GST_WARNING_OBJECT (src, "Buffer pool failed to return buffer.");
    return -1;
  }
}

static int
gst_droid_cam_src_vfsrc_enqueue_buffer (preview_stream_ops_t * window,
    buffer_handle_t * handle)
{
  GstDroidCamSrc *src = gst_vf_src_get_droid_cam_src (window);
  GstBuffer *buffer = gst_droid_cam_buffer_meta_get_buffer (handle);
  GstVideoCropMeta *crop_meta;
  GList *events;
  GstFlowReturn ret;
  gboolean send_new_segment;

  GST_LOG_OBJECT (src, "vfsrc enqueue_buffer");

  GST_PAD_STREAM_LOCK (src->vfsrc);

  send_new_segment = src->send_new_segment;
  src->send_new_segment = FALSE;

  events = src->events;
  src->events = NULL;

  crop_meta = gst_buffer_add_video_crop_meta (buffer);
  crop_meta->x = src->viewfinder_crop.left;
  crop_meta->y = src->viewfinder_crop.top;
  crop_meta->width = src->viewfinder_crop.right - src->viewfinder_crop.left;
  crop_meta->height = src->viewfinder_crop.bottom - src->viewfinder_crop.top;

  GST_PAD_STREAM_UNLOCK (src->vfsrc);

  if (G_UNLIKELY (send_new_segment)) {
    GST_DEBUG_OBJECT (src, "sending new segment event");

    GstSegment segment;
    gst_segment_init (&segment, GST_FORMAT_TIME);
    segment.start = 0;
    segment.stop = -1;
    segment.position = 0;

    if (!gst_pad_push_event (src->vfsrc, gst_event_new_segment (&segment))) {
      /* TODO: send an error and stop task? */
      GST_WARNING_OBJECT (src, "failed to push new segment");
    }
  }

  while (events) {
    GstEvent *ev = g_list_nth_data (events, 0);
    events = g_list_remove (events, ev);

    gst_pad_push_event (src->vfsrc, ev);
  }

  ret = gst_pad_push (src->vfsrc, buffer);

  if (ret == GST_FLOW_NOT_LINKED || ret <= GST_FLOW_NOT_NEGOTIATED) {
    GST_ELEMENT_ERROR (src, STREAM, FAILED,
        ("Internal data flow error."),
        ("streaming task paused, reason %s (%d)", gst_flow_get_name (ret),
            ret));
  } else {
    return 0;
  }

  /* perform EOS */
  gst_pad_push_event (src->vfsrc, gst_event_new_eos ());

  return -1;
}

static int
gst_droid_cam_src_vfsrc_cancel_buffer (preview_stream_ops_t * window,
    buffer_handle_t * handle)
{
  GstDroidCamSrc *src = gst_vf_src_get_droid_cam_src (window);
  GstBuffer *buffer = gst_droid_cam_buffer_meta_get_buffer (handle);

  GST_DEBUG_OBJECT (gst_vf_src_get_droid_cam_src (window), "vfsrc cancel_buffer");

  GST_PAD_STREAM_LOCK (src->vfsrc);

  if (src->viewfinder_pool) {
    gst_buffer_pool_set_active (src->viewfinder_pool, FALSE);
    gst_object_unref (src->viewfinder_pool);
    src->viewfinder_pool = NULL;
  }

  GST_PAD_STREAM_UNLOCK (src->vfsrc);

  gst_buffer_unref (buffer);

  return 0;
}

static int
gst_droid_cam_src_vfsrc_set_buffer_count (preview_stream_ops_t * window,
    int count)
{
  GstDroidCamSrc *src = gst_vf_src_get_droid_cam_src (window);

  GST_DEBUG_OBJECT (src, "vfsrc set_buffer_count %d", count);

  GST_PAD_STREAM_LOCK (src->vfsrc);
  src->viewfinder_buffer_count = count;
  GST_PAD_STREAM_UNLOCK (src->vfsrc);

  return 0;
}

static int
gst_droid_cam_src_vfsrc_set_buffers_geometry (preview_stream_ops_t * window,
    int width, int height, int format)
{
  GstDroidCamSrc *src = gst_vf_src_get_droid_cam_src (window);
  GstVideoFormat video_format;

  GST_DEBUG_OBJECT (src, "vfsrc set_buffers_geometry w: %d h: %d f: %x", width, height, format);

  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
      video_format = GST_VIDEO_FORMAT_RGBA;
      break;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      video_format = GST_VIDEO_FORMAT_RGBx;
      break;
    case HAL_PIXEL_FORMAT_RGB_888:
      video_format = GST_VIDEO_FORMAT_RGB;
      break;
    case HAL_PIXEL_FORMAT_RGB_565:
      video_format = GST_VIDEO_FORMAT_RGB16;
      break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      video_format = GST_VIDEO_FORMAT_BGRA;
      break;
    case HAL_PIXEL_FORMAT_YV12:
      video_format = GST_VIDEO_FORMAT_YV12;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
      video_format = GST_VIDEO_FORMAT_NV16;
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      video_format = GST_VIDEO_FORMAT_NV12;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
      video_format = GST_VIDEO_FORMAT_YUY2;
      break;
    default:
      return -1;
  }

  GST_PAD_STREAM_LOCK (src->vfsrc);

  if (src->viewfinder_pool && (src->viewfinder_format != format
      || src->viewfinder_info.width != width
      || src->viewfinder_info.height != height)) {
    gst_object_unref (src->viewfinder_pool);
    src->viewfinder_pool = NULL;
  }

  src->viewfinder_format = format;
  gst_video_info_set_format (&src->viewfinder_info, video_format, width,
      height);

  GST_PAD_STREAM_UNLOCK (src->vfsrc);

  return 0;
}

static int
gst_droid_cam_src_vfsrc_set_crop (preview_stream_ops_t * window, int left,
    int top, int right, int bottom)
{
  GstDroidCamSrc *src = gst_vf_src_get_droid_cam_src (window);

  GST_DEBUG_OBJECT (src, "vfsrc set_crop");


  GST_PAD_STREAM_LOCK (src->vfsrc);
  src->viewfinder_crop.left = left;
  src->viewfinder_crop.top = top;
  src->viewfinder_crop.right = right;
  src->viewfinder_crop.bottom = bottom;
  GST_PAD_STREAM_UNLOCK (src->vfsrc);

  return 0;
}

static int
gst_droid_cam_src_vfsrc_set_usage (preview_stream_ops_t * window, int usage)
{
  GstDroidCamSrc *src = gst_vf_src_get_droid_cam_src (window);

  GST_DEBUG_OBJECT (src, "vfsrc set_usage %x", usage);

  GST_PAD_STREAM_LOCK (src->vfsrc);
  src->viewfinder_usage = usage;
  GST_PAD_STREAM_UNLOCK (src->vfsrc);

  return 0;
}

static int
gst_droid_cam_src_vfsrc_set_swap_interval (preview_stream_ops_t * window,
    int interval)
{
  return 0;
}

static int
gst_droid_cam_src_vfsrc_get_min_undequeued_buffer_count (
    const preview_stream_ops_t * window, int * count)
{
  GST_DEBUG_OBJECT (gst_vf_src_get_droid_cam_src (window), "vfsrc get_min_undequeued_buffer_count");
  *count = MIN_UNDEQUEUED_BUFFER_COUNT;
  return 0;
}

static int
gst_droid_cam_src_vfsrc_lock_buffer (preview_stream_ops_t * window,
    buffer_handle_t * buffer)
{
  return 0;
}

static int
gst_droid_cam_src_vfsrc_set_timestamp (preview_stream_ops_t * window,
    int64_t timestamp)
{
  return 0;
}

void
gst_vf_src_pad_init_window (preview_stream_ops_t * window)
{
  memset (window, 0, sizeof (preview_stream_ops_t));

  window->dequeue_buffer = gst_droid_cam_src_vfsrc_dequeue_buffer;
  window->enqueue_buffer = gst_droid_cam_src_vfsrc_enqueue_buffer;
  window->cancel_buffer = gst_droid_cam_src_vfsrc_cancel_buffer;
  window->set_buffer_count = gst_droid_cam_src_vfsrc_set_buffer_count;
  window->set_buffers_geometry = gst_droid_cam_src_vfsrc_set_buffers_geometry;
  window->set_crop = gst_droid_cam_src_vfsrc_set_crop;
  window->set_usage = gst_droid_cam_src_vfsrc_set_usage;
  window->set_swap_interval = gst_droid_cam_src_vfsrc_set_swap_interval;
  window->get_min_undequeued_buffer_count
      = gst_droid_cam_src_vfsrc_get_min_undequeued_buffer_count;
  window->lock_buffer = gst_droid_cam_src_vfsrc_lock_buffer;
  window->set_timestamp = gst_droid_cam_src_vfsrc_set_timestamp;
}
