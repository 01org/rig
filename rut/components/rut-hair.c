/*
* Rut
*
* Copyright (C) 2012 Intel Corporation
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library. If not, see
* <http://www.gnu.org/licenses/>.
*/

#include "rut-hair.h"
#include "rut-global.h"
#include "math.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

static RutPropertySpec _rut_hair_prop_specs[] = {
  {
    .name = "hair-length",
    .nick = "Length",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_hair_get_length,
    .setter.float_type = rut_hair_set_length,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
    .animatable = TRUE
  },
  {
    .name = "hair-detail",
    .nick = "Detail",
    .type = RUT_PROPERTY_TYPE_INTEGER,
    .getter.integer_type = rut_hair_get_n_shells,
    .setter.integer_type = rut_hair_set_n_shells,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .int_range = { -1, INT_MAX }},
  },
  {
    .name = "hair-gravity",
    .nick = "Gravity",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_hair_get_gravity,
    .setter.float_type = rut_hair_set_gravity,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
    .animatable = TRUE
  },
  {
    .name = "hair-density",
    .nick = "Density",
    .type = RUT_PROPERTY_TYPE_INTEGER,
    .getter.integer_type = rut_hair_get_density,
    .setter.integer_type = rut_hair_set_density,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .int_range = { -1, INT_MAX }},
  },
  {
    .name = "hair-thickness",
    .nick = "Thickness",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_hair_get_thickness,
    .setter.float_type = rut_hair_set_thickness,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }}
  },
  {
    .name = "hair-resolution",
    .nick = "Resolution",
    .type = RUT_PROPERTY_TYPE_INTEGER,
    .getter.integer_type = rut_hair_get_resolution,
    .setter.integer_type = rut_hair_set_resolution,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .int_range = { -1, INT_MAX }},
  },
  { NULL }
};

static void
_rut_hair_generate_textures (RutHair *hair)
{
  CoglPipeline *pipeline;
  float diameter, diameter_iter;
  int i;
  int tex_width;
  int tex_height;
  uint8_t* pixels;
  int pixel_size;
  int rowstride;

  if (hair->textures)
    {
      for (i = 0; i < sizeof (hair->textures) / sizeof (CoglTexture*); i++)
        cogl_object_unref (hair->textures[i]);

      g_free (hair->textures);
    }

  hair->textures = g_new (CoglTexture*, hair->resolution);

  diameter = hair->thickness;
  diameter_iter = hair->thickness / hair->resolution;
  pipeline = cogl_pipeline_new (hair->ctx->cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0, hair->circle);

  for (i = 0; i < hair->resolution; i++)
    {
      CoglOffscreen *offscreen;
      int j;
      hair->textures[i] = cogl_texture_new_with_size (hair->ctx->cogl_context,
                            256, 256, COGL_TEXTURE_NONE,
                            COGL_PIXEL_FORMAT_RGBA_8888);
      tex_width = 256;
      tex_height = 256;

      offscreen = cogl_offscreen_new_to_texture (hair->textures[i]);

      cogl_framebuffer_clear4f (COGL_FRAMEBUFFER (offscreen),
                                COGL_BUFFER_BIT_COLOR, 0, 0, 0, 0);

      cogl_pipeline_set_color4f (pipeline, 0.3, 0.3, 0.3, 1.0);

      srand (7025);
      for (j = 0; j < hair->density; j++)
        {
          float x = ((rand() / (float)RAND_MAX) * 2) - 1;
          float y = ((rand() / (float)RAND_MAX) * 2) - 1;

          cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen),
                                           pipeline, 
                                           x - (diameter / 2.0), 
                                           y - (diameter / 2.0),
                                           x + (diameter / 2.0),
                                           y + (diameter / 2.0));
        }

      cogl_pipeline_set_color4f (pipeline, 1, 1, 1, 1);

      srand (7025);
      for (j = 0; j < hair->density; j++)
        {
          float x = ((rand() / (float)RAND_MAX) * 2) - 1;
          float y = ((rand() / (float)RAND_MAX) * 2) - 1;
          float shadow_offset = diameter / 4.0f;
          x += shadow_offset;
          y += shadow_offset;

          cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen),
                                           pipeline,
                                           x - (diameter / 2.0), 
                                           y - (diameter / 2.0), 
                                           x + (diameter / 2.0), 
                                           y + (diameter / 2.0));
        }

      cogl_object_unref (offscreen);

      diameter -= diameter_iter;
    }
}

RutType rut_hair_type;

static void
_rut_hair_free (void *object)
{
  RutHair *hair = object;

  rut_simple_introspectable_destroy (hair);

  g_slice_free (RutHair, hair);
}

static RutRefCountableVTable _rut_hair_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_hair_free
};

static RutComponentableVTable _rut_hair_componentable_vtable = {
    0
};

static RutIntrospectableVTable _rut_hair_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};

void
_rut_hair_init_type (void)
{
  rut_type_init (&rut_hair_type, "RigHair");

  rut_type_add_interface (&rut_hair_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutHair, ref_count),
                          &_rut_hair_ref_countable_vtable);
  rut_type_add_interface (&rut_hair_type,
                          RUT_INTERFACE_ID_COMPONENTABLE,
                          offsetof (RutHair, component),
                          &_rut_hair_componentable_vtable);

  rut_type_add_interface (&rut_hair_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0,
                          &_rut_hair_introspectable_vtable);

  rut_type_add_interface (&rut_hair_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RutHair, introspectable),
                          NULL);
}

RutHair *
rut_hair_new (RutContext *ctx)
{
  RutHair *hair = g_slice_new0 (RutHair);

  rut_object_init (&hair->_parent, &rut_hair_type);

  hair->ref_count = 1;

  hair->component.type = RUT_COMPONENT_TYPE_HAIR;

  hair->ctx = rut_refable_ref (ctx);

  hair->length = 100;
  hair->n_shells = 100;
  hair->gravity = 0.5;
  hair->density = 3000;
  hair->thickness = 0.02;
  hair->resolution = 100;
  hair->textures = NULL;
  hair->slant = 45;
  hair->circle = cogl_texture_new_from_file (hair->ctx->cogl_context,
                                             rut_find_data_file ("circle1.png"),
                                               COGL_TEXTURE_NONE,
                                               COGL_PIXEL_FORMAT_RGBA_8888,
                                               NULL);

  rut_simple_introspectable_init (hair, _rut_hair_prop_specs,
                                  hair->properties);

  _rut_hair_generate_textures (hair);

  return hair;
}


CoglTexture *
rut_hair_get_texture (RutObject *obj, int layer)
{
  RutHair *hair = obj;
  return hair->textures[layer];
}

float
rut_hair_get_length (RutObject *obj)
{
  RutHair *hair = RUT_HAIR (obj);

  return hair->length;
}

void
rut_hair_set_length (RutObject *obj,
                     float length)
{
  RutHair *hair = RUT_HAIR (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (length == hair->length)
    return;

  hair->length = length;

  entity = hair->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                     &hair->properties[RUT_HAIR_PROP_LENGTH]);
}

int
rut_hair_get_n_shells (RutObject *obj)
{
  RutHair *hair = RUT_HAIR (obj);

  return hair->n_shells;
}

void
rut_hair_set_n_shells (RutObject *obj,
                       int n_shells)
{
  RutHair *hair = RUT_HAIR (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (n_shells == hair->n_shells)
    return;

  hair->n_shells = n_shells;

  entity = hair->component.entity;
  ctx = rut_entity_get_context (entity);

  rut_property_dirty (&ctx->property_ctx,
                      &hair->properties[RUT_HAIR_PROP_DETAIL]);
}

float
rut_hair_get_gravity (RutObject *obj)
{
  RutHair *hair = RUT_HAIR (obj);
  return hair->gravity;
}

void
rut_hair_set_gravity (RutObject *obj,
                      float gravity)
{
  RutHair *hair = RUT_HAIR (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (gravity == hair->gravity)
    return;

  hair->gravity = gravity;

  entity = hair->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                     &hair->properties[RUT_HAIR_PROP_GRAVITY]);
}

int
rut_hair_get_density (RutObject *obj)
{
  RutHair *hair = obj;
  return hair->density;
}

void
rut_hair_set_density (RutObject *obj,
                      int density)
{
  RutHair *hair = obj;
  RutEntity *entity;
  RutContext *ctx;

  if (density == hair->density)
    return;

  hair->density = density;

  _rut_hair_generate_textures (hair);
  entity = hair->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                      &hair->properties[RUT_HAIR_PROP_DENSITY]);
}

float
rut_hair_get_thickness (RutObject *obj)

{
  RutHair *hair = obj;
  return hair->thickness;
}

void
rut_hair_set_thickness (RutObject *obj,
                        float thickness)
{
  RutHair *hair = obj;
  RutEntity *entity;
  RutContext *ctx;

  if (thickness == hair->thickness)
    return;

  hair->thickness = thickness;

  _rut_hair_generate_textures (hair);
  entity = hair->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                      &hair->properties[RUT_HAIR_PROP_THICKNESS]);
}

int
rut_hair_get_resolution (RutObject *obj)
{
  RutHair *hair = obj;
  return hair->resolution;
}

void
rut_hair_set_resolution (RutObject *obj,
                         int resolution)
{
  RutHair *hair = obj;
  RutEntity *entity;
  RutContext *ctx;

  if (resolution == hair->resolution)
    return;

  hair->resolution = resolution;

  _rut_hair_generate_textures (hair);
  entity = hair->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                      &hair->properties[RUT_HAIR_PROP_RESOLUTION]);
}

float
rut_hair_get_slant (RutObject *obj)
{
  RutHair *hair = obj;
  return hair->slant;
}
