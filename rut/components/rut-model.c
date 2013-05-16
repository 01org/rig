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

#include <math.h>

#include "rut-global.h"
#include "rut-types.h"
#include "rut-geometry.h"
#include "rut-mesh.h"
#include "rut-mesh-ply.h"

#include "components/rut-material.h"
#include "components/rut-model.h"

#define PI 3.14159265359

CoglPrimitive *
rut_model_get_primitive (RutObject *object)
{
  RutModel *model = object;

  if (!model->primitive)
    {
      if (model->mesh)
        {
          model->primitive =
            rut_mesh_create_primitive (model->ctx, model->mesh);
        }
    }

  return model->primitive;
}

RutType rut_model_type;

static RutComponentableVTable _rut_model_componentable_vtable = {
  .draw = NULL
};

static RutPrimableVTable _rut_model_primable_vtable = {
  .get_primitive = rut_model_get_primitive
};

static RutPickableVTable _rut_model_pickable_vtable = {
  .get_mesh = rut_model_get_mesh
};

static void
_rut_model_free (void *object)
{
  RutModel *model = object;

  if (model->primitive)
    cogl_object_unref (model->primitive);

  if (model->mesh)
    rut_refable_unref (model->mesh);

  g_slice_free (RutModel, model);
}

static RutRefCountableVTable _rut_model_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_model_free
};

void
_rut_model_init_type (void)
{
  rut_type_init (&rut_model_type, "RigModel");
  rut_type_add_interface (&rut_model_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutModel, ref_count),
                          &_rut_model_ref_countable_vtable);
  rut_type_add_interface (&rut_model_type,
                          RUT_INTERFACE_ID_COMPONENTABLE,
                          offsetof (RutModel, component),
                          &_rut_model_componentable_vtable);
  rut_type_add_interface (&rut_model_type,
                          RUT_INTERFACE_ID_PRIMABLE,
                          0, /* no associated properties */
                          &_rut_model_primable_vtable);
  rut_type_add_interface (&rut_model_type,
                          RUT_INTERFACE_ID_PICKABLE,
                          0, /* no associated properties */
                          &_rut_model_pickable_vtable);
}

static RutModel *
_rut_model_new (RutContext *ctx)
{
  RutModel *model;

  model = g_slice_new0 (RutModel);
  rut_object_init (&model->_parent, &rut_model_type);
  model->ref_count = 1;
  model->component.type = RUT_COMPONENT_TYPE_GEOMETRY;
  model->ctx = ctx;

  return model;
}

static float
calculate_magnitude (float x,
                     float y,
                     float z)
{
  return sqrt (x * x + y * y + z * z);
}

float
calculate_dot_product (float *v1,
                       float *v2)
{
  return (v1[0] * v2[0]) + (v1[1] * v2[1]) + (v1[2] * v2[2]);
}

static void
normalize_vertex (float *vertex)
{
  float magnitude = calculate_magnitude (vertex[0], vertex[1], vertex[2]);

  vertex[0] = vertex[0] / magnitude;
  vertex[1] = vertex[1] / magnitude;
  vertex[2] = vertex[2] / magnitude;
}

static CoglBool
measure_mesh_x_cb (void **attribute_data,
                   int vertex_index,
                   void *user_data)
{
  RutModel *model = user_data;
  float *pos = attribute_data[0];

  if (pos[0] < model->min_x)
    model->min_x = pos[0];
  if (pos[0] > model->max_x)
    model->max_x = pos[0];

  return TRUE;
}

static CoglBool
measure_mesh_xy_cb (void **attribute_data,
                    int vertex_index,
                    void *user_data)
{
  RutModel *model = user_data;
  float *pos = attribute_data[0];

  measure_mesh_x_cb (attribute_data, vertex_index, user_data);

  if (pos[1] < model->min_y)
    model->min_y = pos[1];
  if (pos[1] > model->max_y)
    model->max_y = pos[1];

  return TRUE;
}

static CoglBool
measure_mesh_xyz_cb (void **attribute_data,
                     int vertex_index,
                     void *user_data)
{
  RutModel *model = user_data;
  float *pos = attribute_data[0];
  float *normal = attribute_data[1];
  float *tangent = attribute_data[2];

  measure_mesh_xy_cb (attribute_data, vertex_index, user_data);

  if (pos[2] < model->min_z)
    model->min_z = pos[2];
  if (pos[2] > model->max_z)
    model->max_z = pos[2];

  normal[0] = 0;
  normal[1] = 0;
  normal[2] = 0;

  tangent[0] = 0;
  tangent[1] = 0;
  tangent[2] = 0;

  return TRUE;
}

static CoglBool
calculate_tangent_space (void **attribute_data_v0,
                         void **attribute_data_v1,
                         void **attribute_data_v2,
                         int v0_index,
                         int v1_index,
                         int v2_index,
                         void *user_data)
{
  float *vert_p0 = attribute_data_v0[0];
  float *vert_p1 = attribute_data_v1[0];
  float *vert_p2 = attribute_data_v2[0];

  float *vert_n0 = attribute_data_v0[1];
  float *vert_n1 = attribute_data_v1[1];
  float *vert_n2 = attribute_data_v2[1];

  float *vert_t0 = attribute_data_v0[2];
  float *vert_t1 = attribute_data_v1[2];
  float *vert_t2 = attribute_data_v2[2];

  float *tex_coord0 = attribute_data_v0[3];
  float *tex_coord1 = attribute_data_v1[3];
  float *tex_coord2 = attribute_data_v2[3];

  float edge1[3];
  float edge2[3];
  float tex_edge1[2];
  float tex_edge2[2];
  float poly_normal[3];
  float poly_tangent[3];
  float coef;
  float center[3], dir1[3], dir2[3], angle;
  int i;
  RutModel *model = user_data;

  /* Vertex 1 */

  center[0] = (model->min_x + model->max_x) * 0.5;
  center[1] = vert_p0[1];
  center[2] = (model->min_z + model->max_z) * 0.5;

  dir2[0] = model->min_x - center[2];
  dir2[1] = vert_p0[1] - center[1];
  dir2[2] = model->min_z - center[2];

  dir1[0] = vert_p0[0] - center[0];
  dir1[1] = vert_p0[1] - center[1];
  dir1[2] = vert_p0[2] - center[2];

  angle = atan2 (dir1[0], dir1[2]) - atan2 (dir2[0], dir2[2]);

  if (angle < 0)
    angle = (2.0 * PI) + angle;

  if (angle > 0)
    tex_coord0[0] = angle/ (2.0 * PI);
  else
    tex_coord0[0] = 0;

  tex_coord0[1] = (vert_p0[1] - model->min_y) / (model->max_y - model->min_y);

  if (tex_coord0[0] <= 0)
    {
      if (vert_p0[0] < vert_p1[0] ||
          vert_p0[0] < vert_p2[0] )
        tex_coord0[0] = 1;
    }

  if ((vert_p0[1] == model->max_y ||
      vert_p0[1] == model->min_y) &&
      vert_p0[1] == vert_p1[1] &&
      vert_p0[1] == vert_p2[1])
    {
      tex_coord0[0] = (vert_p0[0] - model->min_x) /
                      (model->max_x - model->min_x);
      tex_coord0[1] = (vert_p0[2] - model->min_z) /
                      (model->max_z - model->min_z);
    }

  /* Vertex 2 */

  center[1] = vert_p1[1];
  dir2[1] = vert_p1[1] - center[1];

  dir1[0] = vert_p1[0] - center[0];
  dir1[1] = vert_p1[1] - center[1];
  dir1[2] = vert_p1[2] - center[2];

  angle = atan2 (dir1[0], dir1[2]) - atan2 (dir2[0], dir2[2]);

  if (angle < 0)
    angle = (2.0 * PI) + angle;

  if (angle > 0)
    tex_coord1[0] = angle/ (2.0 * PI);
  else
    tex_coord1[0] = 0;

  tex_coord1[1] = (vert_p1[1] - model->min_y) / (model->max_y - model->min_y);

  if (tex_coord1[0] <= 0)
    {
      if (vert_p1[0] < vert_p0[0] ||
          vert_p1[0] < vert_p2[0] )
        tex_coord1[0] = 1;
    }

  if ((vert_p1[1] == model->max_y ||
      vert_p1[1] == model->min_y) &&
      vert_p1[1] == vert_p0[1] &&
      vert_p1[1] == vert_p2[1])
    {
      tex_coord1[0] = (vert_p1[0] - model->min_x) /
                      (model->max_x - model->min_x);
      tex_coord1[1] = (vert_p1[2] - model->min_z) /
                      (model->max_z - model->min_z);
    }

  /* Vertex 3 */

  center[1] = vert_p2[1];
  dir2[1] = vert_p2[1] - center[1];

  dir1[0] = vert_p2[0] - center[0];
  dir1[1] = vert_p2[1] - center[1];
  dir1[2] = vert_p2[2] - center[2];

  angle = atan2 (dir1[0], dir1[2]) - atan2 (dir2[0], dir2[2]);

  if (angle < 0)
    angle = (2.0 * PI) + angle;

  if (angle > 0)
    tex_coord2[0] = angle/ (2.0 * PI);
  else
    tex_coord2[0] = 0;

  tex_coord2[1] = (vert_p2[1] - model->min_y) / (model->max_y - model->min_y);

  if (tex_coord2[0] <= 0)
    {
      if (vert_p2[0] < vert_p0[0] ||
          vert_p2[0] < vert_p1[0] )
        tex_coord2[0] = 1;
    }

  if ((vert_p2[1] == model->max_y ||
      vert_p2[1] == model->min_y) &&
      vert_p2[1] == vert_p0[1] &&
      vert_p2[1] == vert_p1[1])
    {
      tex_coord2[0] = (vert_p2[0] - model->min_x) /
                      (model->max_x - model->min_x);
      tex_coord2[1] = (vert_p2[2] - model->min_z) /
                      (model->max_z - model->min_z);
    }

  edge1[0] = vert_p1[0] - vert_p0[0];
  edge1[1] = vert_p1[1] - vert_p0[1];
  edge1[2] = vert_p1[2] - vert_p0[2];

  edge2[0] = vert_p2[0] - vert_p0[0];
  edge2[1] = vert_p2[1] - vert_p0[1];
  edge2[2] = vert_p2[2] - vert_p0[2];

  poly_normal[0] = (edge1[1] * edge2[2]) - (edge1[2] * edge2[1]);
  poly_normal[1] = (edge1[2] * edge2[0]) - (edge1[0] * edge2[2]);
  poly_normal[2] = (edge1[0] * edge2[1]) - (edge1[1] * edge2[0]);

  normalize_vertex (poly_normal);

  vert_n0[0] += poly_normal[0];
  vert_n0[1] += poly_normal[1];
  vert_n0[2] += poly_normal[2];

  normalize_vertex (vert_n0);

  vert_n1[0] += poly_normal[0];
  vert_n1[1] += poly_normal[1];
  vert_n1[2] += poly_normal[2];

  normalize_vertex (vert_n1);

  vert_n2[0] += poly_normal[0];
  vert_n2[1] += poly_normal[1];
  vert_n2[2] += poly_normal[2];

  normalize_vertex (vert_n2);

  tex_edge1[0] = tex_coord1[0] - tex_coord0[0];
  tex_edge1[1] = tex_coord1[1] - tex_coord0[1];

  tex_edge2[0] = tex_coord2[0] - tex_coord0[0];
  tex_edge2[1] = tex_coord2[1] - tex_coord0[1];

  coef = 1 / (tex_edge1[0] * tex_edge2[1] - tex_edge2[0] * tex_edge1[1]);

  poly_tangent[0] = coef * ((edge1[0] * tex_edge2[1]) -
                            (edge2[0] * tex_edge1[1]));
  poly_tangent[1] = coef * ((edge1[1] * tex_edge2[1]) -
                            (edge2[1] * tex_edge1[1]));
  poly_tangent[2] = coef * ((edge1[2] * tex_edge2[1]) -
                            (edge2[2] * tex_edge1[1]));

  normalize_vertex (poly_tangent);

  vert_t0[0] += poly_tangent[0];
  vert_t0[1] += poly_tangent[1];
  vert_t0[2] += poly_tangent[2];

  normalize_vertex (vert_t0);

  vert_t1[0] += poly_tangent[0];
  vert_t1[1] += poly_tangent[1];
  vert_t1[2] += poly_tangent[2];

  normalize_vertex (vert_t1);

  vert_t2[0] += poly_tangent[0];
  vert_t2[1] += poly_tangent[1];
  vert_t2[2] += poly_tangent[2];

  normalize_vertex (vert_t2);

  for (i = 4; i < 7; i++)
    {
      float *tex = attribute_data_v0[i];
      tex[0] = tex_coord0[0];
      tex[1] = tex_coord0[1];
      tex = attribute_data_v1[i];
      tex[0] = tex_coord1[0];
      tex[1] = tex_coord1[1];
      tex = attribute_data_v2[i];
      tex[0] = tex_coord2[0];
      tex[1] = tex_coord2[1];
    }

  return TRUE;
}

RutModel *
rut_model_new_from_mesh (RutContext *ctx,
                         RutMesh *mesh)
{
  RutModel *model;
  RutAttribute *attribute;
  RutMeshVertexCallback measure_callback;

  model = _rut_model_new (ctx);
  model->type = RUT_MODEL_TYPE_FILE;
  model->mesh = rut_refable_ref (mesh);

  attribute = rut_mesh_find_attribute (model->mesh, "cogl_position_in");

  model->min_x = G_MAXFLOAT;
  model->max_x = G_MINFLOAT;
  model->min_y = G_MAXFLOAT;
  model->max_y = G_MINFLOAT;
  model->min_z = G_MAXFLOAT;
  model->max_z = G_MINFLOAT;

  if (attribute->n_components == 1)
    {
      model->min_y = model->max_y = 0;
      model->min_z = model->max_z = 0;
      measure_callback = measure_mesh_x_cb;
    }
  else if (attribute->n_components == 2)
    {
      model->min_z = model->max_z = 0;
      measure_callback = measure_mesh_xy_cb;
    }
  else if (attribute->n_components == 3)
    measure_callback = measure_mesh_xyz_cb;

  rut_mesh_foreach_vertex (model->mesh,
                           measure_callback,
                           model,
                           "cogl_position_in",
                           "cogl_normal_in",
                           "tangent_in",
                           NULL);

  rut_mesh_foreach_triangle (model->mesh,
                             calculate_tangent_space,
                             model,
                             "cogl_position_in",
                             "cogl_normal_in",
                             "tangent_in",
                             "cogl_tex_coord0_in",
                             "cogl_tex_coord1_in",
                             "cogl_tex_coord2_in",
                             "cogl_tex_coord5_in",
                             NULL);

  return model;
}

RutModel *
rut_model_new_from_asset (RutContext *ctx, RutAsset *asset)
{
  RutMesh *mesh = rut_asset_get_mesh (asset);
  RutModel *model;

  if (!mesh)
    return NULL;

  model = rut_model_new_from_mesh (ctx, mesh);
  model->asset = rut_refable_ref (asset);

  return model;
}

RutMesh *
rut_model_get_mesh (RutObject *self)
{
  RutModel *model = self;
  return model->mesh;
}

RutAsset *
rut_model_get_asset (RutModel *model)
{
  return model->asset;
}

