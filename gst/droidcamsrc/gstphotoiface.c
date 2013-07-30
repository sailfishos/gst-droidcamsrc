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

#include "gstphotoiface.h"
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#include <gst/interfaces/photography.h>
#undef GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include "gstdroidcamsrc.h"
#include "cameraparams.h"

static void
gst_photo_iface_implements_interface_init (GstImplementsInterfaceClass * klass);
static gboolean
gst_photo_iface_implements_iface_supported (GstImplementsInterface * iface,
    GType iface_type);
static void gst_photo_iface_photo_interface_init (GstPhotographyInterface *
    iface);

void
gst_photo_iface_init (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_photo_iface_implements_interface_init,
    NULL,
    NULL,
  };

  static const GInterfaceInfo photo_iface_info = {
    (GInterfaceInitFunc) gst_photo_iface_photo_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);

  g_type_add_interface_static (type, GST_TYPE_PHOTOGRAPHY, &photo_iface_info);
}

static void
gst_photo_iface_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_photo_iface_implements_iface_supported;
}

static gboolean
gst_photo_iface_implements_iface_supported (GstImplementsInterface * iface,
    GType iface_type)
{
  return iface_type == GST_TYPE_PHOTOGRAPHY;
}

static void
gst_photo_iface_photo_interface_init (GstPhotographyInterface * iface)
{
  // TODO:
}

void
gst_photo_iface_add_properties (GObjectClass * gobject_class)
{
  // TODO:
}

gboolean
gst_photo_iface_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {

  }

  return FALSE;
}

gboolean
gst_photo_iface_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {

  }

  return FALSE;
}
