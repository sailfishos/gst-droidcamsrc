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

#ifndef __CAMERA_PARAMS_HH__
#define __CAMERA_PARAMS_HH__

#include <glib.h>

G_BEGIN_DECLS

void *camera_params_from_string(const char *str);
void camera_params_free(void *params);
char *camera_params_to_string(void *params);
void camera_params_dump(void *params);
void camera_params_set(void *p, const char *key, const char *val);

G_END_DECLS

#endif /* __CAMERA_PARAMS_HH__ */
