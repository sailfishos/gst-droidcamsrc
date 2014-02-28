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

#ifndef __GST_IMG_SRC_PAD_H__
#define __GST_IMG_SRC_PAD_H__

#include <gst/gst.h>
#include "gstdroidcamsrc.h"

G_BEGIN_DECLS

GstPad *gst_img_src_pad_new (GstStaticPadTemplate *pad_template, const char * name);
GstCaps *gst_img_src_pad_get_supported_caps_unlocked (GstDroidCamSrc *src);
gboolean gst_img_src_pad_renegotiate (GstPad * pad);

G_END_DECLS

#endif /* __GST_IMG_SRC_PAD_H__ */
