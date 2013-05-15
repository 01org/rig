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

#include <config.h>

#include "rut-entity.h"
#include "rut.h"

/* XXX: at some point we should perhaps separate out the Rig rendering code
 * into a "Renderer" and let that code somehow define how many slots it wants
 * associated with an entity for caching state. */
#define N_PIPELINE_CACHE_SLOTS 3
#define N_IMAGE_SOURCE_CACHE_SLOTS 3
#define N_PRIMITIVE_CACHE_SLOTS 1

enum
{
  PROP_LABEL,
  PROP_VISIBLE,
  PROP_POSITION,
  PROP_ROTATION,
  PROP_SCALE,
  PROP_CAST_SHADOW,
  PROP_RECEIVE_SHADOW,

  N_PROPS
};

/* FIXME:
 *  - directly store the position in the transform matrix?
 */
struct _RutEntity
{
  RutObjectProps _parent;

  RutContext *ctx;
  int ref_count;

  uint32_t id;

  char *label;

  RutGraphableProps graphable;

  /* private fields */
  float position[3];
  CoglQuaternion rotation;
  float scale;                          /* uniform scaling only */
  CoglMatrix transform;

  GPtrArray *components;

  CoglPipeline *pipeline_caches[N_PIPELINE_CACHE_SLOTS];
  RutImageSource *image_source_caches[N_IMAGE_SOURCE_CACHE_SLOTS];
  CoglPrimitive *primitive_caches[N_PRIMITIVE_CACHE_SLOTS];

  RutSimpleIntrospectableProps introspectable;
  RutProperty properties[N_PROPS];

  unsigned int visible:1;
  unsigned int dirty:1;
  unsigned int cast_shadow:1;
  unsigned int receive_shadow:1;
};

static RutPropertySpec _rut_entity_prop_specs[] = {
  {
    .name = "label",
    .type = RUT_PROPERTY_TYPE_TEXT,
    .getter.text_type = rut_entity_get_label,
    .setter.text_type = rut_entity_set_label,
    .nick = "Label",
    .blurb = "A label for the entity",
    .flags = RUT_PROPERTY_FLAG_READWRITE
  },
  {
    .name = "visible",
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .getter.boolean_type = rut_entity_get_visible,
    .setter.boolean_type = rut_entity_set_visible,
    .nick = "Visible",
    .blurb = "Whether the entity is visible or not",
    .flags = RUT_PROPERTY_FLAG_READWRITE
  },
  {
    .name = "position",
    .type = RUT_PROPERTY_TYPE_VEC3,
    .getter.vec3_type = rut_entity_get_position,
    .setter.vec3_type = rut_entity_set_position,
    .nick = "Position",
    .blurb = "The entity's position",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .animatable = TRUE
  },
  {
    .name = "rotation",
    .type = RUT_PROPERTY_TYPE_QUATERNION,
    .getter.quaternion_type = rut_entity_get_rotation,
    .setter.quaternion_type = rut_entity_set_rotation,
    .nick = "Rotation",
    .blurb = "The entity's rotation",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .animatable = TRUE
  },
  {
    .name = "scale",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_entity_get_scale,
    .setter.float_type = rut_entity_set_scale,
    .nick = "Scale",
    .blurb = "The entity's uniform scale factor",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .animatable = TRUE
  },
  {
    .name = "cast_shadow",
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .getter.boolean_type = rut_entity_get_cast_shadow,
    .setter.boolean_type = rut_entity_set_cast_shadow,
    .nick = "Cast Shadow",
    .blurb = "Whether the entity casts shadows or not",
    .flags = RUT_PROPERTY_FLAG_READWRITE
  },
  {
    .name = "receive_shadow",
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .getter.boolean_type = rut_entity_get_receive_shadow,
    .setter.boolean_type = rut_entity_set_receive_shadow,
    .nick = "Receive Shadow",
    .blurb = "Whether the entity receives shadows or not",
    .flags = RUT_PROPERTY_FLAG_READWRITE
  },

  { 0 }
};

static void
_rut_entity_free (void *object)
{
  RutEntity *entity = object;
  CoglPipeline **pipeline_caches = entity->pipeline_caches;
  RutImageSource **image_source_caches = entity->image_source_caches;
  CoglPrimitive **primitive_caches = entity->primitive_caches;
  int i;

  g_free (entity->label);

  while (entity->components->len)
    rut_entity_remove_component (entity,
                                 g_ptr_array_index (entity->components, 0));

  g_ptr_array_free (entity->components, TRUE);

  rut_graphable_destroy (entity);

  for (i = 0; i < N_PIPELINE_CACHE_SLOTS; i++)
    {
      if (pipeline_caches[i])
        cogl_object_unref (pipeline_caches[i]);
    }

  for (i = 0; i < N_IMAGE_SOURCE_CACHE_SLOTS; i++)
    {
      if (image_source_caches[i])
        rut_refable_unref (image_source_caches[i]);
    }

  for (i = 0; i < N_PRIMITIVE_CACHE_SLOTS; i++)
  {
    if (primitive_caches[i])
      rut_refable_unref (primitive_caches[i]);
  }

  g_slice_free (RutEntity, entity);
}

static RutRefCountableVTable _rut_entity_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_entity_free
};

static RutGraphableVTable _rut_entity_graphable_vtable = {
  NULL, /* child_removed */
  NULL, /* child_added */
  NULL, /* parent_changed */
};

static RutTransformableVTable _rut_entity_transformable_vtable = {
  rut_entity_get_transform
};

static RutIntrospectableVTable _rut_entity_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};


RutType rut_entity_type;

void
_rut_entity_init_type (void)
{
  rut_type_init (&rut_entity_type, "RigEntity");
  rut_type_add_interface (&rut_entity_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutEntity, ref_count),
                          &_rut_entity_ref_countable_vtable);
  rut_type_add_interface (&rut_entity_type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (RutEntity, graphable),
                          &_rut_entity_graphable_vtable);
  rut_type_add_interface (&rut_entity_type,
                          RUT_INTERFACE_ID_TRANSFORMABLE,
                          0,
                          &_rut_entity_transformable_vtable);
  rut_type_add_interface (&rut_entity_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &_rut_entity_introspectable_vtable);
  rut_type_add_interface (&rut_entity_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RutEntity, introspectable),
                          NULL); /* no implied vtable */
}

RutEntity *
rut_entity_new (RutContext *ctx)
{
  RutEntity *entity = g_slice_new0 (RutEntity);

  rut_object_init (&entity->_parent, &rut_entity_type);

  entity->ctx = ctx;
  entity->ref_count = 1;

  rut_simple_introspectable_init (entity,
                                  _rut_entity_prop_specs,
                                  entity->properties);

  entity->visible = TRUE;
  entity->receive_shadow = TRUE;

  entity->position[0] = 0.0f;
  entity->position[1] = 0.0f;
  entity->position[2] = 0.0f;

  entity->scale = 1.0f;

  cogl_quaternion_init_identity (&entity->rotation);
  cogl_matrix_init_identity (&entity->transform);
  entity->components = g_ptr_array_new ();

  rut_graphable_init (entity);

  return entity;
}

RutContext *
rut_entity_get_context (RutEntity *entity)
{
  return entity->ctx;
}

void
rut_entity_set_label (RutObject *obj,
                      const char *label)
{
  RutEntity *entity = RUT_ENTITY (obj);

  g_free (entity->label);
  entity->label = g_strdup (label);
  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[PROP_LABEL]);
}

const char *
rut_entity_get_label (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->label ? entity->label : "";
}

float
rut_entity_get_x (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->position[0];
}

void
rut_entity_set_x (RutObject *obj,
                  float x)
{
  RutEntity *entity = RUT_ENTITY (obj);

  entity->position[0] = x;
  entity->dirty = TRUE;
}

float
rut_entity_get_y (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->position[1];
}

void
rut_entity_set_y (RutObject *obj,
                  float y)
{
  RutEntity *entity = RUT_ENTITY (obj);

  entity->position[1] = y;
  entity->dirty = TRUE;
}

float
rut_entity_get_z (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->position[2];
}

void
rut_entity_set_z (RutObject *obj,
                  float z)
{
  RutEntity *entity = RUT_ENTITY (obj);

  entity->position[2] = z;
  entity->dirty = TRUE;
}

const float *
rut_entity_get_position (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->position;
}

void
rut_entity_set_position (RutObject *obj,
                         const float position[3])
{
  RutEntity *entity = RUT_ENTITY (obj);

  entity->position[0] = position[0];
  entity->position[1] = position[1];
  entity->position[2] = position[2];
  entity->dirty = TRUE;
}

void
rut_entity_get_transformed_position (RutEntity *entity,
                                     float position[3])
{
  CoglMatrix transform;
  float w = 1;

  rut_graphable_get_transform (entity, &transform);

  cogl_matrix_transform_point (&transform,
                               &position[0],
                               &position[1],
                               &position[2],
                               &w);
}

const CoglQuaternion *
rut_entity_get_rotation (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return &entity->rotation;
}

void
rut_entity_set_rotation (RutObject *obj,
                         const CoglQuaternion *rotation)
{
  RutEntity *entity = RUT_ENTITY (obj);

  entity->rotation = *rotation;
  entity->dirty = TRUE;
}

void
rut_entity_apply_rotations (RutObject *entity,
                            CoglQuaternion *rotations)
{
  int depth = 0;
  RutObject **entity_nodes;
  RutObject *node = entity;
  int i;

  do {
    RutGraphableProps *graphable_priv =
      rut_object_get_properties (node, RUT_INTERFACE_ID_GRAPHABLE);

    depth++;

    node = graphable_priv->parent;
  } while (node);

  entity_nodes = g_alloca (sizeof (RutObject *) * depth);

  node = entity;
  i = 0;
  do {
    RutGraphableProps *graphable_priv;
    RutObjectProps *obj = node;

    if (obj->type == &rut_entity_type)
      entity_nodes[i++] = node;

    graphable_priv =
      rut_object_get_properties (node, RUT_INTERFACE_ID_GRAPHABLE);
    node = graphable_priv->parent;
  } while (node);

  for (i--; i >= 0; i--)
    {
      const CoglQuaternion *rotation = rut_entity_get_rotation (entity_nodes[i]);
      cogl_quaternion_multiply (rotations, rotations, rotation);
    }
}

void
rut_entity_get_rotations (RutObject *entity,
                          CoglQuaternion *rotation)
{
  cogl_quaternion_init_identity (rotation);
  rut_entity_apply_rotations (entity, rotation);
}

void
rut_entity_get_view_rotations (RutObject *entity,
                               RutObject *camera_entity,
                               CoglQuaternion *rotation)
{
  rut_entity_get_rotations (camera_entity, rotation);
  cogl_quaternion_invert (rotation);

  rut_entity_apply_rotations (entity, rotation);
}

float
rut_entity_get_scale (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->scale;
}

void
rut_entity_set_scale (RutObject *obj,
                      float scale)
{
  RutEntity *entity = RUT_ENTITY (obj);

  entity->scale = scale;
  entity->dirty = TRUE;
}

float
rut_entity_get_scales (RutObject *entity)
{
  RutObject *node = entity;
  float scales = 1;

  do {
    RutGraphableProps *graphable_priv =
      rut_object_get_properties (node, RUT_INTERFACE_ID_GRAPHABLE);
    RutObjectProps *obj = node;

    if (obj->type == &rut_entity_type)
      scales *= rut_entity_get_scale (node);

    node = graphable_priv->parent;
  } while (node);

  return scales;
}

const CoglMatrix *
rut_entity_get_transform (RutObject *self)
{
  RutEntity *entity = self;
  CoglMatrix rotation;

  if (!entity->dirty)
    return &entity->transform;

  cogl_matrix_init_translation (&entity->transform,
                                entity->position[0],
                                entity->position[1],
                                entity->position[2]);
  cogl_matrix_init_from_quaternion (&rotation, &entity->rotation);
  cogl_matrix_multiply (&entity->transform, &entity->transform, &rotation);
  cogl_matrix_scale (&entity->transform,
                     entity->scale, entity->scale, entity->scale);

  entity->dirty = FALSE;

  return &entity->transform;
}

void
rut_entity_add_component (RutEntity *entity,
                          RutObject *object)
{
  RutComponentableProps *component =
    rut_object_get_properties (object, RUT_INTERFACE_ID_COMPONENTABLE);
  component->entity = entity;
  rut_refable_ref (object);
  g_ptr_array_add (entity->components, object);
}

void
rut_entity_remove_component (RutEntity *entity,
                             RutObject *object)
{
  RutComponentableProps *component =
    rut_object_get_properties (object, RUT_INTERFACE_ID_COMPONENTABLE);
  component->entity = NULL;
  rut_refable_unref (object);
  g_warn_if_fail (g_ptr_array_remove_fast (entity->components, object));
}

void
rut_entity_update (RutEntity *entity,
                   int64_t    time)
{
  int i;

  for (i = 0; i < entity->components->len; i++)
    {
      RutObject *component = g_ptr_array_index (entity->components, i);
      RutComponentableVTable *componentable =
        rut_object_get_vtable (component, RUT_INTERFACE_ID_COMPONENTABLE);

      if (componentable->update)
        componentable->update(component, time);
    }
}

void
rut_entity_draw (RutEntity       *entity,
                 CoglFramebuffer *fb)
{
  int i;

  if (!entity->visible)
    return;

  for (i = 0; i < entity->components->len; i++)
    {
      RutObject *component = g_ptr_array_index (entity->components, i);
      RutComponentableVTable *componentable =
        rut_object_get_vtable (component, RUT_INTERFACE_ID_COMPONENTABLE);

      if (componentable->draw)
        componentable->draw(component, fb);
    }
}

void
rut_entity_translate (RutEntity *entity,
                      float      tx,
                      float      ty,
                      float      tz)
{
  entity->position[0] += tx;
  entity->position[1] += ty;
  entity->position[2] += tz;

  entity->dirty = TRUE;
}

void
rut_entity_set_translate (RutEntity *entity,
                          float tx,
                          float ty,
                          float tz)
{
  entity->position[0] = tx;
  entity->position[1] = ty;
  entity->position[2] = tz;

  entity->dirty = TRUE;
}

void
rut_entity_rotate_x_axis (RutEntity *entity,
                          float      x_angle)
{
  CoglQuaternion x_rotation, *current;

  /* XXX: avoid the allocation here, and/or make the muliplication in place */
  current = cogl_quaternion_copy (&entity->rotation);
  cogl_quaternion_init_from_x_rotation (&x_rotation, x_angle);
  cogl_quaternion_multiply (&entity->rotation, current, &x_rotation);
  cogl_quaternion_free (current);

  entity->dirty = TRUE;
}

void
rut_entity_rotate_y_axis (RutEntity *entity,
                          float      y_angle)
{
  CoglQuaternion y_rotation, *current;

  /* XXX: avoid the allocation here, and/or make the muliplication in place */
  current = cogl_quaternion_copy (&entity->rotation);
  cogl_quaternion_init_from_y_rotation (&y_rotation, y_angle);
  cogl_quaternion_multiply (&entity->rotation, current, &y_rotation);
  cogl_quaternion_free (current);

  entity->dirty = TRUE;
}

void
rut_entity_rotate_z_axis (RutEntity *entity,
                          float      z_angle)
{
  CoglQuaternion z_rotation, *current;

  /* XXX: avoid the allocation here, and/or make the muliplication in place */
  current = cogl_quaternion_copy (&entity->rotation);
  cogl_quaternion_init_from_z_rotation (&z_rotation, z_angle);
  cogl_quaternion_multiply (&entity->rotation, current, &z_rotation);
  cogl_quaternion_free (current);

  entity->dirty = TRUE;
}

CoglBool
rut_entity_get_cast_shadow (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->cast_shadow;
}

void
rut_entity_set_cast_shadow (RutObject *obj,
                            CoglBool cast_shadow)
{
  RutEntity *entity = RUT_ENTITY (obj);

  if (entity->cast_shadow == cast_shadow)
    return;

  entity->cast_shadow = cast_shadow;

  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[PROP_CAST_SHADOW]);
}

CoglBool
rut_entity_get_receive_shadow (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->receive_shadow;
}

void
rut_entity_set_receive_shadow (RutObject *obj,
                               gboolean receive_shadow)
{
  RutEntity *entity = RUT_ENTITY (obj);

  if (entity->receive_shadow == receive_shadow)
    return;

  entity->receive_shadow = receive_shadow;

  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[PROP_RECEIVE_SHADOW]);
}

RutObject *
rut_entity_get_component (RutEntity *entity,
                          RutComponentType type)
{
  int i;

  for (i = 0; i < entity->components->len; i++)
    {
      RutObject *component = g_ptr_array_index (entity->components, i);
      RutComponentableProps *component_props =
        rut_object_get_properties (component, RUT_INTERFACE_ID_COMPONENTABLE);

      if (component_props->type == type)
        return component;
    }

  return NULL;
}

void
rut_entity_foreach_component (RutEntity *entity,
                              RutComponentCallback callback,
                              void *user_data)
{
  int i;
  for (i = 0; i < entity->components->len; i++)
    callback (g_ptr_array_index (entity->components, i), user_data);
}

void
rut_entity_set_pipeline_cache (RutEntity *entity,
                               int slot,
                               CoglPipeline *pipeline)
{
  if (entity->pipeline_caches[slot])
    cogl_object_unref (entity->pipeline_caches[slot]);

  entity->pipeline_caches[slot] = pipeline;
  if (pipeline)
    cogl_object_ref (pipeline);
}

CoglPipeline *
rut_entity_get_pipeline_cache (RutEntity *entity,
                               int slot)
{
  return entity->pipeline_caches[slot];
}

void
rut_entity_set_image_source_cache (RutEntity *entity,
                                   int slot,
                                   RutImageSource *source)
{
  if (entity->image_source_caches[slot])
    rut_refable_unref (entity->image_source_caches[slot]);

  entity->image_source_caches[slot] = source;
  if (source)
    rut_refable_ref (source);
}

RutImageSource*
rut_entity_get_image_source_cache (RutEntity *entity,
                                   int slot)
{
  return entity->image_source_caches[slot];
}

void
rut_entity_set_primitive_cache (RutEntity *entity,
                                int slot,
                                CoglPrimitive *primitive)
{
  if (entity->primitive_caches[slot])
    cogl_object_unref (entity->primitive_caches[slot]);

  entity->primitive_caches[slot] = primitive;
  if (primitive)
    cogl_object_ref (primitive);
}

CoglPrimitive *
rut_entity_get_primitive_cache (RutEntity *entity,
                                int slot)
{
  return entity->primitive_caches[slot];
}

CoglBool
rut_entity_get_visible (RutObject *obj)
{
  RutEntity *entity = RUT_ENTITY (obj);

  return entity->visible;
}

void
rut_entity_set_visible (RutObject *obj, CoglBool visible)
{
  RutEntity *entity = RUT_ENTITY (obj);

  entity->visible = visible;
}
