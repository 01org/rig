/*
 * Rut
 *
 * Copyright (C) 2012  Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __RUT_IMAGE_SOURCE_H__
#define __RUT_IMAGE_SOURCE_H__

#include <cogl-gst/cogl-gst.h>
#include "rut-type.h"
#include "rut-object.h"
#include "rut-interfaces.h"
#include "rut-context.h"
#include "rut-asset.h"

typedef struct _RutImageSource RutImageSource;
#define RUT_IMAGE_SOURCE(p) ((RutImageSource *)(p))
extern RutType rut_image_source_type;

void _rut_image_source_init_type (void);

RutImageSource*
rut_image_source_new (RutContext *ctx,
                      RutAsset *asset);

CoglTexture*
rut_image_source_get_texture (RutImageSource *source);

CoglGstVideoSink*
rut_image_source_get_sink (RutImageSource *source);

CoglBool
rut_image_source_get_is_video (RutImageSource *source);

typedef void (*RutImageSourceReadyCallback) (RutImageSource *source,
                                             void *user_data);

typedef void (*RutImageSourceChangedCallback) (RutImageSource *source,
                                               void *user_data);
                                               
RutClosure *
rut_image_source_add_ready_callback (RutImageSource *source,
                                     RutImageSourceReadyCallback callback,
                                     void *user_data,
                                     RutClosureDestroyCallback destroy_cb);

RutClosure *
rut_image_source_add_on_changed_callback (RutImageSource *source,
                                          RutImageSourceChangedCallback callback,
                                          void *user_data,
                                          RutClosureDestroyCallback destroy_cb);

#endif
