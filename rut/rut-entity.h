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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __RUT_ENTITY_H__
#define __RUT_ENTITY_H__

#include <stdint.h>

#include <cogl/cogl.h>

#include "rut-type.h"
#include "rut-object.h"
#include "rut-interfaces.h"
#include "rut-context.h"
#include "rut-image-source.h"

#define RUT_COMPONENT(X) ((RutComponent *)(X))
typedef struct _RutComponent RutComponent;

#define RUT_ENTITY(X) ((RutEntity *)X)
typedef struct _RutEntity RutEntity;
extern RutType rut_entity_type;

typedef enum
{
  RUT_COMPONENT_TYPE_ANIMATION_CLIP,
  RUT_COMPONENT_TYPE_CAMERA,
  RUT_COMPONENT_TYPE_LIGHT,
  RUT_COMPONENT_TYPE_GEOMETRY,
  RUT_COMPONENT_TYPE_MATERIAL,

  RUT_N_COMPNONENTS
} RutComponentType;

typedef struct _RutComponentableProps
{
  RutComponentType type;
  RutEntity *entity; /* back pointer to the entity the component belongs to */
} RutComponentableProps;

typedef struct _RutComponentableVTable
{
  void (*start)   (RutObject *component);
  void (*update)  (RutObject *component, int64_t time);
  void (*draw)    (RutObject *component, CoglFramebuffer *fb);

} RutComponentableVTable;

typedef enum
{
  RUT_ENTITY_FLAG_NONE        = 0,
  RUT_ENTITY_FLAG_DIRTY       = 1 << 0,
  RUT_ENTITY_FLAG_CAST_SHADOW = 1 << 1,
} RutEntityFlag;

void
_rut_entity_init_type (void);

CoglBool
rut_entity_get_cast_shadow (RutObject *entity);

RutEntity *
rut_entity_new (RutContext *ctx);

RutContext *
rut_entity_get_context (RutEntity *entity);

const char *
rut_entity_get_label (RutObject *entity);

void
rut_entity_set_label (RutObject *entity,
                      const char *label);

float
rut_entity_get_x (RutObject *entity);

void
rut_entity_set_x (RutObject *entity,
                  float x);

float
rut_entity_get_y (RutObject *entity);

void
rut_entity_set_y (RutObject *entity,
                  float y);

float
rut_entity_get_z (RutObject *entity);

void
rut_entity_set_z (RutObject *entity,
                  float z);

const float *
rut_entity_get_position (RutObject *entity);

void
rut_entity_set_position (RutObject *entity,
                         const float position[3]);

void
rut_entity_get_transformed_position (RutEntity *entity,
                                     float position[3]);

const CoglQuaternion *
rut_entity_get_rotation (RutObject *entity);

void
rut_entity_set_rotation (RutObject *entity,
                         const CoglQuaternion *rotation);

void
rut_entity_apply_rotations (RutObject *entity,
                            CoglQuaternion *rotations);

void
rut_entity_get_rotations (RutObject *entity,
                          CoglQuaternion *rotation);

void
rut_entity_get_view_rotations (RutObject *entity,
                               RutObject *camera_entity,
                               CoglQuaternion *rotation);

float
rut_entity_get_scale (RutObject *entity);

void
rut_entity_set_scale (RutObject *entity,
                      float scale);

float
rut_entity_get_scales (RutObject *entity);

const CoglMatrix *
rut_entity_get_transform (RutObject *self);

void
rut_entity_add_component (RutEntity *entity,
                          RutObject *component);

void
rut_entity_remove_component (RutEntity *entity,
                             RutObject *component);

void
rut_entity_update (RutEntity *entity,
                   int64_t time);
void
rut_entity_draw (RutEntity *entity,
                 CoglFramebuffer *fb);
void
rut_entity_translate (RutEntity *entity,
                      float tx,
                      float tz,
                      float ty);

void
rut_entity_set_translate (RutEntity *entity,
                          float tx,
                          float ty,
                          float tz);

void
rut_entity_rotate_x_axis (RutEntity *entity,
                          float x_angle);
void
rut_entity_rotate_y_axis (RutEntity *entity,
                          float y_angle);
void
rut_entity_rotate_z_axis (RutEntity *entity,
                          float z_angle);

RutObject *
rut_entity_get_component (RutEntity *entity,
                          RutComponentType type);

void
rut_entity_set_cast_shadow (RutObject *entity,
                            gboolean cast_shadow);

CoglBool
rut_entity_get_receive_shadow (RutObject *entity);

void
rut_entity_set_receive_shadow (RutObject *entity,
                               gboolean receive_shadow);

typedef void (*RutComponentCallback) (RutComponent *component,
                                      void *user_data);

void
rut_entity_foreach_component (RutEntity *entity,
                              RutComponentCallback callback,
                              void *user_data);

void
rut_entity_set_pipeline_cache (RutEntity *entity,
                               int slot,
                               CoglPipeline *pipeline);

CoglPipeline *
rut_entity_get_pipeline_cache (RutEntity *entity,
                               int slot);

void
rut_entity_set_image_source_cache (RutEntity *entity,
                                   int slot,
                                   RutImageSource *src);

RutImageSource *
rut_entity_get_image_source_cache (RutEntity *entity,
                                   int slot);


void
rut_entity_set_primitive_cache (RutEntity *entity,
                                int slot,
                                CoglPrimitive *primitive);

CoglPrimitive *
rut_entity_get_primitive_cache (RutEntity *entity,
                                int slot);

CoglBool
rut_entity_get_visible (RutObject *entity);

void
rut_entity_set_visible (RutObject *entity, CoglBool visible);

#endif /* __RUT_ENTITY_H__ */
