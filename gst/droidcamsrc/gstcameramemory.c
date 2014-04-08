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

#include "gstcameramemory.h"

#include <sys/mman.h>

GST_DEBUG_CATEGORY_STATIC (droidcameramemory_debug);
#define GST_CAT_DEFAULT droidcameramemory_debug

typedef struct _GstCameraMemoryAllocator GstCameraMemoryAllocator;
typedef struct _GstCameraMemoryAllocatorClass GstCameraMemoryAllocatorClass;

struct _GstCameraMemoryAllocator
{
  GstAllocator parent;

  camera_memory_t mem;
  int fd;
  size_t buf_size;
  guint num_bufs;
  void *data;
};

struct _GstCameraMemoryAllocatorClass
{
  GstAllocatorClass parent_class;
};

typedef struct _GstCameraMemory GstCameraMemory;
struct _GstCameraMemory
{
  GstMemory mem;

  void *data;
  GstDroidCamSrc *src;
};

static gboolean gst_camera_memory_get_mmap (GstCameraMemoryAllocator * alloc);
static gboolean gst_camera_memory_get_malloc (GstCameraMemoryAllocator * alloc);
static void gst_camera_memory_release (struct camera_memory *mem);

static gpointer
gst_camera_memory_map (GstMemory * mem, gsize maxsize,
    GstMapFlags flags)
{
  GstCameraMemory *memory = (GstCameraMemory *) mem;

  if (flags & GST_MAP_WRITE) {
    return NULL;
  }

  return memory->data;
}

static void
gst_camera_memory_unmap (GstMemory * mem)
{
}

GType gst_camera_memory_allocator_get_type (void);
G_DEFINE_TYPE (GstCameraMemoryAllocator,
    gst_camera_memory_allocator, GST_TYPE_ALLOCATOR);

#define GST_TYPE_CAMERA_MEMORY_ALLOCATOR      (gst_camera_memory_allocator_get_type())
#define GST_IS_CAMERA_MEMORY_ALLOCATOR(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_CAMERA_MEMORY_ALLOCATOR))
#define GST_CAMERA_MEMORY_ALLOCATOR(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_CAMERA_MEMORY_ALLOCATOR, GstCameraMemoryAllocator))
#define GST_CAMERA_MEMORY_ALLOCATOR_CAST(obj) ((GstCameraMemoryAllocator*) (obj))

static GstMemory *
gst_camera_memory_allocator_alloc (GstAllocator * alloc, gsize size,
    GstAllocationParams * params)
{
  g_assert_not_reached ();
  return NULL;
}

static void
gst_camera_memory_allocator_free (GstAllocator * alloc, GstMemory * mem)
{
  GstCameraMemory *memory = (GstCameraMemory *) mem;

  if (memory->src) {
    GST_PAD_STREAM_LOCK (memory->src->vidsrc);
    if (memory->src->video_capture_status == VIDEO_CAPTURE_RUNNING) {
      memory->src->dev->ops->release_recording_frame (memory->src->dev,
          memory->data);
    }
    GST_PAD_STREAM_UNLOCK (memory->src->vidsrc);

    g_mutex_lock (&memory->src->num_video_frames_lock);
    if (--memory->src->pushed_video_frames == 0) {
      g_cond_signal (&memory->src->num_video_frames_cond);
    }
    g_mutex_unlock (&memory->src->num_video_frames_lock);

    gst_object_unref (memory->src);
  }

  g_slice_free (GstCameraMemory, memory);
}

static void
gst_camera_memory_allocator_finalize (GObject * object)
{
  GstCameraMemoryAllocator * allocator = GST_CAMERA_MEMORY_ALLOCATOR
      (object);

  if (allocator->fd < 0) {
    g_slice_free1 (allocator->mem.size, allocator->mem.data);
  } else {
    munmap (allocator->mem.data, allocator->mem.size);
  }

  G_OBJECT_CLASS (gst_camera_memory_allocator_parent_class)->finalize
      (object);
}

static void
gst_camera_memory_allocator_class_init (
    GstCameraMemoryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;
  GObjectClass *gobject = (GObjectClass *) klass;

  allocator_class->alloc = gst_camera_memory_allocator_alloc;
  allocator_class->free = gst_camera_memory_allocator_free;

  gobject->finalize = gst_camera_memory_allocator_finalize;

  GST_DEBUG_CATEGORY_INIT (droidcameramemory_debug, "droidcameramemory", 0,
      "Android camera memory allocation");
}

static void
gst_camera_memory_allocator_init (
    GstCameraMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "GstDroidCameraMemory";
  alloc->mem_map = gst_camera_memory_map;
  alloc->mem_unmap = gst_camera_memory_unmap;

  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}


camera_memory_t *
gst_camera_memory_get (int fd, size_t buf_size, unsigned int num_bufs,
    void *user_data)
{
  GstCameraMemoryAllocator *allocator = g_object_new
      (gst_camera_memory_allocator_get_type(), NULL);
  gboolean res;
  size_t size = buf_size * num_bufs;

  allocator->fd = fd;
  allocator->buf_size = buf_size;
  allocator->num_bufs = num_bufs;
  allocator->data = NULL;
  allocator->mem.size = size;
  allocator->mem.handle = allocator;
  allocator->mem.release = gst_camera_memory_release;

  if (fd != -1) {
    res = gst_camera_memory_get_mmap (allocator);
  } else {
    res = gst_camera_memory_get_malloc (allocator);
  }

  if (res == FALSE) {
    gst_object_unref (allocator);
    return NULL;
  }

  return &allocator->mem;
}

static gboolean
gst_camera_memory_get_mmap (GstCameraMemoryAllocator * alloc)
{
  alloc->mem.data =
      mmap (0, alloc->mem.size, PROT_READ | PROT_WRITE, MAP_SHARED, alloc->fd, 0);
  if (alloc->mem.data == MAP_FAILED) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_camera_memory_get_malloc (GstCameraMemoryAllocator * alloc)
{
  alloc->mem.data = g_slice_alloc (alloc->mem.size);
  if (!alloc->mem.data) {
    return FALSE;
  }

  return TRUE;
}

static void
gst_camera_memory_release (struct camera_memory *mem)
{
  GstCameraMemoryAllocator *alloc = (GstCameraMemoryAllocator *) mem->handle;

  gst_object_unref (alloc);
}

GstMemory *
gst_camera_memory_new (const camera_memory_t *camera_memory, unsigned int index)
{
  GstCameraMemoryAllocator *allocator
      = (GstCameraMemoryAllocator *) camera_memory->handle;
  GstCameraMemory *memory = g_slice_new (GstCameraMemory);

  gst_memory_init (GST_MEMORY_CAST (memory), GST_MEMORY_FLAG_READONLY,
      GST_ALLOCATOR(allocator), NULL, allocator->buf_size, 4, 0,
      allocator->buf_size);

  memory->data = allocator->mem.data + (index * allocator->buf_size);
  memory->src = NULL;

  return (GstMemory *) memory;
}

GstMemory *
gst_camera_memory_new_video (const camera_memory_t *camera_memory,
    unsigned int index, GstDroidCamSrc *src)
{
  GstCameraMemoryAllocator *allocator
      = (GstCameraMemoryAllocator *) camera_memory->handle;
  GstCameraMemory *memory = g_slice_new (GstCameraMemory);

  gst_memory_init (GST_MEMORY_CAST (memory), GST_MEMORY_FLAG_READONLY,
      GST_ALLOCATOR(allocator), NULL, allocator->buf_size, 4, 0,
      allocator->buf_size);

  memory->data = allocator->mem.data + (index * allocator->buf_size);
  memory->src = src;

  gst_object_ref (memory->src);

  return (GstMemory *) memory;
}
