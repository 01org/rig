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
#include "rut-renderer.h"

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

  { 0 }
};

static void
_rut_entity_free (void *object)
{
  RutEntity *entity = object;

  g_free (entity->label);

  while (entity->components->len)
    rut_entity_remove_component (entity,
                                 g_ptr_array_index (entity->components, 0));

  g_ptr_array_free (entity->components, TRUE);

  rut_graphable_destroy (entity);

  if (entity->renderer_priv)
    {
      RutObject *renderer = *(RutObject **)entity->renderer_priv;
      rut_renderer_free_priv (renderer, entity);
    }

  g_slice_free (RutEntity, entity);
}

RutType rut_entity_type;

void
_rut_entity_init_type (void)
{
  static RutGraphableVTable graphable_vtable = {
      NULL, /* child_removed */
      NULL, /* child_added */
      NULL, /* parent_changed */
  };
  static RutTransformableVTable transformable_vtable = {
      rut_entity_get_transform
  };
  static RutIntrospectableVTable introspectable_vtable = {
      rut_simple_introspectable_lookup_property,
      rut_simple_introspectable_foreach_property
  };

  RutType *type = &rut_entity_type;
#define TYPE RutEntity

  rut_type_init (type, G_STRINGIFY (TYPE));
  rut_type_add_refable (type, ref_count, _rut_entity_free);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (TYPE, graphable),
                          &graphable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_TRANSFORMABLE,
                          0,
                          &transformable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &introspectable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (TYPE, introspectable),
                          NULL); /* no implied vtable */

#undef TYPE
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
  RutEntity *entity = obj;

  g_free (entity->label);
  entity->label = g_strdup (label);
  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[RUT_ENTITY_PROP_LABEL]);
}

const char *
rut_entity_get_label (RutObject *obj)
{
  RutEntity *entity = obj;

  return entity->label ? entity->label : "";
}

const float *
rut_entity_get_position (RutObject *obj)
{
  RutEntity *entity = obj;

  return entity->position;
}

void
rut_entity_set_position (RutObject *obj,
                         const float position[3])
{
  RutEntity *entity = obj;

  if (memcpy (entity->position, position, sizeof (float) * 3) == 0)
    return;

  entity->position[0] = position[0];
  entity->position[1] = position[1];
  entity->position[2] = position[2];
  entity->dirty = TRUE;

  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[RUT_ENTITY_PROP_POSITION]);
}

float
rut_entity_get_x (RutObject *obj)
{
  RutEntity *entity = obj;

  return entity->position[0];
}

void
rut_entity_set_x (RutObject *obj,
                  float x)
{
  RutEntity *entity = obj;
  float pos[3] = {
      x,
      entity->position[1],
      entity->position[2],
  };

  rut_entity_set_position (entity, pos);
}

float
rut_entity_get_y (RutObject *obj)
{
  RutEntity *entity = obj;

  return entity->position[1];
}

void
rut_entity_set_y (RutObject *obj,
                  float y)
{
  RutEntity *entity = obj;
  float pos[3] = {
      entity->position[0],
      y,
      entity->position[2],
  };

  rut_entity_set_position (entity, pos);
}

float
rut_entity_get_z (RutObject *obj)
{
  RutEntity *entity = obj;

  return entity->position[2];
}

void
rut_entity_set_z (RutObject *obj,
                  float z)
{
  RutEntity *entity = obj;
  float pos[3] = {
      entity->position[0],
      entity->position[1],
      z,
  };

  rut_entity_set_position (entity, pos);
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
  RutEntity *entity = obj;

  return &entity->rotation;
}

void
rut_entity_set_rotation (RutObject *obj,
                         const CoglQuaternion *rotation)
{
  RutEntity *entity = obj;

  if (memcmp (&entity->rotation, rotation, sizeof (*rotation)) == 0)
      return;

  entity->rotation = *rotation;
  entity->dirty = TRUE;

  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[RUT_ENTITY_PROP_ROTATION]);
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
  RutEntity *entity = obj;

  return entity->scale;
}

void
rut_entity_set_scale (RutObject *obj,
                      float scale)
{
  RutEntity *entity = obj;

  if (entity->scale == scale)
    return;

  entity->scale = scale;
  entity->dirty = TRUE;

  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[RUT_ENTITY_PROP_SCALE]);
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
rut_entity_translate (RutEntity *entity,
                      float tx,
                      float ty,
                      float tz)
{
  float pos[3] = {
      entity->position[1] + tx,
      entity->position[1] + ty,
      entity->position[2] + tz,
  };

  rut_entity_set_position (entity, pos);
}

void
rut_entity_set_translate (RutEntity *entity,
                          float tx,
                          float ty,
                          float tz)
{
  float pos[3] = { tx, ty, tz, };

  rut_entity_set_position (entity, pos);
}

void
rut_entity_rotate_x_axis (RutEntity *entity,
                          float x_angle)
{
  CoglQuaternion x_rotation;

  cogl_quaternion_init_from_x_rotation (&x_rotation, x_angle);
  cogl_quaternion_multiply (&entity->rotation, &entity->rotation,
                            &x_rotation);

  entity->dirty = TRUE;

  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[RUT_ENTITY_PROP_ROTATION]);
}

void
rut_entity_rotate_y_axis (RutEntity *entity,
                          float y_angle)
{
  CoglQuaternion y_rotation;

  cogl_quaternion_init_from_y_rotation (&y_rotation, y_angle);
  cogl_quaternion_multiply (&entity->rotation, &entity->rotation,
                            &y_rotation);

  entity->dirty = TRUE;

  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[RUT_ENTITY_PROP_ROTATION]);
}

void
rut_entity_rotate_z_axis (RutEntity *entity,
                          float z_angle)
{
  CoglQuaternion z_rotation;

  cogl_quaternion_init_from_z_rotation (&z_rotation, z_angle);
  cogl_quaternion_multiply (&entity->rotation, &entity->rotation,
                            &z_rotation);

  entity->dirty = TRUE;

  rut_property_dirty (&entity->ctx->property_ctx,
                      &entity->properties[RUT_ENTITY_PROP_ROTATION]);
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
rut_entity_foreach_component_safe (RutEntity *entity,
                                   RutComponentCallback callback,
                                   void *user_data)
{
  int i;
  int n_components = entity->components->len;
  size_t size = sizeof (void *) * n_components;
  void **components = g_alloca (size);

  memcpy (components, entity->components->pdata, size);

  for (i = 0; i < n_components; i++)
    callback (components[i], user_data);
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

RutEntity *
rut_entity_copy (RutEntity *entity)
{
  RutEntity *copy = rut_entity_new (entity->ctx);
  GPtrArray *entity_components = entity->components;
  GPtrArray *copy_components;
  RutGraphableProps *graph_props =
    rut_object_get_properties (entity, RUT_INTERFACE_ID_GRAPHABLE);
  int i;
  GList *l;

  copy->label = NULL;

  memcpy (copy->position, entity->position, sizeof (entity->position));
  copy->rotation = entity->rotation;
  copy->scale = entity->scale;
  copy->transform = entity->transform;
  copy->dirty = FALSE;

  copy_components = g_ptr_array_sized_new (entity_components->len);
  copy->components = copy_components;

  for (i = 0; i < entity_components->len; i++)
    {
      RutObject *component = g_ptr_array_index (entity_components, i);
      RutComponentableVTable *componentable =
        rut_object_get_vtable (component, RUT_INTERFACE_ID_COMPONENTABLE);
      RutObject *component_copy = componentable->copy (component);

      rut_entity_add_component (copy, component_copy);
      rut_refable_unref (component_copy);
    }

  for (l = graph_props->children.head; l; l = l->next)
    {
      RutObject *child = l->data;
      RutObject *child_copy;

      if (rut_object_get_type (child) != &rut_entity_type)
        continue;

      child_copy = rut_entity_copy (child);
      rut_graphable_add_child (copy, child_copy);
    }

  return copy;
}

void
rut_entity_notify_changed (RutEntity *entity)
{
  if (entity->renderer_priv)
    {
      RutObject *renderer = *(RutObject **)entity->renderer_priv;
      rut_renderer_notify_entity_changed (renderer, entity);
    }
}
