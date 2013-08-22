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

#include "components/rut-model.h"

#define PI 3.14159265359

/* The vertex structure */

typedef struct _Vertex
{
  float pos[3];
  float normal[3];
  float tx, ty, tz;
  float s0, t0; /* source image coordinates */
  float s1, t1; /* patch/fin texture coordinates */
}Vertex;

/* The polygon structure */

typedef struct _Polygon
{
  int id;
  Vertex *vertices[3];
  Vertex flat_vertices[3];
  float tangent[3];
  float normal[3];
  CoglBool uncovered;
}Polygon;

/* Texture patch structure */

typedef struct _TexturePatch
{
  GList *polygons;
  Polygon *root;
  float tangent_angle;
  float width;
  float height;
}TexturePatch;

typedef struct _RutModelPrivate
{
  GList *texture_patches;
  Polygon *fin_polygons;
  Vertex *fin_vertices;
  Polygon *polygons;
  Vertex *vertices;
  RutBitmask adj_matrix;
  int n_polygons;
  int n_vertices;
  int n_fin_polygons;
  int n_fin_vertices;
}RutModelPrivate;

/* Some convinient constants */
static float flat_normal[3] = { 0, 0, 1 };

static RutModel *
_rut_model_new (RutContext *ctx);

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

CoglPrimitive *
rut_model_get_fin_primitive (RutObject *object)
{
  RutModel *model = object;

  return model->fin_primitive;
}

static void
_rut_model_free (void *object)
{
  RutModel *model = object;

  if (model->primitive)
    cogl_object_unref (model->primitive);

  if (model->mesh)
    rut_refable_unref (model->mesh);

  if (model->patched_mesh)
    {
      g_free (model->priv->polygons);
      g_free (model->priv->vertices);
      rut_refable_unref (model->patched_mesh);
    }

  if (model->fin_mesh)
    {
      cogl_object_unref (model->fin_primitive);
      g_free (model->priv->fin_polygons);
      g_free (model->priv->fin_vertices);
      rut_refable_unref (model->fin_mesh);
    }

  g_free (model->priv);
  g_slice_free (RutModel, model);
}

static RutObject *
_rut_model_copy (RutObject *object)
{
  RutModel *model = object;
  RutModel *copy = _rut_model_new (model->ctx);

  copy->type = model->type;
  copy->mesh = rut_refable_ref (model->mesh);

  if (model->asset)
    copy->asset = rut_refable_ref (model->asset);

  copy->min_x = model->min_x;
  copy->max_x = model->max_x;
  copy->min_y = model->min_y;
  copy->max_y = model->max_y;
  copy->min_z = model->min_z;
  copy->max_z = model->max_z;

  copy->builtin_normals = model->builtin_normals;
  copy->builtin_tex_coords = model->builtin_tex_coords;

  if (model->primitive)
    copy->primitive = cogl_object_ref (model->primitive);

  if (model->is_hair_model)
    {
      copy->is_hair_model = model->is_hair_model;
      copy->patched_mesh = rut_refable_ref (model->patched_mesh);
      copy->fin_mesh = rut_refable_ref (model->fin_mesh);
      if (copy->fin_primitive)
        copy->fin_primitive = cogl_object_ref (model->fin_primitive);
      copy->default_hair_length = model->default_hair_length;
    }

  return copy;
}

RutType rut_model_type;

void
_rut_model_init_type (void)
{
  static RutRefableVTable refable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _rut_model_free
  };

  static RutComponentableVTable componentable_vtable = {
    .copy = _rut_model_copy
  };

  static RutPrimableVTable primable_vtable = {
    .get_primitive = rut_model_get_primitive
  };

  static RutPickableVTable pickable_vtable = {
    .get_mesh = rut_model_get_mesh
  };


  RutType *type = &rut_model_type;
#define TYPE RutModel

  rut_type_init (type, G_STRINGIFY (TYPE));
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &refable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_COMPONENTABLE,
                          offsetof (TYPE, component),
                          &componentable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_PRIMABLE,
                          0, /* no associated properties */
                          &primable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_PICKABLE,
                          0, /* no associated properties */
                          &pickable_vtable);

#undef TYPE
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

/* Some vertex utility functions */

static void
substract_vertices (float *result, Vertex *v1, Vertex *v2)
{
  result[0] = v1->pos[0] - v2->pos[0];
  result[1] = v1->pos[1] - v2->pos[1];
  result[2] = v1->pos[2] - v2->pos[2];
}

static float
calculate_dot_product (float *v1, float *v2)
{
  return (v1[0] * v2[0]) + (v1[1] * v2[1]) + (v1[2] * v2[2]);
}

static void
calculate_cross_product (float *result, float *v1, float *v2)
{
  result[0] = (v1[1] * v2[2]) - (v2[1] * v1[2]);
  result[1] = (v1[2] * v2[0]) - (v2[2] * v1[0]);
  result[2] = (v1[0] * v2[1]) - (v2[0] * v1[1]);
}

static void
calculate_triangle_centroid (float *result,
                             Vertex *v1,
                             Vertex *v2,
                             Vertex *v3)
{
  result[0] = (v1->pos[0] + v2->pos[0] + v3->pos[0]) / 3.f;
  result[1] = (v1->pos[1] + v2->pos[1] + v3->pos[1]) / 3.f;
  result[2] = (v1->pos[2] + v2->pos[2] + v3->pos[2]) / 3.f;
}

static float
calculate_magnitude (float x,
                     float y,
                     float z)
{
  return sqrt (x * x + y * y + z * z);
}

static void
rotate_vertex_around_custom_axis (float *vertex,
                                  float *axis,
                                  float angle)
{
  float w[3], u[3];
  float v[3];
  float div = calculate_dot_product (vertex, axis) /
    calculate_dot_product (axis, axis);
  float length = calculate_magnitude (axis[0], axis[1], axis[2]);
  float cosine = cosf (angle);
  float sine = sinf (angle);

  w[0] = axis[0] * div;
  w[1] = axis[1] * div;
  w[2] = axis[2] * div;

  u[0] = vertex[0] - w[0];
  u[1] = vertex[1] - w[1];
  u[2] = vertex[2] - w[2];

  calculate_cross_product (v, axis, u);

  v[0] /= length;
  v[1] /= length;
  v[2] /= length;

  vertex[0] = w[0] + u[0] * cosine + v[0] * sine;
  vertex[1] = w[1] + u[1] * cosine + v[1] * sine;
  vertex[2] = w[2] + u[2] * cosine + v[2] * sine;
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
check_vertex_equality (Vertex *v1, Vertex *v2)
{
  return v1->pos[0] == v2->pos[0] &&
         v1->pos[1] == v2->pos[1] &&
         v1->pos[2] == v2->pos[2];
}

/* Calculate polygon tangent */

static void
calculate_poly_tangent (Polygon *poly)
{
  float edge1[3];
  float edge2[3];
  float tex_edge1[2];
  float tex_edge2[2];
  float coef;

  substract_vertices (edge1, poly->vertices[1], poly->vertices[0]);
  substract_vertices (edge2, poly->vertices[2], poly->vertices[0]);

  tex_edge1[0] = poly->vertices[1]->s0 - poly->vertices[0]->s0;
  tex_edge1[1] = poly->vertices[1]->t0 - poly->vertices[0]->t0;

  tex_edge2[0] = poly->vertices[2]->s0 - poly->vertices[0]->s0;
  tex_edge2[1] = poly->vertices[2]->t0 - poly->vertices[0]->t0;

  coef = 1 / (tex_edge1[0] * tex_edge2[1] - tex_edge2[0] * tex_edge1[1]);

  poly->tangent[0] = coef * ((edge1[0] * tex_edge2[1]) +
                             (edge2[0] * (-1 * tex_edge1[1])));
  poly->tangent[1] = coef * ((edge1[1] * tex_edge2[1]) +
                             (edge2[1] * (-1 * tex_edge1[1])));
  poly->tangent[2] = coef * ((edge1[2] * tex_edge2[1]) +
                             (edge2[2] * (-1 * tex_edge1[1])));

  normalize_vertex (poly->tangent);
}

static void
calculate_vertex_tangent (Polygon *poly, Vertex *vertex)
{
  float tangent[3];

  tangent[0] = poly->tangent[0] + vertex->tx;
  tangent[1] = poly->tangent[1] + vertex->ty;
  tangent[2] = poly->tangent[2] + vertex->tz;

  normalize_vertex (tangent);

  vertex->tx = tangent[0];
  vertex->ty = tangent[1];
  vertex->tz = tangent[2];
}

/* Calculate polygon normal */

static void
calculate_poly_normal (Polygon *poly)
{
  float edge1[3], edge2[3];

  substract_vertices (edge1, poly->vertices[1], poly->vertices[0]);
  substract_vertices (edge2, poly->vertices[2], poly->vertices[0]);

  calculate_cross_product (poly->normal, edge1, edge2);
  normalize_vertex (poly->normal);
}

/* Generate cylindrical uv coordinates for a single vertex */

static void
calculate_cylindrical_uv_coordinates (RutModel *model,
                                      float *position,
                                      float *tex)
{
  float center[3], dir1[3], dir2[3], angle;

  center[0] = (model->min_x + model->max_x) * 0.5;
  center[1] = position[1];
  center[2] = (model->min_z + model->max_z) * 0.5;

  dir2[0] = model->min_x - center[0];
  dir2[1] = position[1] - center[1];
  dir2[2] = model->min_z - center[2];

  dir1[0] = position[0] - center[0];
  dir1[1] = position[1] - center[1];
  dir1[2] = position[2] - center[2];

  angle = atan2 (dir1[0], dir1[2]) - atan2 (dir2[0], dir2[2]);

  if (angle < 0)
    angle = (2.0 * PI) + angle;

  if (angle > 0)
    tex[0] = angle/ (2.0 * PI);
  else
    tex[0] = 0;

  tex[1] = (position[1] - model->min_y) / (model->max_y - model->min_y);
}

static void
generate_cylindrical_uv_coordinates (Vertex *vertex,
                                     RutModel *model)
{
  float pos[3] = { vertex->pos[0], vertex->pos[1], vertex->pos[2] };
  float tex[2];

  calculate_cylindrical_uv_coordinates (model, pos, tex);
  vertex->s0 = tex[0];
  vertex->t0 = tex[1];
}

static void
add_polygon_fins (RutModel *model,
                  Polygon *polygon)
{
  Vertex *fin_verts[12];
  Polygon *fin_polys[6];
  int poly_iter = model->priv->n_fin_polygons;
  int vert_iter = model->priv->n_fin_vertices;
  int edges[3][2];
  int i, j;

  edges[0][0] = 0;
  edges[0][1] = 1;
  edges[1][0] = 1;
  edges[1][1] = 2;
  edges[2][0] = 2;
  edges[2][1] = 0;

  j = 0;

  for (i = 0; i < 3; i++)
    {
      int cv = i * 4;
      fin_verts[cv] = &model->priv->fin_vertices[vert_iter + cv];
      fin_verts[cv + 1] = &model->priv->fin_vertices[vert_iter + cv + 1];
      fin_verts[cv + 2] = &model->priv->fin_vertices[vert_iter + cv + 2];
      fin_verts[cv + 3] = &model->priv->fin_vertices[vert_iter + cv + 3];
      fin_polys[j] = &model->priv->fin_polygons[poly_iter + j];
      fin_polys[j + 1] = &model->priv->fin_polygons[poly_iter + j + 1];

      fin_polys[j]->vertices[0] = fin_verts[cv];
      fin_polys[j]->vertices[1] = fin_verts[cv + 1];
      fin_polys[j]->vertices[2] = fin_verts[cv + 2];
      fin_polys[j + 1]->vertices[0] = fin_verts[cv + 2];
      fin_polys[j + 1]->vertices[1] = fin_verts[cv + 3];
      fin_polys[j + 1]->vertices[2] = fin_verts[cv];

      fin_verts[cv]->pos[0] = fin_verts[cv + 1]->pos[0] =
        polygon->vertices[edges[i][0]]->pos[0];
      fin_verts[cv]->pos[1] = fin_verts[cv + 1]->pos[1] =
        polygon->vertices[edges[i][0]]->pos[1];
      fin_verts[cv]->pos[2] = fin_verts[cv + 1]->pos[2] =
        polygon->vertices[edges[i][0]]->pos[2];

      fin_verts[cv]->normal[0] = fin_verts[cv + 1]->normal[0] =
        polygon->vertices[edges[i][0]]->normal[0];
      fin_verts[cv]->normal[1] = fin_verts[cv + 1]->normal[1] =
        polygon->vertices[edges[i][0]]->normal[1];
      fin_verts[cv]->normal[2] = fin_verts[cv + 1]->normal[2] =
        polygon->vertices[edges[i][0]]->normal[2];

      fin_verts[cv]->tx = fin_verts[cv + 1]->tx =
        polygon->vertices[edges[i][0]]->tx;
      fin_verts[cv]->ty = fin_verts[cv + 1]->ty =
        polygon->vertices[edges[i][0]]->ty;
      fin_verts[cv]->tz = fin_verts[cv + 1]->tz =
        polygon->vertices[edges[i][0]]->tz;

      fin_verts[cv + 2]->pos[0] = fin_verts[cv + 3]->pos[0] =
        polygon->vertices[edges[i][1]]->pos[0];
      fin_verts[cv + 2]->pos[1] = fin_verts[cv + 3]->pos[1] =
        polygon->vertices[edges[i][1]]->pos[1];
      fin_verts[cv + 2]->pos[2] = fin_verts[cv + 3]->pos[2] =
        polygon->vertices[edges[i][1]]->pos[2];

      fin_verts[cv + 2]->normal[0] = fin_verts[cv + 3]->normal[0] =
        polygon->vertices[edges[i][1]]->normal[0];
      fin_verts[cv + 2]->normal[1] = fin_verts[cv + 3]->normal[1] =
        polygon->vertices[edges[i][1]]->normal[1];
      fin_verts[cv + 2]->normal[2] = fin_verts[cv + 3]->normal[2] =
        polygon->vertices[edges[i][1]]->normal[2];

      fin_verts[cv + 2]->tx = fin_verts[cv + 3]->tx =
        polygon->vertices[edges[i][1]]->tx;
      fin_verts[cv + 2]->ty = fin_verts[cv + 3]->ty =
        polygon->vertices[edges[i][1]]->ty;
      fin_verts[cv + 2]->tz = fin_verts[cv + 3]->tz =
        polygon->vertices[edges[i][1]]->tz;

      fin_verts[cv]->s0 = fin_verts[cv + 1]->s0 =
        polygon->vertices[edges[i][0]]->s0;

      fin_verts[cv]->t0 = fin_verts[cv + 1]->t0 =
        polygon->vertices[edges[i][0]]->t0;

      fin_verts[cv + 2]->s0 = fin_verts[cv + 3]->s0 =
        polygon->vertices[edges[i][1]]->s0;

      fin_verts[cv + 2]->t0 = fin_verts[cv + 3]->t0 =
        polygon->vertices[edges[i][1]]->t0;

      fin_verts[cv]->s1 = fin_verts[cv + 1]->s1 = 0;
      fin_verts[cv + 1]->s1 = fin_verts[cv + 3]->s1 = 1;

      fin_verts[cv + 1]->t1 = fin_verts[cv + 2]->t1 = 0;
      fin_verts[cv]->t1 = fin_verts[cv + 3]->t1 = 1;

      j += 2;
    }

  model->priv->n_fin_polygons += 6;
  model->priv->n_fin_vertices += 12;
}

static void
calculate_tangents (float *position0,
                    float *position1,
                    float *position2,
                    float *tex0,
                    float *tex1,
                    float *tex2,
                    float *tangent0,
                    float *tangent1,
                    float *tangent2)
{
  float edge1[3];
  float edge2[3];
  float tex_edge1[3];
  float tex_edge2[3];
  float coef;
  float poly_tangent[3];

  edge1[0] = position1[0] - position0[0];
  edge1[1] = position1[1] - position0[1];
  edge1[2] = position1[2] - position0[2];

  edge2[0] = position2[0] - position0[0];
  edge2[1] = position2[1] - position0[1];
  edge2[2] = position2[2] - position0[2];

  tex_edge1[0] = tex1[0] - tex0[0];
  tex_edge1[1] = tex1[1] - tex0[1];

  tex_edge2[0] = tex2[0] - tex0[0];
  tex_edge2[1] = tex2[1] - tex0[1];

  coef = 1 / (tex_edge1[0] * tex_edge2[1] - tex_edge2[0] * tex_edge1[1]);

  poly_tangent[0] =
    coef * ((edge1[0] * tex_edge2[1]) - (edge2[0] * tex_edge1[1]));
  poly_tangent[1] =
    coef * ((edge1[1] * tex_edge2[1]) - (edge2[1] * tex_edge1[1]));
  poly_tangent[2] =
    coef * ((edge1[2] * tex_edge2[1]) - (edge2[2] * tex_edge1[1]));

  normalize_vertex (poly_tangent);

  tangent0[0] = poly_tangent[0];
  tangent0[1] = poly_tangent[1];
  tangent0[2] = poly_tangent[2];

  tangent1[0] = poly_tangent[0];
  tangent1[1] = poly_tangent[1];
  tangent1[2] = poly_tangent[2];

  tangent2[0] = poly_tangent[0];
  tangent2[1] = poly_tangent[1];
  tangent2[2] = poly_tangent[2];
}

static void
calculate_normals (float *position0,
                   float *position1,
                   float *position2,
                   float *normal0,
                   float *normal1,
                   float *normal2)
{
  float edge1[3];
  float edge2[3];
  float poly_normal[3];

  edge1[0] = position1[0] - position0[0];
  edge1[1] = position1[1] - position0[1];
  edge1[2] = position1[2] - position0[2];

  edge2[0] = position2[0] - position0[0];
  edge2[1] = position2[1] - position0[1];
  edge2[2] = position2[2] - position0[2];

  poly_normal[0] = (edge1[1] * edge2[2]) - (edge1[2] * edge2[1]);
  poly_normal[1] = (edge1[2] * edge2[0]) - (edge1[0] * edge2[2]);
  poly_normal[2] = (edge1[0] * edge2[1]) - (edge1[1] * edge2[0]);

  normalize_vertex (poly_normal);

  normal0[0] = poly_normal[0];
  normal0[1] = poly_normal[1];
  normal0[2] = poly_normal[2];

  normal1[0] = poly_normal[0];
  normal1[1] = poly_normal[1];
  normal1[2] = poly_normal[2];

  normal2[0] = poly_normal[0];
  normal2[1] = poly_normal[1];
  normal2[2] = poly_normal[2];
}

/* Automatically generate all required properties not included in the
 * PLY file */

static CoglBool
generate_missing_properties (void **attribute_data_v0,
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

  RutModel *model = user_data;

  if (!model->builtin_tex_coords)
  {
    calculate_cylindrical_uv_coordinates (model, vert_p0, tex_coord0);
    calculate_cylindrical_uv_coordinates (model, vert_p1, tex_coord1);
    calculate_cylindrical_uv_coordinates (model, vert_p2, tex_coord2);
  }

  if (!model->builtin_normals)
    calculate_normals (vert_p0, vert_p1, vert_p2, vert_n0, vert_n1, vert_n2);

  calculate_tangents (vert_p0, vert_p1, vert_p2, tex_coord0, tex_coord1,
                      tex_coord2, vert_t0, vert_t1, vert_t2);

  return TRUE;
}

static CoglBool
generate_polygons_for_patching (void **attribute_data_v0,
                                void **attribute_data_v1,
                                void **attribute_data_v2,
                                int v0_index,
                                int v1_index,
                                int v2_index,
                                void *user_data)
{
  float *positions[3] = { attribute_data_v0[0], attribute_data_v1[0],
      attribute_data_v2[0] };
  float *normals[3] = { attribute_data_v0[1], attribute_data_v1[1],
      attribute_data_v2[1] };
  float *tangents[3] = { attribute_data_v0[2], attribute_data_v1[2],
      attribute_data_v2[2] };
  float *tex_coords0[3] = { attribute_data_v0[3], attribute_data_v1[3],
      attribute_data_v2[3] };
  int i;
  RutModel *model = user_data;
  Polygon *polygon = &model->priv->polygons[model->priv->n_polygons];

  polygon->id = model->priv->n_polygons;
  polygon->uncovered = TRUE;

  for (i = 0; i < 3; i++)
    {
      polygon->vertices[i] =
        &model->priv->vertices[model->priv->n_vertices + i];
      polygon->vertices[i]->pos[0] = positions[i][0];
      polygon->vertices[i]->pos[1] = positions[i][1];
      polygon->vertices[i]->pos[2] = positions[i][2];

      polygon->vertices[i]->normal[0] = normals[i][0];
      polygon->vertices[i]->normal[1] = normals[i][1];
      polygon->vertices[i]->normal[2] = normals[i][2];

      if (model->builtin_tex_coords)
        {
          polygon->vertices[i]->s0 = tex_coords0[i][0];
          polygon->vertices[i]->t0 = tex_coords0[i][1];
        }
      else
        {
          generate_cylindrical_uv_coordinates (polygon->vertices[i], model);
          tex_coords0[i][0] = polygon->vertices[i]->s0;
          tex_coords0[i][1] = polygon->vertices[i]->t0;
        }
    }

  if (!model->builtin_normals)
    calculate_poly_normal (polygon);
  else
    {
      polygon->normal[0] =
        (polygon->vertices[0]->normal[0] +
         polygon->vertices[1]->normal[0] + polygon->vertices[2]->normal[0]) / 3.0;
      polygon->normal[1] =
        (polygon->vertices[0]->normal[1] +
         polygon->vertices[1]->normal[1] + polygon->vertices[2]->normal[1]) / 3.0;
      polygon->normal[2] =
        (polygon->vertices[0]->normal[2] +
         polygon->vertices[1]->normal[2] + polygon->vertices[2]->normal[2]) / 3.0;
      normalize_vertex (polygon->normal);
    }

  calculate_poly_tangent (polygon);

  for (i = 0; i < 3; i++)
    {
      if (!model->builtin_normals)
        {
          normals[i][0] = polygon->normal[0];
          normals[i][1] = polygon->normal[1];
          normals[i][2] = polygon->normal[2];
        }

      calculate_vertex_tangent (polygon, polygon->vertices[i]);
      tangents[i][0] = polygon->vertices[i]->tx;
      tangents[i][1] = polygon->vertices[i]->ty;
      tangents[i][2] = polygon->vertices[i]->tz;
    }

  model->priv->n_polygons++;
  model->priv->n_vertices += 3;

  return TRUE;
}

static CoglBool
copy_tangents_to_polygons (void **attribute_data_v0,
                           void **attribute_data_v1,
                           void **attribute_data_v2,
                           int v0_index,
                           int v1_index,
                           int v2_index,
                           void *user_data)
{
  RutModel *model = user_data;
  Polygon *polygon = &model->priv->polygons[model->priv->n_polygons];
  float *normals[3] = { attribute_data_v0[0], attribute_data_v1[0],
      attribute_data_v2[0] };
  float *tangents[3] = { attribute_data_v0[1], attribute_data_v1[1],
      attribute_data_v2[1] };
  int i;

  for (i = 0; i < 3; i++)
    {
      polygon->vertices[i]->tx = tangents[i][0];
      polygon->vertices[i]->ty = tangents[i][1];
      polygon->vertices[i]->tz = tangents[i][2];
      if (!model->builtin_normals)
        {
          polygon->vertices[i]->normal[0] = normals[i][0];
          polygon->vertices[i]->normal[1] = normals[i][1];
          polygon->vertices[i]->normal[2] = normals[i][2];
        }
    }

  model->priv->n_polygons++;

  return TRUE;
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

  if (!model->builtin_normals)
    normal[0] = normal[1] = normal[2] = 0;

  tangent[0] = tangent[1] = tangent[2] = 0;

  return TRUE;
}

/* Gets the angle between 2 vectors relative to a rotation axis (usually their
 * cross product). This function takes the "direction" of the angle / rotation
 * into consideration and adjusts the angle accordingly, avoiding clockwise
 * rotation. */

static float
get_angle_between_vectors (float *start_vector,
                           float *end_vector,
                           float *axis)
{
  float cosine;
  float angle;
  float rotated_vertex[3];

  cosine = calculate_dot_product (start_vector, end_vector);
  angle = acos (cosine);

  if (angle == 0.f)
    return 0.f;

  rotated_vertex[0] = start_vector[0];
  rotated_vertex[1] = start_vector[1];
  rotated_vertex[2] = start_vector[2];

  rotate_vertex_around_custom_axis (rotated_vertex, axis, angle);

  if (calculate_dot_product (rotated_vertex, end_vector) > 0.9998)
    return angle;

  /* Otherwise this is a clockwise rotation and the angle needs to be adjusted */

  angle = PI * 2.f - angle;

  return angle;
}

/* Determine whether 2 polygons are connected i.e. whether they share an edge */

static int
check_for_shared_vertices (Polygon *poly1, Polygon *poly2)
{
  Vertex *edges1[3][2];
  Vertex *edges2[3][2];
  int i, j;

  edges1[0][0] = poly1->vertices[0];
  edges1[0][1] = poly1->vertices[1];

  edges1[1][0] = poly1->vertices[1];
  edges1[1][1] = poly1->vertices[2];

  edges1[2][0] = poly1->vertices[2];
  edges1[2][1] = poly1->vertices[0];

  edges2[0][0] = poly2->vertices[0];
  edges2[0][1] = poly2->vertices[1];

  edges2[1][0] = poly2->vertices[1];
  edges2[1][1] = poly2->vertices[2];

  edges2[2][0] = poly2->vertices[2];
  edges2[2][1] = poly2->vertices[0];


  for (i = 0; i < 3; i++)
    {
      for (j = 0; j < 3; j++)
        {
          if (check_vertex_equality (edges1[i][0], edges2[j][0]) &&
              check_vertex_equality (edges1[i][1], edges2[j][1]))
            return 1;
            else if (check_vertex_equality (edges1[i][0], edges2[j][1]) &&
                     check_vertex_equality (edges1[i][1], edges2[j][0]))
            return 1;
        }
    }

  return 0;
}

/* Generate the adjacency matrix */

static void
generate_adjacency_matrix (RutModel *model)
{
  int i, j;
  RutModelPrivate *priv = model->priv;

  _rut_bitmask_clear_all (&priv->adj_matrix);

  for (i = 0; i < priv->n_polygons; i++)
    {
      Polygon *origin = &priv->polygons[i];
      unsigned int adj_offset = priv->n_polygons * origin->id;

      for (j = 0; j < priv->n_polygons; j++)
        {
          Polygon *child = &priv->polygons[j];

          if (origin->id == child->id)
            _rut_bitmask_set (&priv->adj_matrix, adj_offset + child->id, 0);
          else
            _rut_bitmask_set (&priv->adj_matrix, adj_offset + child->id,
                              check_for_shared_vertices (origin, child));
        }
    }
}

/* Finds a polygon which hasn't been covered by a patch yet */

static Polygon*
find_uncovered_polygon (RutModel *model)
{
  int i;

  for (i = 0; i < model->priv->n_polygons; i++)
    {
      Polygon *polygon = &model->priv->polygons[i];
      if (polygon->uncovered)
        return polygon;
    }

  return NULL;
}

/* Places the polygon at the origin of a metaphorical 2D plane */

static void
position_polygon_at_2D_origin (Polygon *polygon)
{
  float centroid[3], axis[3];
  float angle;
  int i;

  /* 1. Calculate polygon centroid */

  calculate_triangle_centroid (centroid,
                               polygon->vertices[0],
                               polygon->vertices[1],
                               polygon->vertices[2]);


  /* 2. Move centroid to origin */

  for (i = 0; i < 3; i++)
    {
      polygon->flat_vertices[i].pos[0] =
        polygon->vertices[i]->pos[0] - centroid[0];
      polygon->flat_vertices[i].pos[1] =
        polygon->vertices[i]->pos[1] - centroid[1];
      polygon->flat_vertices[i].pos[2] =
        polygon->vertices[i]->pos[2] - centroid[2];
    }

  /* 3. Find the angle between the polgyon normal and the 2D plane normal */

  calculate_cross_product (axis, flat_normal, polygon->normal);
  angle = get_angle_between_vectors (polygon->normal, flat_normal, axis);

  if (angle == 0.f)
    return;


  /* 4. Rotate the polygon so that its normal parallelly aligns with the
     2D plane one */

  for (i = 0; i < 3; i++)
    rotate_vertex_around_custom_axis (polygon->flat_vertices[i].pos, axis, angle);
}

/* Map the new polygon by "extruding" the new vertex it indroduces */

static void
extrude_new_vertex (Polygon *parent, Polygon *child)
{
  float distance[3];
  CoglBool found_first_set = FALSE;
  int shared_vertices[2][2];
  int i, j;

  for (i = 0; i < 3; i++)
    {
      for (j = 0; j < 3; j++)
        {
          if (check_vertex_equality (parent->vertices[i], child->vertices[j]))
            {
              if (!found_first_set)
                {
                  shared_vertices[0][0] = i;
                  shared_vertices[0][1] = j;
                  found_first_set = TRUE;
                }
              else
                {
                  shared_vertices[1][0] = i;
                  shared_vertices[1][1] = j;
                  goto FOUND_SHARED_VERTICES;
                }
            }
        }
    }

FOUND_SHARED_VERTICES:

  substract_vertices (distance,
                      &parent->flat_vertices[shared_vertices[0][0]],
                      &child->flat_vertices[shared_vertices[0][1]]);

  for (i = 0; i < 3; i++)
    {
      child->flat_vertices[i].pos[0] += distance[0];
      child->flat_vertices[i].pos[1] += distance[1];
      child->flat_vertices[i].pos[2] += distance[2];
    }
}

static CoglBool
extract_texture_coordinates (TexturePatch* patch, Polygon *polygon)
{
  int i;
  float x_min = -1 * (patch->width / 2.0);
  float y_min = -1 * (patch->height / 2.0);
  float x_max = patch->width / 2.0;
  float y_max = patch->height / 2.0;
  float new_s[3];
  float new_t[3];

  /* 7. Extract the texture coordinates from the flattened polygon using
   * linear interpolation */

  for (i = 0; i < 3; i++)
    {
      new_s[i] = (polygon->flat_vertices[i].pos[0] - x_min) / (x_max - x_min);
      new_t[i] = (polygon->flat_vertices[i].pos[1] - y_min) / (y_max - x_min);
      if (new_s[i] > 1.0 || new_t[i] > 1.0 || new_s[i] < 0.0 || new_t[i] < 0.0)
        return FALSE;
    }

  for (i = 0; i < 3; i++)
    {
      polygon->vertices[i]->s1 = new_s[i];
      polygon->vertices[i]->t1 = new_t[i];
    }

  return TRUE;
}

static void
grow_texture_patch (RutModel *model, TexturePatch *patch)
{
  RutModelPrivate *priv = model->priv;
  CoglBool *visited = g_new (CoglBool, priv->n_polygons);
  GQueue *stack = g_queue_new ();
  int i;

  for (i = 0; i < priv->n_polygons; i++)
    visited[i] = FALSE;

  g_queue_push_tail (stack, patch->root);

  while (!g_queue_is_empty (stack))
    {
      Polygon *parent = g_queue_pop_tail (stack);

      if (visited[parent->id])
        continue;

      visited[parent->id] = TRUE;

      for (i = 0; i < priv->n_polygons; i++)
        {
          Polygon *child = &priv->polygons[i];
          unsigned int adj_offset = priv->n_polygons * parent->id;

          if (_rut_bitmask_get (&priv->adj_matrix, adj_offset + child->id) != 1)
            continue;

          if (!visited[child->id])
            {
              position_polygon_at_2D_origin (child);
              extrude_new_vertex (parent, child);
              if (extract_texture_coordinates (patch, child))
                {
                  patch->polygons = g_list_prepend (patch->polygons, child);
                  g_queue_push_tail (stack, child);
                  child->uncovered = FALSE;
                }
            }
        }
    }

  g_free (visited);
  g_queue_free (stack);
}

TexturePatch*
create_texture_patch (RutModel *model)
{
  TexturePatch *patch = NULL;
  Polygon *root = NULL;

  root = find_uncovered_polygon (model);

  if (!root)
    return NULL;

  patch = g_new (TexturePatch, 1);
  patch->polygons = NULL;
  patch->root = root;

  /* One problem with using a constant fraction for the patch sizing
   * is that if we have low resolution geometry then its possible that
   * many of the polygons are themselves larger than the patch size
   * and so we fail to extract any texture coordinates.
   */
#warning "TODO: Make the patch sizing adaptive if it fails extract texture coordinates"
  patch->width = fabsf (model->max_x - model->min_x) / 5.0;
  patch->height = fabsf (model->max_y - model->min_y) / 5.0;

  position_polygon_at_2D_origin (root);

  extract_texture_coordinates (patch, root);
  patch->root->uncovered = FALSE;

  patch->polygons = g_list_prepend (patch->polygons, root);
  grow_texture_patch (model, patch);

  model->priv->texture_patches =
    g_list_prepend (model->priv->texture_patches, patch);

  return patch;
}

static RutMesh *
create_custom_mesh (Vertex *vertices,
                    int n_vertices,
                    unsigned int *indices,
                    int n_indices)
{
  RutMesh *mesh;
  RutAttribute *attributes[8];
  RutBuffer *vertex_buffer;
  RutBuffer *index_buffer;

  vertex_buffer = rut_buffer_new (sizeof (Vertex) * n_vertices);
  memcpy (vertex_buffer->data, vertices, sizeof (Vertex) * n_vertices);

  if (indices)
    {
      index_buffer = rut_buffer_new (sizeof (unsigned int) * n_indices);
      memcpy (index_buffer->data, indices, sizeof (unsigned int) * n_indices);
    }

  attributes[0] = rut_attribute_new (vertex_buffer,
                                     "cogl_position_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, pos),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[1] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord0_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s0),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

#warning "TODO: audit why we have all of these texture coordinate attributes..."
  attributes[2] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord1_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s0),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[3] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord4_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s0),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[4] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord7_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s0),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[5] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord11_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s1),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[6] = rut_attribute_new (vertex_buffer,
                                     "cogl_normal_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, normal),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[7] = rut_attribute_new (vertex_buffer,
                                     "tangent_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, tx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  mesh = rut_mesh_new (COGL_VERTICES_MODE_TRIANGLES, n_vertices, attributes, 8);

  if (indices)
    {
      rut_mesh_set_indices (mesh, COGL_INDICES_TYPE_UNSIGNED_INT, index_buffer,
                            n_indices);
    }

  return mesh;
}

static RutMesh *
create_patched_mesh_from_model (RutObject *object)
{
  RutModel *model = object;
  GList *iter;

  if (model->patched_mesh)
    return model->patched_mesh;

  generate_adjacency_matrix (model);

  while (create_texture_patch (model));

  for (iter = model->priv->texture_patches; iter; iter = iter->next)
    g_free (iter->data);

  g_list_free (model->priv->texture_patches);

  model->patched_mesh = create_custom_mesh (model->priv->vertices,
                                            model->priv->n_vertices,
                                            NULL, /* no indices */
                                            0);

  return model->patched_mesh;
}

RutMesh *
create_fin_mesh_from_model (RutObject *object)
{
  RutModel *model = object;
  unsigned int *indices;
  int i, j, k;

  if (model->fin_mesh)
    return model->fin_mesh;

  indices = g_new (unsigned int, model->priv->n_fin_polygons * 3);

  j = 0;
  k = 0;

  for (i = 0; i < model->priv->n_fin_polygons; i += 2)
    {
      indices[k] = j;
      indices[k + 1] = j + 1;
      indices[k + 2] = j + 2;
      indices[k + 3] = j + 2;
      indices[k + 4] = j + 3;
      indices[k + 5] = j;

      j += 4;
      k += 6;
    }

  model->fin_mesh = create_custom_mesh (model->priv->fin_vertices,
                                        model->priv->n_fin_vertices,
                                        indices,
                                        model->priv->n_fin_polygons * 3);

  g_free (indices);

  return model->fin_mesh;
}

RutModel *
rut_model_new_from_mesh (RutContext *ctx,
                         RutMesh *mesh,
                         CoglBool needs_normals,
                         CoglBool needs_tex_coords)
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

  model->builtin_normals = !needs_normals;
  model->builtin_tex_coords = !needs_tex_coords;

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
                             generate_missing_properties,
                             model,
                             "cogl_position_in",
                             "cogl_normal_in",
                             "tangent_in",
                             "cogl_tex_coord0_in",
                             NULL);

  return model;
}

RutModel *
rut_model_new_from_asset (RutContext *ctx,
                          RutAsset *asset,
                          CoglBool needs_normals,
                          CoglBool needs_tex_coords)
{
  RutMesh *mesh = rut_asset_get_mesh (asset);
  RutModel *model;

  if (!mesh)
    return NULL;

  model = rut_model_new_from_mesh (ctx, mesh, needs_normals, needs_tex_coords);
  model->asset = rut_refable_ref (asset);

  return model;
}

RutModel *
rut_model_new_for_hair (RutModel *base)
{
  RutModel *model = _rut_model_copy (base);

  int n_vertices;
  int i;

  g_return_val_if_fail (!base->is_hair_model, NULL);

  if (model->primitive)
    {
      cogl_object_unref (model->primitive);
      model->primitive = NULL;
    }

  model->is_hair_model = TRUE;
  model->patched_mesh = NULL;
  model->fin_mesh = NULL;

  model->priv = g_new (RutModelPrivate, 1);

  _rut_bitmask_init (&model->priv->adj_matrix);

  model->priv->texture_patches = NULL;

  n_vertices = model->mesh->indices_buffer ?
    model->mesh->n_indices : model->mesh->n_vertices;

  model->priv->polygons = g_new (Polygon, n_vertices / 3);
  model->priv->vertices = g_new (Vertex, n_vertices);

  model->priv->n_fin_polygons = 0;
  model->priv->n_fin_vertices = 0;
  model->priv->fin_polygons = g_new (Polygon, (n_vertices / 3) * 6);
  model->priv->fin_vertices = g_new (Vertex, n_vertices * 4);

  model->priv->n_vertices = 0;
  model->priv->n_polygons = 0;
  rut_mesh_foreach_triangle (model->mesh,
                             generate_polygons_for_patching,
                             model,
                             "cogl_position_in",
                             "cogl_normal_in",
                             "tangent_in",
                             "cogl_tex_coord0_in",
                             NULL);

  /* TODO: we can fold this into generate_polygons_for_patching */
  model->priv->n_polygons = 0;
  rut_mesh_foreach_triangle (model->mesh,
                             copy_tangents_to_polygons,
                             model,
                             "cogl_normal_in",
                             "tangent_in",
                             NULL);

  model->patched_mesh = create_patched_mesh_from_model (model);

  for (i = 0; i < model->priv->n_polygons; i++)
    add_polygon_fins (model, &model->priv->polygons[i]);

  model->fin_mesh = create_fin_mesh_from_model (model);

  model->fin_primitive = rut_mesh_create_primitive (model->ctx,
                                                    model->fin_mesh);

  rut_refable_unref (model->mesh);
  model->mesh = model->patched_mesh;

  model->default_hair_length = rut_model_get_default_hair_length (model);

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

float
rut_model_get_default_hair_length (RutObject *object)
{
  RutModel *model = object;
  float x_size, y_size, z_size;

  if (model->default_hair_length > 0)
    return model->default_hair_length;

  model->default_hair_length = 0;

  x_size = fabsf (model->max_x - model->min_x) / 5.0;
  y_size = fabsf (model->max_y - model->min_y) / 5.0;
  z_size = fabsf (model->max_z - model->min_z) / 5.0;

  if (x_size < y_size && x_size > 0)
    model->default_hair_length = x_size;

  if (y_size < x_size && y_size > 0)
    model->default_hair_length = y_size;

  if (z_size < model->default_hair_length  && z_size > 0)
    model->default_hair_length = z_size;

  return model->default_hair_length;
}
