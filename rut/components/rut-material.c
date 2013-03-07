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
  {
    .name = "pointalism-on",
    .nick = "Pointalism",
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .getter.boolean_type = rut_material_get_pointalism_on,
    .setter.boolean_type = rut_material_set_pointalism_on,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .animatable = TRUE
  },
  {
    .name = "pointalism-scale",
    .nick = "Pointalism Scale Factor",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_material_get_pointalism_scale,
    .setter.float_type = rut_material_set_pointalism_scale,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
    .animatable = TRUE
  },
  {
    .name = "pointalism-z",
    .nick = "Pointalism Z Factor",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_material_get_pointalism_z,
    .setter.float_type = rut_material_set_pointalism_z,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
    .animatable = TRUE
  },
  { 0 }
};

RutType rut_material_type;

static RutComponentableVTable _rut_material_componentable_vtable = {
    0
};

static RutIntrospectableVTable _rut_material_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};

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

static RutRefCountableVTable _rut_material_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_material_free
};

void
_rut_material_init_type (void)
{
  rut_type_init (&rut_material_type, "RigMaterial");
  rut_type_add_interface (&rut_material_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutMaterial, ref_count),
                          &_rut_material_ref_countable_vtable);
  rut_type_add_interface (&rut_material_type,
                           RUT_INTERFACE_ID_COMPONENTABLE,
                           offsetof (RutMaterial, component),
                           &_rut_material_componentable_vtable);
  rut_type_add_interface (&rut_material_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &_rut_material_introspectable_vtable);
  rut_type_add_interface (&rut_material_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RutMaterial, introspectable),
                          NULL); /* no implied vtable */

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
  material->pointalism_on = FALSE;
  material->pointalism_cell_size = 10;
  material->pointalism_columns = 50;
  material->pointalism_rows = 37;
  material->pointalism_scale = 1;
  material->pointalism_z = 1;
  material->sink = NULL;

  rut_simple_introspectable_init (material,
                                  _rut_material_prop_specs,
                                  material->properties);

  material->uniforms_flush_age = -1;

  material->texture_asset = NULL;
  material->normal_map_asset = NULL;
  material->alpha_mask_asset = NULL;
  material->video_texture_asset = NULL;
  material->video_renderer = NULL;
  material->circle_shape = NULL;

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
        case RUT_ASSET_TYPE_VIDEO:
          material->video_texture_asset = rut_refable_ref (asset);
          material->video_renderer = rut_video_renderer_new (ctx->cogl_context,
                                                             material->pointalism_columns,
                                                             material->pointalism_rows,
                                                             material->pointalism_cell_size);
          material->circle_shape = ctx->circle_texture;
          rut_material_video_play (material, ctx);
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
    
  if (material->video_texture_asset)
    {
      rut_refable_unref (material->video_texture_asset);
      material->video_texture_asset = NULL;
      rut_material_video_stop (material);
      material->pointalism_on = FALSE;
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
    
  if (material->video_texture_asset)
    {
      rut_refable_unref (material->video_texture_asset);
      material->video_texture_asset = NULL;
      rut_material_video_stop (material);
      material->pointalism_on = FALSE;
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
    
  if (material->video_texture_asset)
    {
      rut_refable_unref (material->video_texture_asset);
      material->video_texture_asset = NULL;
      rut_material_video_stop (material);
      material->pointalism_on = FALSE;
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
rut_material_set_video_texture_asset (RutContext *ctx,
                                      RutMaterial *material,
                                      RutAsset *asset)
{
  if (material->texture_asset)
    {
      rut_refable_unref (material->texture_asset);
      material->texture_asset = NULL;
      rut_material_video_stop (material);
    }

  if (material->video_texture_asset)
    {
      rut_refable_unref (material->video_texture_asset);
      material->video_texture_asset = NULL;
    }
    
  if (material->normal_map_asset)
    {
      rut_refable_unref (material->normal_map_asset);
      material->normal_map_asset = NULL;
    }
    
  if (material->alpha_mask_asset)
    {
      rut_refable_unref (material->alpha_mask_asset);
      material->alpha_mask_asset = NULL;
    }
    
  if (material->video_renderer)
    {
      cogl_object_unref (material->circle_shape);
      cogl_object_unref (material->video_renderer->attributes[0]);
      cogl_object_unref (material->video_renderer->attributes[1]);
      cogl_object_unref (material->video_renderer->indices);
      g_free (material->video_renderer->grid);
      g_free (material->video_renderer);
    }
    
  if (asset)
    {
      material->video_texture_asset = rut_refable_ref (asset);
      material->video_renderer = rut_video_renderer_new (ctx->cogl_context,
                                                         material->pointalism_columns,
                                                         material->pointalism_rows,
                                                         material->pointalism_cell_size);
      material->circle_shape = ctx->circle_texture;
      rut_material_video_play (material, ctx);
    }
}

void
rut_material_reset_video_texture_renderer (RutMaterial *material,
                                           RutContext *ctx)
{
  if (material->video_renderer)
    {
      cogl_object_unref (material->video_renderer->attributes[0]);
      cogl_object_unref (material->video_renderer->attributes[1]);
      cogl_object_unref (material->video_renderer->indices);
      g_free (material->video_renderer->grid);
      g_free (material->video_renderer);
      material->video_renderer = NULL;
    }
    
  material->video_renderer = rut_video_renderer_new (ctx->cogl_context,
                                                     material->pointalism_columns, 
                                                     material->pointalism_rows,
                                                     material->pointalism_cell_size);
}

RutAsset *
rut_material_get_video_texture_asset (RutMaterial *material)
{
  return material->video_texture_asset;
}

CoglBool
loop_video (GstBus *bus, 
            GstMessage *msg, 
            void *data)
{
  GstElement *pipeline = GST_ELEMENT(data);
  switch (GST_MESSAGE_TYPE(msg))
    {
      case GST_MESSAGE_EOS:
        gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                          GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, 
                          GST_CLOCK_TIME_NONE);
        break;
      default:
        break;
    }
  return TRUE;
}

void
rut_material_video_play (RutMaterial *material,
                         RutContext *ctx)
{
  GstBus* bus;
  char *uri;
  char *filename = g_build_filename (ctx->assets_location, 
                                     rut_asset_get_path (material->video_texture_asset), 
                                     NULL);
  
  if (material->sink)
    {
      gst_element_set_state (material->pipeline, GST_STATE_NULL);
      g_object_unref (material->sink);
      g_object_unref (material->pipeline);
      g_object_unref (material->bin);
    }

  material->sink = cogl_gst_video_sink_new (ctx->cogl_context);
  material->pipeline = gst_pipeline_new ("renderer");     
  material->bin = gst_element_factory_make ("playbin", NULL);
  uri = g_strconcat ("file://", filename, NULL);
  g_object_set (G_OBJECT (material->bin), "video-sink", 
                GST_ELEMENT (material->sink), NULL);
  g_object_set (G_OBJECT (material->bin), "uri", uri, NULL);
  gst_bin_add (GST_BIN (material->pipeline), material->bin);
  
  bus = gst_pipeline_get_bus (GST_PIPELINE (material->pipeline));
  gst_bus_add_watch (bus, loop_video, material->pipeline);
  
  gst_element_set_state (material->pipeline, GST_STATE_PLAYING);
  
  g_free (uri);
  g_free (filename);
  gst_object_unref (bus);
}

void
rut_material_video_stop (RutMaterial *material)
{
  if (material->sink)
    {
      gst_element_set_state (material->pipeline, GST_STATE_NULL);
      g_object_unref (material->sink);
    }
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

CoglBool
rut_material_get_pointalism_on (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  
  return material->pointalism_on;
}

void 
rut_material_set_pointalism_on (RutObject *obj,
                                CoglBool pointalism_on)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  RutEntity *entity;
  RutContext *ctx;
  
  if (pointalism_on == material->pointalism_on)
    return;
  
  material->pointalism_on = pointalism_on;
  
  entity = material->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx, 
                        &material->properties[RUT_MATERIAL_PROP_POINTALISM_ON]);
}

float
rut_material_get_pointalism_scale (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  
  return material->pointalism_scale;
}

void
rut_material_set_pointalism_scale (RutObject *obj,
                                   float scale)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  RutEntity *entity;
  RutContext *ctx;
  
  if (scale == material->pointalism_scale)
    return;
  
  material->pointalism_scale = scale;
  
  entity = material->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx, 
                     &material->properties[RUT_MATERIAL_PROP_POINTALISM_SCALE]);
}

float
rut_material_get_pointalism_z (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  
  return material->pointalism_z;
}



void
rut_material_set_pointalism_z (RutObject *obj,
                               float z)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  RutEntity *entity;
  RutContext *ctx;
  
  if (z == material->pointalism_z)
    return;
  
  material->pointalism_z = z;
  
  entity = material->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx, 
                     &material->properties[RUT_MATERIAL_PROP_POINTALISM_Z]);
}

/*
int
rut_material_get_pointalism_columns (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  
  return material->pointalism_columns;
}

void
rut_material_set_pointalism_columns (RutObject *obj,
                                     int cols)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  RutEntity *entity;
  RutContext *ctx;
  
  if (cols == material->pointalism_columns)
    return;
  
  material->pointalism_columns = cols;
  
  entity = material->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx, 
                   &material->properties[RUT_MATERIAL_PROP_POINTALISM_COLUMNS]);
  rut_material_reset_video_texture_renderer (material, ctx);
}
                                     
int
rut_material_get_pointalism_rows (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  
  return material->pointalism_rows;
}

void
rut_material_set_pointalism_rows (RutObject *obj,
                                  int rows)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  RutEntity *entity;
  RutContext *ctx;
  
  if (rows == material->pointalism_rows)
    return;
  
  material->pointalism_rows = rows;
  
  entity = material->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx, 
                      &material->properties[RUT_MATERIAL_PROP_POINTALISM_ROWS]);
  rut_material_reset_video_texture_renderer (material, ctx);
}
                                  
float
rut_material_get_pointalism_cell_size (RutObject *obj)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  
  return material->pointalism_cell_size;
}

void
rut_material_set_pointalism_cell_size (RutObject *obj,
                                       float size)
{
  RutMaterial *material = RUT_MATERIAL (obj);
  RutEntity *entity;
  RutContext *ctx;
  
  if (size == material->pointalism_cell_size)
    return;
  
  material->pointalism_cell_size = size;
  
  entity = material->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx, 
                 &material->properties[RUT_MATERIAL_PROP_POINTALISM_CELL_SIZE]);
  rut_material_reset_video_texture_renderer (material, ctx);
}*/

void
rut_material_flush_uniforms (RutMaterial *material,
                             CoglPipeline *pipeline)
{
  int location;

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

  material->uniforms_flush_age = material->uniforms_age;
}

void
rut_material_dirty_uniforms (RutMaterial *material)
{
  material->uniforms_flush_age = material->uniforms_age -1;
}
