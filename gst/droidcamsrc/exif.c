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

#include "exif.h"
#include <libexif/exif-data.h>
#include <libexif/exif-mem.h>

GstTagList *
gst_droid_cam_src_get_exif_tags (GstBuffer * input)
{
  guchar *raw_data;
  guint len = 0;
  guchar *data = NULL;
  ExifMem *mem = NULL;
  ExifData *exif = NULL;
  GstBuffer *buffer = NULL;
  GstTagList *tags = NULL;

  mem = exif_mem_new_default ();
  if (!mem) {
    goto cleanup;
  }

  exif = exif_data_new_mem (mem);
  if (!exif) {
    goto cleanup;
  }

  exif_data_set_data_type (exif, EXIF_DATA_TYPE_COMPRESSED);
  exif_data_load_data (exif, GST_BUFFER_DATA (input), GST_BUFFER_SIZE (input));

  exif_data_save_data (exif, &data, &len);
  if (len < 6) {
    goto cleanup;
  }

  len -= 6;
  raw_data = data;
  raw_data += 6;

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = raw_data;
  GST_BUFFER_SIZE (buffer) = len;
  GST_BUFFER_MALLOCDATA (buffer) = NULL;

  tags = gst_tag_list_from_exif_buffer_with_tiff_header (buffer);

  if (tags) {
    ExifEntry *e = exif_content_get_entry (exif->ifd[EXIF_IFD_EXIF],
        EXIF_TAG_ISO_SPEED_RATINGS);

    gst_tag_list_remove_tag (tags, GST_TAG_DEVICE_MANUFACTURER);
    gst_tag_list_remove_tag (tags, GST_TAG_DEVICE_MODEL);
    gst_tag_list_remove_tag (tags, GST_TAG_APPLICATION_NAME);
    gst_tag_list_remove_tag (tags, GST_TAG_DATE_TIME);
    /*
     * ISO seems to be dropped by gst_tag_list_from_exif_buffer_with_tiff_header ()
     * since EEIF 2.3 makes a new mess out of exif tags.
     * We eill behave as N9 does for now until we hit an iceberg :|
     */
    if (e) {
      guint16 iso = exif_get_short (e->data, EXIF_BYTE_ORDER_MOTOROLA);
      gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
          GST_TAG_CAPTURING_ISO_SPEED, iso, NULL);
    }
  }

cleanup:
  if (exif) {
    exif_data_unref (exif);
    exif = NULL;
  }

  if (len != 0) {
    exif_mem_free (mem, data);
  }

  if (mem) {
    exif_mem_unref (mem);
    mem = NULL;
  }

  if (buffer) {
    gst_buffer_unref (buffer);
  }

  return tags;
}
