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

#include "gstcamerabufferpool.h"
#include <gst/gst.h>
#include <gst/gstnativebuffer.h>

static void gst_camera_buffer_pool_finalize (GstCameraBufferPool * pool);
static gboolean gst_camera_buffer_pool_resurrect_buffer (void *data,
    GstNativeBuffer * buffer);
static gboolean gst_camera_buffer_pool_free_buffer (void *data,
    GstNativeBuffer * buffer);

static GstCameraBufferPoolClass *parent_class;

G_DEFINE_TYPE (GstCameraBufferPool, gst_camera_buffer_pool,
    GST_TYPE_MINI_OBJECT);

GST_DEBUG_CATEGORY_STATIC (droidcambufferpool_debug);
#define GST_CAT_DEFAULT droidcambufferpool_debug

#define MIN_UNDEQUEUED_BUFFER_COUNT 2

/* Maximum amount of time we wait for the application/pipeline to finish rendering */
#define MAX_DEQUEUE_TIMEOUT_MS 33

#define container_of(ptr, type, member) ({ \
      const typeof( ((type *)0)->member ) *__mptr = (ptr); (type *)( (char *)__mptr - offsetof(type,member) );})

static void
gst_camera_buffer_pool_class_init (GstCameraBufferPoolClass * pool_class)
{
  GstMiniObjectClass *mo_class = GST_MINI_OBJECT_CLASS (pool_class);

  parent_class = g_type_class_peek_parent (pool_class);

  mo_class->finalize =
      (GstMiniObjectFinalizeFunction) gst_camera_buffer_pool_finalize;

  GST_DEBUG_CATEGORY_INIT (droidcambufferpool_debug, "droidbufferpool", 0,
      "Android camera buffer pool");
}

GstCameraBufferPool *
gst_camera_buffer_pool_new (GstElement * src, GstGralloc * gralloc)
{
  GstCameraBufferPool *pool =
      (GstCameraBufferPool *) gst_mini_object_new (GST_TYPE_CAMERA_BUFFER_POOL);

  pool->gralloc = gst_gralloc_ref (gralloc);
  pool->src = (GstElement *) gst_object_ref (src);

  return pool;
}

void
gst_camera_buffer_pool_unlock_hal_queue (GstCameraBufferPool * pool)
{
  GST_LOG_OBJECT (pool, "unlock hal queue");

  g_mutex_lock (&pool->hal_lock);
  g_cond_signal (&pool->hal_cond);
  g_mutex_unlock (&pool->hal_lock);
}

void
gst_camera_buffer_pool_unlock_app_queue (GstCameraBufferPool * pool)
{
  GST_LOG_OBJECT (pool, "unlock app queue");

  g_mutex_lock (&pool->app_lock);
  g_cond_signal (&pool->app_cond);
  g_mutex_unlock (&pool->app_lock);
}

static gboolean
gst_camera_buffer_pool_free_buffer (void *data, GstNativeBuffer * buffer)
{
  /* Don't touch data. Likely to be garbage. */
  gst_gralloc_free (gst_native_buffer_get_gralloc (buffer),
      *gst_native_buffer_get_handle (buffer));

  return FALSE;                 /* free buffer finally */
}

static gboolean
gst_camera_buffer_pool_resurrect_buffer (void *data, GstNativeBuffer * buffer)
{
  GstCameraBufferPool *pool = (GstCameraBufferPool *) data;
  GST_DEBUG_OBJECT (pool, "resurrect buffer");

  gst_buffer_ref (GST_BUFFER (buffer));

  g_mutex_lock (&pool->hal_lock);

  g_queue_push_tail (pool->hal_queue, buffer);

  g_cond_signal (&pool->hal_cond);

  g_mutex_unlock (&pool->hal_lock);

  GST_DEBUG_OBJECT (pool, "resurrected buffer");

  return TRUE;
}

static gboolean
gst_camera_buffer_pool_allocate_and_add_unlocked (GstCameraBufferPool * pool)
{
  buffer_handle_t handle = NULL;
  int stride = 0;
  GstNativeBuffer *buffer = NULL;
  GstCaps *caps;

  GST_DEBUG_OBJECT (pool, "allocate and add");

  g_return_val_if_fail (pool->width != 0, FALSE);
  g_return_val_if_fail (pool->height != 0, FALSE);
  g_return_val_if_fail (pool->count != 0, FALSE);
  g_return_val_if_fail (pool->usage != 0, FALSE);
  g_return_val_if_fail (pool->format != 0, FALSE);

  handle =
      gst_gralloc_allocate (pool->gralloc, pool->width, pool->height,
      pool->format, pool->usage, &stride);

  if (!handle) {
    GST_ERROR_OBJECT (pool, "failed to allocate native buffer");
    return FALSE;
  }

  buffer =
      gst_native_buffer_new (handle, pool->gralloc, pool->width, pool->height,
      stride, pool->usage, pool->format);
  GST_DEBUG_OBJECT (pool, "Allocated buffer %p", buffer);

  caps = gst_caps_new_simple (GST_NATIVE_BUFFER_NAME,
      "width", G_TYPE_INT, pool->width,
      "height", G_TYPE_INT, pool->height,
      "framerate", GST_TYPE_FRACTION, pool->fps_n, pool->fps_d,
      "format", G_TYPE_INT, pool->format,
      "orientation-angle", G_TYPE_INT, pool->orientation, NULL);

  GST_DEBUG_OBJECT (pool, "setting buffer caps to %" GST_PTR_FORMAT, caps);

  gst_buffer_set_caps (GST_BUFFER (buffer), caps);
  gst_caps_unref (caps);

  gst_native_buffer_set_finalize_callback (buffer,
      gst_camera_buffer_pool_resurrect_buffer, pool);

  g_ptr_array_add (pool->buffers, buffer);

  g_mutex_lock (&pool->hal_lock);

  g_queue_push_tail (pool->hal_queue, buffer);

  g_cond_signal (&pool->hal_cond);

  g_mutex_unlock (&pool->hal_lock);

  return TRUE;
}

static GstCameraBufferPool *
gst_camera_buffer_pool_get (const struct preview_stream_ops *ops)
{
  return container_of (ops, GstCameraBufferPool, window);
}

static GstNativeBuffer *
gst_camera_buffer_pool_get_buffer (buffer_handle_t * buffer)
{
  return gst_native_buffer_find (buffer);
}

static int
gst_camera_buffer_pool_set_buffers_geometry (struct preview_stream_ops *w,
    int width, int height, int format)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  GST_DEBUG_OBJECT (pool, "set buffers geometry from %dx%d@0x%x to %dx%d@0x%x",
      pool->width, pool->height, pool->format, width, height, format);

  if (pool->width == width && pool->height == height && pool->format == format) {
    GST_DEBUG_OBJECT (pool, "same geometry and format. Nothing to do");
    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);
    return 0;
  } else if (pool->width != 0 || pool->height != 0 || pool->format != 0) {
    GST_WARNING_OBJECT (pool, "geometry previously set");

    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

    return -EINVAL;
  }

  GST_LOG_OBJECT (pool,
      "Setting geometry: width = %d, height = %d, format = %d (0x%x)", width,
      height, format, format);

  pool->width = width;
  pool->height = height;
  pool->format = format;

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  return 0;
}

static int
gst_camera_buffer_pool_set_buffer_count (struct preview_stream_ops *w,
    int count)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  GST_DEBUG_OBJECT (pool, "set buffer count from %d to %d", pool->count, count);

  if (pool->count == count) {
    GST_DEBUG_OBJECT (pool, "same count. Nothing to do");
    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);
    return 0;
  } else if (pool->count != 0) {
    GST_WARNING_OBJECT (pool, "count previously set");

    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

    return -EINVAL;
  }

  GST_LOG_OBJECT (pool, "Setting buffer count to %d", count);

  pool->count = count;

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  return 0;
}

static int
gst_camera_buffer_pool_set_crop (struct preview_stream_ops *w, int left,
    int top, int right, int bottom)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  GST_DEBUG_OBJECT (pool, "set crop");

  if (pool->left != 0 || pool->top != 0 || pool->right != 0
      || pool->bottom != 0) {
    GST_WARNING_OBJECT (pool, "crop previously set");

    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

    return -EINVAL;
  }

  GST_LOG_OBJECT (pool,
      "Setting crop to left: %d, top: %d, right: %d, bottom: %d", left, top,
      right, bottom);

  pool->left = left;
  pool->top = top;
  pool->right = right;
  pool->bottom = bottom;

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  return 0;
}

static int
gst_camera_buffer_pool_set_usage (struct preview_stream_ops *w, int usage)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  GST_DEBUG_OBJECT (pool, "set usage from 0x%x to 0x%x", pool->usage, usage);

  if (pool->usage == usage) {
    GST_DEBUG_OBJECT (pool, "same usage. Nothing to do");
    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);
    return 0;
  } else if (pool->usage != 0) {
    GST_WARNING_OBJECT (pool, "usage previously set");

    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

    return -EINVAL;
  }

  GST_LOG_OBJECT (pool, "Setting usage to 0x%x", usage);

  pool->usage = usage;

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  return 0;
}

static int
gst_camera_buffer_pool_set_swap_interval (struct preview_stream_ops *w,
    int interval)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  GST_DEBUG_OBJECT (pool, "set swap interval");

  if (pool->swap_interval != 0) {
    GST_WARNING_OBJECT (pool, "swap interval previously set");

    GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

    return -EINVAL;
  }

  GST_LOG_OBJECT (pool, "Setting swap interval to %x", interval);

  pool->swap_interval = interval;

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  return 0;
}

static int
gst_camera_buffer_pool_get_min_undequeued_buffer_count (const struct
    preview_stream_ops *w, int *count)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_DEBUG_OBJECT (pool, "get min undequeued buffer count");

  GST_LOG_OBJECT (pool, "min undequeued buffer count = %d",
      MIN_UNDEQUEUED_BUFFER_COUNT);

  *count = MIN_UNDEQUEUED_BUFFER_COUNT;

  return 0;
}

static int
gst_camera_buffer_pool_dequeue_buffer (struct preview_stream_ops *w,
    buffer_handle_t ** buffer, int *stride)
{
  GstNativeBuffer *buff;

  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_DEBUG_OBJECT (pool, "dequeue buffer");

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  while (pool->buffers->len < pool->count) {
    if (!gst_camera_buffer_pool_allocate_and_add_unlocked (pool)) {
      GST_CAMERA_BUFFER_POOL_UNLOCK (pool);
      return -ENOMEM;
    }
  }

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  g_mutex_lock (&pool->hal_lock);

  GST_LOG_OBJECT (pool, "hal queue size: %i", pool->hal_queue->length);

  /*
   * TODO: We have an issue here.
   * Qualcomm camera HAL queues the preview buffer and then tries to dequeue a buffer
   * immediately. GStreamer sink always keeps a reference to the buffer so dequeue
   * will always block waiting for the buffer to be returned from the sink which will never
   * happen and we will simply deadlock.
   * It seems that Qualcomm HAL is tolerant to the error we return here. To be tested
   * with camera HAL from other vendors.
   */
  if (pool->hal_queue->length == 0) {
    GST_DEBUG_OBJECT (pool, "waiting for buffer");
    GTimeVal tv;
    g_get_current_time (&tv);
    g_time_val_add (&tv, MAX_DEQUEUE_TIMEOUT_MS * 1000);        /* in microseconds */
    g_cond_timed_wait (&pool->hal_cond, &pool->hal_lock, &tv);

#if 0
    g_cond_wait (&pool->hal_cond, &pool->hal_lock);
#endif

    GST_DEBUG_OBJECT (pool, "done waiting for buffer");
  }

  buff = g_queue_pop_head (pool->hal_queue);

  g_mutex_unlock (&pool->hal_lock);

  if (!buff) {
    /* TODO: Not really sure what to do here */
    GST_WARNING_OBJECT (pool, "no buffer");
    return -EINVAL;
  }

  *stride = gst_native_buffer_get_stride (buff);
  *buffer = gst_native_buffer_get_handle (buff);

  GST_DEBUG_OBJECT (pool, "dequeueing buffer %p", buff);

  return 0;
}

static void
gst_camera_buffer_pool_set_buffer_metadata (GstCameraBufferPool * pool,
    GstNativeBuffer * buffer)
{
  GstBuffer *buff = GST_BUFFER (buffer);

  GST_DEBUG_OBJECT (pool, "set buffer metadata");

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  GST_BUFFER_OFFSET (buff) = pool->frames++;
  GST_BUFFER_OFFSET_END (buff) = pool->frames;

  /* TODO: We don't know how the timestamp format really is.
     This assertion is there until we get Android HAL which sends timestamp
     and we know what to do with it. */
  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  g_return_if_fail (pool->last_timestamp == 0);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  if (!pool->last_timestamp) {
    GstClock *clock;
    GstClockTime timestamp;
    GstClockTime base_time;

    GST_OBJECT_LOCK (pool->src);
    clock = GST_ELEMENT_CLOCK (pool->src);
    base_time = pool->src->base_time;

    if (clock) {
      timestamp = gst_clock_get_time (clock) - pool->src->base_time;
    } else {
      timestamp = GST_CLOCK_TIME_NONE;
    }

    GST_OBJECT_UNLOCK (pool->src);

    if (timestamp > pool->buffer_duration) {
      timestamp -= pool->buffer_duration;
    }

    GST_BUFFER_TIMESTAMP (buff) = timestamp;

    GST_LOG_OBJECT (pool, "buffer timestamp set to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));

    GST_LOG_OBJECT (pool, "base time is now %" GST_TIME_FORMAT,
        GST_TIME_ARGS (base_time));
  }

  GST_BUFFER_DURATION (buff) = pool->buffer_duration;

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);
}

static int
gst_camera_buffer_pool_enqueue_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);
  GstNativeBuffer *buff = gst_camera_buffer_pool_get_buffer (buffer);
  gboolean flushing;

  GST_DEBUG_OBJECT (pool, "enqueue buffer %p", buff);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);
  flushing = pool->flushing;
  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  if (flushing) {
    GST_DEBUG_OBJECT (pool,
        "pool is flushing. Pushing buffer %p back to camera HAL", buff);

    g_mutex_lock (&pool->hal_lock);

    g_queue_push_tail (pool->hal_queue, buff);

    g_cond_signal (&pool->hal_cond);

    GST_LOG_OBJECT (pool, "hal queue size: %i", pool->hal_queue->length);

    g_mutex_unlock (&pool->hal_lock);
  } else {
    gst_camera_buffer_pool_set_buffer_metadata (pool, buff);

    GST_DEBUG_OBJECT (pool, "Pushing buffer %p to application queue", buff);

    g_mutex_lock (&pool->app_lock);

    g_queue_push_tail (pool->app_queue, buff);

    g_cond_signal (&pool->app_cond);

    g_mutex_unlock (&pool->app_lock);
  }

  return 0;
}

static int
gst_camera_buffer_pool_cancel_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);
  GstNativeBuffer *buff = gst_camera_buffer_pool_get_buffer (buffer);

  GST_DEBUG_OBJECT (pool, "cancel buffer: %p", buff);

  g_mutex_lock (&pool->hal_lock);

  g_queue_push_tail (pool->hal_queue, buff);

  g_cond_signal (&pool->hal_cond);

  GST_LOG_OBJECT (pool, "hal queue size: %i", pool->hal_queue->length);

  g_mutex_unlock (&pool->hal_lock);

  return 0;
}

static int
gst_camera_buffer_pool_lock_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  /* TODO: What should we do here ? */
  GST_DEBUG_OBJECT (pool, "lock buffer");

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  return 0;
}

static int
gst_camera_buffer_pool_set_timestamp (struct preview_stream_ops *w,
    int64_t timestamp)
{
  GstCameraBufferPool *pool = gst_camera_buffer_pool_get (w);

  GST_CAMERA_BUFFER_POOL_LOCK (pool);

  GST_DEBUG_OBJECT (pool, "set timestamp");

  pool->last_timestamp = timestamp;

  GST_LOG_OBJECT (pool, "setting timestamp to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  GST_CAMERA_BUFFER_POOL_UNLOCK (pool);

  return 0;
}

static void
gst_camera_buffer_pool_init (GstCameraBufferPool * pool)
{
  pool->buffer_duration = GST_CLOCK_TIME_NONE;
  pool->flushing = TRUE;
  pool->frames = 0;
  pool->fps_n = 0;
  pool->fps_d = 0;
  pool->orientation = -1;

  g_mutex_init (&pool->lock);

  pool->buffers = g_ptr_array_new ();

  pool->hal_queue = g_queue_new ();
  g_mutex_init (&pool->hal_lock);
  g_cond_init (&pool->hal_cond);

  pool->app_queue = g_queue_new ();
  g_mutex_init (&pool->app_lock);
  g_cond_init (&pool->app_cond);

  pool->window.set_buffer_count = gst_camera_buffer_pool_set_buffer_count;
  pool->window.set_buffers_geometry =
      gst_camera_buffer_pool_set_buffers_geometry;
  pool->window.set_crop = gst_camera_buffer_pool_set_crop;
  pool->window.set_usage = gst_camera_buffer_pool_set_usage;
  pool->window.set_swap_interval = gst_camera_buffer_pool_set_swap_interval;
  pool->window.get_min_undequeued_buffer_count =
      gst_camera_buffer_pool_get_min_undequeued_buffer_count;
  pool->window.dequeue_buffer = gst_camera_buffer_pool_dequeue_buffer;
  pool->window.enqueue_buffer = gst_camera_buffer_pool_enqueue_buffer;
  pool->window.cancel_buffer = gst_camera_buffer_pool_cancel_buffer;
  pool->window.lock_buffer = gst_camera_buffer_pool_lock_buffer;
  pool->window.set_timestamp = gst_camera_buffer_pool_set_timestamp;
}

static void
gst_camera_buffer_pool_finalize (GstCameraBufferPool * pool)
{
  GST_DEBUG_OBJECT (pool, "finalize");

  while (pool->buffers->len > 0) {
    GstNativeBuffer *buffer = g_ptr_array_index (pool->buffers, 0);
    GST_DEBUG_OBJECT (pool, "free buffer %p", buffer);

    /* We will not free the buffer for now.
     * It will be freed whenever its reference count drops to 0 */
    gst_native_buffer_set_finalize_callback (buffer,
        gst_camera_buffer_pool_free_buffer, pool);

    g_ptr_array_remove (pool->buffers, buffer);

    gst_buffer_unref (GST_BUFFER (buffer));
  }

  g_ptr_array_free (pool->buffers, TRUE);

  g_mutex_clear (&pool->hal_lock);
  g_cond_clear (&pool->hal_cond);
  g_queue_free (pool->hal_queue);

  g_mutex_clear (&pool->app_lock);
  g_cond_clear (&pool->app_cond);
  g_queue_free (pool->app_queue);

  gst_gralloc_unref (pool->gralloc);
  gst_object_unref (pool->src);

  g_mutex_clear (&pool->lock);

  GST_MINI_OBJECT_CLASS (parent_class)->finalize (GST_MINI_OBJECT (pool));
}

void
gst_camera_buffer_pool_drain_app_queue (GstCameraBufferPool * pool)
{
  int count;

  GST_DEBUG_OBJECT (pool, "drain app queue");

  count = 0;

  g_mutex_lock (&pool->app_lock);
  g_mutex_lock (&pool->hal_lock);

  while (pool->app_queue->length > 0) {
    GstBuffer *buffer = g_queue_pop_head (pool->app_queue);

    GST_LOG_OBJECT (pool, "popped buffer %p", buffer);

    g_queue_push_tail (pool->hal_queue, buffer);

    ++count;
  }

  if (count > 0) {
    g_cond_signal (&pool->hal_cond);
  }

  g_mutex_unlock (&pool->hal_lock);
  g_mutex_unlock (&pool->app_lock);

  GST_DEBUG_OBJECT (pool, "popped %d buffers from app queue", count);
}
