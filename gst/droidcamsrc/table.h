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

#ifndef __TABLE_H__
#define __TABLE_H__

typedef struct {
  const char *str;
  int val;
} TableEntry;

TableEntry gst_droid_cam_src_flash_table[] = {
  {"auto", GST_PHOTOGRAPHY_FLASH_MODE_AUTO},
  {"off",  GST_PHOTOGRAPHY_FLASH_MODE_OFF},
  {"on",   GST_PHOTOGRAPHY_FLASH_MODE_ON},
  /*  {"",     GST_PHOTOGRAPHY_FLASH_MODE_RED_EYE}, */
  {NULL,   -1}
};

TableEntry gst_droid_cam_src_focus_table[] = {
  {"auto",       GST_PHOTOGRAPHY_FOCUS_MODE_AUTO},
  {"macro",      GST_PHOTOGRAPHY_FOCUS_MODE_MACRO},
  /*  {"", GST_PHOTOGRAPHY_FOCUS_MODE_PORTRAIT}, */
  {"infinity",   GST_PHOTOGRAPHY_FOCUS_MODE_INFINITY},
  {"fixed",      GST_PHOTOGRAPHY_FOCUS_MODE_HYPERFOCAL},
  {"edof",       GST_PHOTOGRAPHY_FOCUS_MODE_EXTENDED},
  /* There is no continuous mode for Android but we will pick either video or image continuous modes
   * depending on the active camera mode */
  {"continuous", GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL},
  {"continuous", GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_EXTENDED},
  {NULL,   -1}
};

TableEntry gst_droid_cam_src_white_balance_table[] = {
  {"auto",            GST_PHOTOGRAPHY_WB_MODE_AUTO},
  {"incandescent",    GST_PHOTOGRAPHY_WB_MODE_TUNGSTEN},
  {"fluorescent",     GST_PHOTOGRAPHY_WB_MODE_FLUORESCENT},
  {"daylight",        GST_PHOTOGRAPHY_WB_MODE_DAYLIGHT},
  {"cloudy-daylight", GST_PHOTOGRAPHY_WB_MODE_CLOUDY},
  {NULL,              -1}
};

const char *gst_droid_cam_src_find_droid (TableEntry *entries, gint val) {
  int x = 0;

  while (entries[x].str) {
    if (entries[x].val == val) {
      return entries[x].str;
    }

    ++x;
  }

  return NULL;
}

int gst_droid_cam_src_find_photo (TableEntry *entries, const char *str) {
  int x = 0;

  while (entries[x].str) {
    if (!strcmp(entries[x].str, str)) {
      return entries[x].val;
    }

    ++x;
  }

  return -1;
}

#endif /* __TABLE_H__ */
