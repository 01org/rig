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

#include "rut-material.h"
#include "rut-global.h"
#include "rut-asset.h"
#include "rut-color.h"
#include "rut-pointalism-grid.h"

static RutPropertySpec _rut_material_prop_specs[] = {
  {
    .name = "ambient",
    .nick = "Ambient",
    .type = RUT_PROPERTY_TYPE_COLOR,
    .getter.color_type = rut_material_get_ambient,
    .setter.color_type = rut_material_set_ambient,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .animatable = TRUE
  },
  {
    .name = "diffuse",
    .nick = "Diffuse",
    .type = RUT_PROPERTY_TYPE_COLOR,
    .getter.color_type = rut_material_get_diffuse,
    .setter.color_type = rut_material_set_diffuse,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .animatable = TRUE
  },
  {
    .name = "specular",
    .nick = "Specular",
    .type = RUT_PROPERTY_TYPE_COLOR,
    .getter.color_type = rut_material_get_specular,
    .setter.color_type = rut_material_set_specular,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .animatable = TRUE
  },
  {
    .name = "shininess",
    .nick = "Shininess",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_material_get_shininess,
    .setter.float_type = rut_material_set_shininess,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 1000 }},
    .animatable = TRUE
  },
  {
    .name = "alpha-mask-threshold",
    .nick = "Alpha Threshold",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_material_get_alpha_mask_threshold,
    .setter.float_type = rut_material_set_alpha_mask_threshold,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 1 }},
    .animatable = TRUE
  },
  { 0 }
};

RutType rut_material_type;

static void
_rut_material_free (void *object)
{
  RutMaterial *material = object;

  if (material->texture_asset)
    rut_refable_unref (material->texture_asset);

  if (material->normal_map_asset)
    rut_refable_unref (material->normal_map_asset);

  if (material->alpha_mask_asset)
    rut_refable_unref (material->alpha_mask_asset);

  rut_simple_introspectable_destroy (material);

  g_slice_free (RutMaterial, material);
}

void
_rut_material_init_type (void)
{
  static RutRefCountableVTable ref_countable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _rut_material_free
  };

  static RutComponentableVTable componentable_vtable = {
    0
  };

  static RutIntrospectableVTable introspectable_vtable = {
    rut_simple_introspectable_lookup_property,
    rut_simple_introspectable_foreach_property
  };

  RutType *type = &rut_material_type;

#define TYPE RutMaterial

  rut_type_init (type, "RigMaterial");
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &ref_countable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_COMPONENTABLE,
                          offsetof (TYPE, component),
                          &componentable_vtable);
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

RutMaterial *
rut_material_new (RutContext *ctx,
                  RutAsset *asset)
{
  RutMaterial *material = g_slice_new0 (RutMaterial);

  rut_object_init (&material->_parent, &rut_material_type);

  material->ref_count = 1;

  material->component.type = RUT_COMPONENT_TYPE_MATERIAL;

  cogl_color_init_from_4f (&material->ambient, 0.23, 0.23, 0.23, 1);
  cogl_color_init_from_4f (&material->diffuse, 0.75, 0.75, 0.75, 1);
  cogl_color_init_from_4f (&material->specular, 0.64, 0.64, 0.64, 1);

  material->shininess = 100;

  rut_simple_introspectable_init (material,
                                  _rut_material_prop_specs,
                                  material->properties);

  material->uniforms_flush_age = -1;

  material->texture_asset = NULL;
  material->normal_map_asset = NULL;
  material->alpha_mask_asset = NULL;

  if (asset)
    {
      switch (rut_asset_get_type (asset))
        {
        case RUT_ASSET_TYPE_TEXTURE:
          material->texture_asset = rut_refable_ref (asset);
          break;
        case RUT_ASSET_TYPE_NORMAL_MAP:
          material->normal_map_asset = rut_refable_ref (asset);
          break;
        case RUT_ASSET_TYPE_ALPHA_MASK:
          material->alpha_mask_asset = rut_refable_ref (asset);
          break;
        default:
          g_warn_if_reached ();
        }
    }

  return material;
}

void
rut_material_set_texture_asset (RutMaterial *material,
                                RutAsset *texture_asset)
{
  if (material->texture_asset)
    {
      rut_refable_unref (material->texture_asset);
      material->texture_asset = NULL;
    }

  if (texture_asset)
    material->texture_asset = rut_refable_ref (texture_asset);
}

RutAsset *
rut_material_get_texture_asset (RutMaterial *material)
{
  return material->texture_asset;
}

void
rut_material_set_normal_map_asset (RutMaterial *material,
                                   RutAsset *normal_map_asset)
{
  if (material->normal_map_asset)
    {
      rut_refable_unref (material->normal_map_asset);
      material->normal_map_asset = NULL;
    }

  if (normal_map_asset)
    material->normal_map_asset = rut_refable_ref (normal_map_asset);
}

RutAsset *
rut_material_get_normal_map_asset (RutMaterial *material)
{
  return material->normal_map_asset;
}

void
rut_material_set_alpha_mask_asset (RutMaterial *material,
                                   RutAsset *alpha_mask_asset)
{
  if (material->alpha_mask_asset)
    {
      rut_refable_unref (material->alpha_mask_asset);
      material->alpha_mask_asset = NULL;
    }

  if (alpha_mask_asset)
    material->alpha_mask_asset = rut_refable_ref (alpha_mask_asset);
}

RutAsset *
rut_material_get_alpha_mask_asset (RutMaterial *material)
{
  return material->alpha_mask_asset;
}

void
rut_material_set_ambient (RutObject *obj,
                          const CoglColor *color)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  material->ambient = *color;
  material->uniforms_age++;
}

const CoglColor *
rut_material_get_ambient (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  return &material->ambient;
}

void
rut_material_set_diffuse (RutObject *obj,
                          const CoglColor *color)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  material->diffuse = *color;
  material->uniforms_age++;

}

const CoglColor *
rut_material_get_diffuse (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  return &material->diffuse;
}

void
rut_material_set_specular (RutObject *obj,
                           const CoglColor *color)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  material->specular = *color;
  material->uniforms_age++;
}

const CoglColor *
rut_material_get_specular (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  return &material->specular;
}

void
rut_material_set_shininess (RutObject *obj,
                            float shininess)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  material->shininess = shininess;
  material->uniforms_age++;
}

float
rut_material_get_shininess (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  return material->shininess;
}

float
rut_material_get_alpha_mask_threshold (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);

  return material->alpha_mask_threshold;
}

void
rut_material_set_alpha_mask_threshold (RutObject *obj,
                                       float threshold)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  RutEntity *entity;
  RutContext *ctx;
  if (material->alpha_mask_threshold == threshold)
    return;

  material->alpha_mask_threshold = threshold;

  entity = material->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                      &material->properties[RUT_MATERIAL_PROP_ALPHA_MASK_THRESHOLD]);
}

void
rut_material_flush_uniforms (RutMaterial *material,
                             CoglPipeline *pipeline)
{
  int location;
  RutObject *geo;
  RutEntity *entity = material->component.entity;

  //if (material->uniforms_age == material->uniforms_flush_age)
  //  return;

  location = cogl_pipeline_get_uniform_location (pipeline, "material_ambient");
  cogl_pipeline_set_uniform_float (pipeline,
                                   location,
                                   4, 1,
                                   (float *)&material->ambient);

  location = cogl_pipeline_get_uniform_location (pipeline, "material_diffuse");
  cogl_pipeline_set_uniform_float (pipeline,
                                   location,
                                   4, 1,
                                   (float *)&material->diffuse);

  location = cogl_pipeline_get_uniform_location (pipeline, "material_specular");
  cogl_pipeline_set_uniform_float (pipeline,
                                   location,
                                   4, 1,
                                   (float *)&material->specular);

  location = cogl_pipeline_get_uniform_location (pipeline,
                                                 "material_shininess");
  cogl_pipeline_set_uniform_1f (pipeline, location, material->shininess);

  location = cogl_pipeline_get_uniform_location (pipeline,
                                                 "material_alpha_threshold");
  cogl_pipeline_set_uniform_1f (pipeline, location, material->alpha_mask_threshold);

  geo = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_GEOMETRY);

  if (rut_object_get_type (geo) == &rut_pointalism_grid_type &&
      material->texture_asset)
    {
      int scale, z;
      scale = rut_pointalism_grid_get_scale (geo);
      z = rut_pointalism_grid_get_z (geo);

      location = cogl_pipeline_get_uniform_location (pipeline,
                                                     "scale_factor");
      cogl_pipeline_set_uniform_1f (pipeline, location, scale);

      location = cogl_pipeline_get_uniform_location (pipeline, "z_trans");
      cogl_pipeline_set_uniform_1f (pipeline, location, z);

      location = cogl_pipeline_get_uniform_location (pipeline, "anti_scale");
      if (rut_pointalism_grid_get_lighter (geo))
        cogl_pipeline_set_uniform_1i (pipeline, location, 1);
      else
        cogl_pipeline_set_uniform_1i (pipeline, location, 0);
    }

  material->uniforms_flush_age = material->uniforms_age;
}

void
rut_material_dirty_uniforms (RutMaterial *material)
{
  material->uniforms_flush_age = material->uniforms_age -1;
}
