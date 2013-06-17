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

#ifndef __GST_CAMERA_BUFFER_POOL_H__
#define __GST_CAMERA_BUFFER_POOL_H__

#include <gst/gst.h>
#include <gst/gstminiobject.h>
#include "android/camera.h"
#include <gst/gstgralloc.h>


G_BEGIN_DECLS

typedef struct _GstCameraBufferPool GstCameraBufferPool;
typedef struct _GstCameraBufferPoolClass GstCameraBufferPoolClass;

#define GST_TYPE_CAMERA_BUFFER_POOL            (gst_camera_buffer_pool_get_type())
#define GST_IS_CAMERA_BUFFER_POOL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_CAMERA_BUFFER_POOL))
#define GST_IS_CAMERA_BUFFER_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_CAMERA_BUFFER_POOL))
#define GST_CAMERA_BUFFER_POOL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_CAMERA_BUFFER_POOL, GstCameraBufferPoolClass))
#define GST_CAMERA_BUFFER_POOL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_CAMERA_BUFFER_POOL, GstCameraBufferPool))
#define GST_CAMERA_BUFFER_POOL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_CAMERA_BUFFER_POOL, GstCameraBufferPoolClass))

#define GST_CAMERA_BUFFER_POOL_LOCK(p) g_mutex_lock (&p->lock)
#define GST_CAMERA_BUFFER_POOL_UNLOCK(p) g_mutex_unlock (&p->lock)

struct _GstCameraBufferPool {
  GstMiniObject parent;

  GstGralloc *gralloc;
  GstElement *src;

  GMutex lock;

  GPtrArray *buffers;

  /* Queue for HAL */
  GQueue *hal_queue;
  GCond hal_cond;
  GMutex hal_lock;

  /* Queue for APP */
  GQueue *app_queue;
  GMutex app_lock;
  GCond app_cond;

  struct preview_stream_ops window;

  int count;
  gint width;
  gint height;
  gint format;

  gint top;
  gint left;
  gint right;
  gint bottom;

  gint swap_interval;

  int64_t last_timestamp;

  gint usage;

  int frames;
  gboolean flushing;

  GstClockTime buffer_duration;
  int fps_n;
  int fps_d;
};

struct _GstCameraBufferPoolClass {
  GstMiniObjectClass parent_class;
};

GType           gst_camera_buffer_pool_get_type           (void);

GstCameraBufferPool *gst_camera_buffer_pool_new (GstElement * src, GstGralloc * gralloc);

void gst_camera_buffer_pool_unlock_hal_queue (GstCameraBufferPool * pool);
void gst_camera_buffer_pool_unlock_app_queue (GstCameraBufferPool * pool);
void gst_camera_buffer_pool_drain_app_queue (GstCameraBufferPool * pool);

G_INLINE_FUNC GstCameraBufferPool *gst_camera_buffer_pool_ref (GstCameraBufferPool * pool);
G_INLINE_FUNC void gst_camera_buffer_pool_unref (GstCameraBufferPool * pool);

static inline GstCameraBufferPool *
gst_camera_buffer_pool_ref (GstCameraBufferPool * pool)
{
  return (GstCameraBufferPool *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (pool));
}

static inline void
gst_camera_buffer_pool_unref (GstCameraBufferPool * pool)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (pool));
}

G_END_DECLS

#endif /* __GST_CAMERA_BUFFER_POOL_H__  */
