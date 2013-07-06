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
#include <glib.h>
#include <sys/mman.h>
#include <stdio.h>              /* perror() */

typedef struct
{
  camera_memory_t mem;
  int fd;
  size_t buf_size;
  guint num_bufs;
  void *data;
} GstCameraMemory;

static gboolean gst_camera_memory_get_mmap (GstCameraMemory * mem);
static gboolean gst_camera_memory_get_malloc (GstCameraMemory * mem);
static void gst_camera_memory_release (struct camera_memory *mem);

camera_memory_t *
gst_camera_memory_get (int fd, size_t buf_size, unsigned int num_bufs,
    void *data)
{
  GstCameraMemory *mem = g_slice_new0 (GstCameraMemory);
  gboolean res;
  mem->fd = fd;
  mem->buf_size = buf_size;
  mem->num_bufs = num_bufs;
  mem->data = data;
  mem->mem.size = buf_size * num_bufs;
  mem->mem.handle = mem;
  mem->mem.release = gst_camera_memory_release;

  if (fd != -1) {
    res = gst_camera_memory_get_mmap (mem);
  } else {
    res = gst_camera_memory_get_malloc (mem);
  }

  if (res == FALSE) {
    g_slice_free (GstCameraMemory, mem);
    return NULL;
  }

  return &mem->mem;
}

static gboolean
gst_camera_memory_get_mmap (GstCameraMemory * mem)
{
  mem->mem.data =
      mmap (0, mem->mem.size, PROT_READ | PROT_WRITE, MAP_SHARED, mem->fd, 0);
  if (mem->mem.data == MAP_FAILED) {
    perror ("mmap");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_camera_memory_get_malloc (GstCameraMemory * mem)
{
  mem->mem.data = g_slice_alloc (mem->mem.size);
  if (!mem->mem.data) {
    perror ("malloc");
    return FALSE;
  }

  return TRUE;
}

static void
gst_camera_memory_release (struct camera_memory *mem)
{
  GstCameraMemory *cm = (GstCameraMemory *) mem->handle;

  if (cm->fd < 0) {
    g_slice_free1 (cm->mem.size, cm->mem.data);
  } else {
    munmap (cm->mem.data, cm->mem.size);
  }

  g_slice_free (GstCameraMemory, cm);

  mem = NULL;
}

void *
gst_camera_memory_get_data (const camera_memory_t * data, int index, int *size)
{
  unsigned long offset;
  void *buffer;

  GstCameraMemory *cm = (GstCameraMemory *) data->handle;

  if (index >= cm->num_bufs) {
    return NULL;
  }

  offset = index * cm->buf_size;

  buffer = cm->mem.data;

  buffer += offset;

  *size = cm->buf_size;

  return buffer;
}
