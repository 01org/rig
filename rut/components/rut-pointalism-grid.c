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

#include "rut-pointalism-grid.h"
#include "rut-global.h"
#include "math.h"

#define MESA_CONST_ATTRIB_BUG_WORKAROUND


static RutPropertySpec _rut_pointalism_grid_prop_specs[] = {
  {
    .name = "pointalism-scale",
    .nick = "Pointalism Scale Factor",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_pointalism_grid_get_scale,
    .setter.float_type = rut_pointalism_grid_set_scale,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
    .animatable = TRUE
  },
  {
    .name = "pointalism-z",
    .nick = "Pointalism Z Factor",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_pointalism_grid_get_z,
    .setter.float_type = rut_pointalism_grid_set_z,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
    .animatable = TRUE
  },
  {
    .name = "pointalism-lighter",
    .nick = "Pointalism Lighter",
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .getter.boolean_type = rut_pointalism_grid_get_lighter,
    .setter.boolean_type = rut_pointalism_grid_set_lighter,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .animatable = TRUE
  },
  {
    .name = "pointalism-columns",
    .nick = "Grid Columns",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_pointalism_grid_get_columns,
    .setter.float_type = rut_pointalism_grid_set_columns,
    .flags = RUT_PROPERTY_FLAG_READWRITE | RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
    .animatable = TRUE
  },
  {
    .name = "pointalism-rows",
    .nick = "Grid Rows",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_pointalism_grid_get_rows,
    .setter.float_type = rut_pointalism_grid_set_rows,
    .flags = RUT_PROPERTY_FLAG_READWRITE | RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
    .animatable = TRUE
  },
  { NULL }
};

static void
_pointalism_grid_slice_free (void *object)
{
  RutPointalismGridSlice *pointalism_grid_slice = object;

  cogl_object_unref (pointalism_grid_slice->primitive);

  g_slice_free (RutPointalismGridSlice, object);
}

static RutRefCountableVTable _pointalism_grid_slice_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _pointalism_grid_slice_free
};

RutType rut_pointalism_grid_slice_type;

void
_rut_pointalism_grid_slice_init_type (void)
{
  rut_type_init (&rut_pointalism_grid_slice_type, "RigPointalismGridSlice");
  rut_type_add_interface (&rut_pointalism_grid_slice_type,
                           RUT_INTERFACE_ID_REF_COUNTABLE,
                           offsetof (RutPointalismGridSlice, ref_count),
                           &_pointalism_grid_slice_ref_countable_vtable);
}

typedef struct _GridVertex
{
  float x0, y0;
  float x1, y1;
  float s0, t0;
  float s1, s2, t1, t2;
  float s3, t3;
#ifdef MESA_CONST_ATTRIB_BUG_WORKAROUND
  float nx, ny, nz;
  float tx, ty, tz;
#endif
} GridVertex;

static CoglPrimitive *
primitive_new_grid (CoglContext *ctx,
                    CoglVerticesMode mode,
                    int n_vertices,
                    const GridVertex *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx, n_vertices * sizeof (GridVertex), data);
  int n_attributes = 9;
  CoglAttribute *attributes[n_attributes];
  CoglPrimitive *primitive;
#ifndef MESA_CONST_ATTRIB_BUG_WORKAROUND
  const float normal[3] = { 0, 0, 1 };
  const float tangent[3] = { 1, 0, 0 };
#endif
  int i;

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, x0),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  attributes[1] = cogl_attribute_new (attribute_buffer,
                                      "cogl_tex_coord0_in",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, s0),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  attributes[2] = cogl_attribute_new (attribute_buffer,
                                      "cogl_tex_coord1_in",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, s3),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  attributes[3] = cogl_attribute_new (attribute_buffer,
                                      "cogl_tex_coord2_in",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, s3),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  attributes[4] = cogl_attribute_new (attribute_buffer,
                                      "cogl_tex_coord5_in",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, s3),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

#ifdef MESA_CONST_ATTRIB_BUG_WORKAROUND
  attributes[5] = cogl_attribute_new (attribute_buffer,
                                      "cogl_normal_in",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, nx),
                                      3,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[6] = cogl_attribute_new (attribute_buffer,
                                      "tangent_in",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, tx),
                                      3,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
#else
  attributes[5] = cogl_attribute_new_const_3fv (ctx,
                                                "cogl_normal_in",
                                                normal);

  attributes[6] = cogl_attribute_new_const_3fv (ctx,
                                                "tangent_in",
                                                tangent);
#endif

  attributes[7] = cogl_attribute_new (attribute_buffer,
                                      "cell_xy",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, x1),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  attributes[8] = cogl_attribute_new (attribute_buffer,
                                      "cell_st",
                                      sizeof (GridVertex),
                                      offsetof (GridVertex, s1),
                                      4,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  cogl_object_unref (attribute_buffer);

  primitive = cogl_primitive_new_with_attributes (mode,
                                                  n_vertices,
                                                  attributes,
                                                  n_attributes);

  for (i = 0; i < n_attributes; i++)
    cogl_object_unref (attributes[i]);

  return primitive;
}

void
pointalism_generate_grid (RutPointalismGridSlice *slice,
                          RutContext *ctx,
                          int tex_width,
                          int tex_height,
                          float columns,
                          float rows)
{
  float rem_x, rem_y;
  float size_x, size_y;
  float s_iter, t_iter;
  float start_x, start_y;
  float clipper_x, clipper_y;
  float x, y;
  int n_vertices, i, col_counter, row_counter;
  GridVertex *vertices;
  CoglPrimitive *prim = slice->primitive;

  rem_x = abs (columns + 1) - columns;
  rem_y = abs (rows + 1) - rows;

  if (rem_x > 0.99)
    rem_x = 0;

  if (rem_y > 0.99)
    rem_y = 0;

  size_x = (float) tex_width / abs (columns + rem_x);
  size_y = (float) tex_height / abs (rows + rem_y);

  s_iter = 1.f / abs (columns + rem_x);
  t_iter = 1.f / abs (rows + rem_y);

  start_x = -1.f * ((size_x * abs (columns + rem_x)) / 2);
  start_y = -1.f * ((size_y * abs (rows + rem_y)) / 2);

  n_vertices = (abs (columns + rem_x) * abs (rows + rem_y)) * 4;
  i = 0;

  vertices = g_new (GridVertex, n_vertices);

  for (row_counter = 0; row_counter < abs (rows + rem_y); row_counter++)
    {
      clipper_y = 0;
      if (row_counter == abs ((rows + rem_y) - 1) && rem_y > 0)
        clipper_y = rem_y;

      for (col_counter = 0; col_counter < abs (columns + rem_x); col_counter++)
        {
          clipper_x = 0;
          if (col_counter == abs ((columns + rem_x) - 1) && rem_x > 0)
            clipper_x = rem_x;

          x = start_x + (size_x / 2);
          y = start_y + (size_y / 2);

          /* Bottom Left */
          vertices[i].x0 = -1 * (size_x / 2);
          vertices[i].y0 = -1 * (size_y  / 2);
          vertices[i].s0 = 0;
          vertices[i].t0 = 0;
          vertices[i].x1 = x;
          vertices[i].y1 = y;
          vertices[i].s1 = col_counter * s_iter;
          vertices[i].t1 = row_counter * t_iter;
          vertices[i].s2 = ((col_counter + 1) * s_iter) - (s_iter * clipper_x);
          vertices[i].t2 = ((row_counter + 1) * t_iter) - (t_iter * clipper_y);
          vertices[i].s3 = col_counter * s_iter;
          vertices[i].t3 = row_counter * t_iter;
          #ifdef MESA_CONST_ATTRIB_BUG_WORKAROUND
          vertices[i].nx = 0;
          vertices[i].ny = 0;
          vertices[i].nz = 1;

          vertices[i].tx = 1;
          vertices[i].ty = 0;
          vertices[i].tz = 0;
          #endif
          i++;

          /* Bottom Right */
          vertices[i].x0 = (size_x / 2) - (size_x * clipper_x);
          vertices[i].y0 = -1 * (size_y / 2);
          vertices[i].s0 = 1 - clipper_x;
          vertices[i].t0 = 0;
          vertices[i].x1 = x;
          vertices[i].y1 = y;
          vertices[i].s1 = col_counter * s_iter;
          vertices[i].t1 = row_counter * t_iter;
          vertices[i].s2 = ((col_counter + 1) * s_iter) - (s_iter * clipper_x);
          vertices[i].t2 = ((row_counter + 1) * t_iter) - (t_iter * clipper_y);
          vertices[i].s3 = ((col_counter + 1) * s_iter) - (s_iter * clipper_x);
          vertices[i].t3 = row_counter * t_iter;
          #ifdef MESA_CONST_ATTRIB_BUG_WORKAROUND
          vertices[i].nx = 0;
          vertices[i].ny = 0;
          vertices[i].nz = 1;

          vertices[i].tx = 1;
          vertices[i].ty = 0;
          vertices[i].tz = 0;
          #endif
          i++;

          /* Top Right */
          vertices[i].x0 = (size_x / 2) - (size_x * clipper_x);
          vertices[i].y0 = (size_y / 2) - (size_y * clipper_y);
          vertices[i].s0 = 1 - clipper_x;
          vertices[i].t0 = 1 - clipper_y;
          vertices[i].x1 = x;
          vertices[i].y1 = y;
          vertices[i].s1 = col_counter * s_iter;
          vertices[i].t1 = row_counter * t_iter;
          vertices[i].s2 = ((col_counter + 1) * s_iter) - (s_iter * clipper_x);
          vertices[i].t2 = ((row_counter + 1) * t_iter) - (t_iter * clipper_y);
          vertices[i].s3 = ((col_counter + 1) * s_iter) - (s_iter * clipper_x);
          vertices[i].t3 = ((row_counter + 1) * t_iter) - (t_iter * clipper_y);
          #ifdef MESA_CONST_ATTRIB_BUG_WORKAROUND
          vertices[i].nx = 0;
          vertices[i].ny = 0;
          vertices[i].nz = 1;

          vertices[i].tx = 1;
          vertices[i].ty = 0;
          vertices[i].tz = 0;
          #endif
          i++;

          /* Top Left */
          vertices[i].x0 = -1 * (size_x / 2);
          vertices[i].y0 = (size_y / 2) - (size_y * clipper_y);
          vertices[i].s0 = 0;
          vertices[i].t0 = 1 - clipper_y;
          vertices[i].x1 = x;
          vertices[i].y1 = y;
          vertices[i].s1 = col_counter * s_iter;
          vertices[i].t1 = row_counter * t_iter;
          vertices[i].s2 = ((col_counter + 1) * s_iter) - (s_iter * clipper_x);
          vertices[i].t2 = ((row_counter + 1) * t_iter) - (t_iter * clipper_y);
          vertices[i].s3 = col_counter * s_iter;
          vertices[i].t3 = ((row_counter + 1) * t_iter) - (t_iter * clipper_y);
          #ifdef MESA_CONST_ATTRIB_BUG_WORKAROUND
          vertices[i].nx = 0;
          vertices[i].ny = 0;
          vertices[i].nz = 1;

          vertices[i].tx = 1;
          vertices[i].ty = 0;
          vertices[i].tz = 0;
          #endif
          i++;

          start_x += size_x;
        }

      start_x = -1 * ((size_x * abs (columns + rem_x)) / 2);
      start_y += size_y;
    }

  slice->primitive = primitive_new_grid (ctx->cogl_context,
                                         COGL_VERTICES_MODE_TRIANGLES,
                                         n_vertices, vertices);

  if (prim)
    cogl_object_unref (prim);

  slice->indices = cogl_get_rectangle_indices (ctx->cogl_context,
                                              (abs (columns + rem_x) *
                                               abs (rows + rem_y)) * 6);

  cogl_primitive_set_indices (slice->primitive, slice->indices,
                             (abs (columns + rem_x) * abs (rows + rem_y)) * 6);
}

static RutPointalismGridSlice *
pointalism_grid_slice_new (RutContext *ctx,
                           int tex_width,
                           int tex_height,
                           float columns,
                           float rows)
{
  RutPointalismGridSlice *grid_slice = g_slice_new (RutPointalismGridSlice);

  rut_object_init (&grid_slice->_parent, &rut_pointalism_grid_slice_type);

  grid_slice->ref_count = 1;
  grid_slice->primitive = NULL;
  grid_slice->indices = NULL;

  pointalism_generate_grid (grid_slice, ctx, tex_width, tex_height, columns,
                            rows);

  return grid_slice;
}


RutType rut_pointalism_grid_type;

static void
_rut_pointalism_grid_free (void *object)
{
  RutPointalismGrid *grid = object;

  rut_refable_unref (grid->slice);
  rut_refable_unref (grid->pick_mesh);

  rut_simple_introspectable_destroy (grid);

  g_slice_free (RutPointalismGrid, grid);
}

static RutRefCountableVTable _rut_pointalism_grid_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_pointalism_grid_free
};

static RutComponentableVTable _rut_pointalism_grid_componentable_vtable = {
    0
};

static RutPrimableVTable _rut_pointalism_grid_primable_vtable = {
  .get_primitive = rut_pointalism_grid_get_primitive
};

static RutPickableVTable _rut_pointalism_grid_pickable_vtable = {
  .get_mesh = rut_pointalism_grid_get_pick_mesh
};

static RutIntrospectableVTable _rut_pointalism_grid_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};

void
_rut_pointalism_grid_init_type (void)
{
  rut_type_init (&rut_pointalism_grid_type, "RigPointalismGrid");
  rut_type_add_interface (&rut_pointalism_grid_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutPointalismGrid, ref_count),
                          &_rut_pointalism_grid_ref_countable_vtable);
  rut_type_add_interface (&rut_pointalism_grid_type,
                          RUT_INTERFACE_ID_COMPONENTABLE,
                          offsetof (RutPointalismGrid, component),
                          &_rut_pointalism_grid_componentable_vtable);
  rut_type_add_interface (&rut_pointalism_grid_type,
                          RUT_INTERFACE_ID_PRIMABLE,
                          0, /* no associated properties */
                          &_rut_pointalism_grid_primable_vtable);
  rut_type_add_interface (&rut_pointalism_grid_type,
                          RUT_INTERFACE_ID_PICKABLE,
                          0, /* no associated properties */
                          &_rut_pointalism_grid_pickable_vtable);

  rut_type_add_interface (&rut_pointalism_grid_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0,
                          &_rut_pointalism_grid_introspectable_vtable);

  rut_type_add_interface (&rut_pointalism_grid_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RutPointalismGrid, introspectable),
                          NULL);
}

RutPointalismGrid *
rut_pointalism_grid_new (RutContext *ctx,
                         float size,
                         int tex_width,
                         int tex_height,
                         float columns,
                         float rows)
{
  RutPointalismGrid *grid = g_slice_new0 (RutPointalismGrid);
  RutBuffer *buffer = rut_buffer_new (sizeof (CoglVertexP3) * 6);
  RutMesh *pick_mesh = rut_mesh_new_from_buffer_p3 (COGL_VERTICES_MODE_TRIANGLES,
                                                    6,
                                                    buffer);
  CoglVertexP3 *pick_vertices = (CoglVertexP3 *)buffer->data;

  rut_object_init (&grid->_parent, &rut_pointalism_grid_type);

  grid->ref_count = 1;

  grid->component.type = RUT_COMPONENT_TYPE_GEOMETRY;

  grid->ctx = rut_refable_ref (ctx);


  grid->slice = pointalism_grid_slice_new (ctx, tex_width, tex_height,
                                           columns, rows);

  pick_vertices[0].x = 0;
  pick_vertices[0].y = 0;
  pick_vertices[1].x = 0;
  pick_vertices[1].y = size;
  pick_vertices[2].x = size;
  pick_vertices[2].y = size;
  pick_vertices[3] = pick_vertices[0];
  pick_vertices[4] = pick_vertices[2];
  pick_vertices[5].x = size;
  pick_vertices[5].y = 0;

  grid->pick_mesh = pick_mesh;
  grid->pointalism_scale = 1;
  grid->pointalism_z = 1;
  grid->pointalism_lighter = TRUE;
  grid->cols = columns;
  grid->rows = rows;
  grid->tex_width = tex_width;
  grid->tex_height = tex_height;

  rut_simple_introspectable_init (grid, _rut_pointalism_grid_prop_specs,
                                  grid->properties);

  return grid;
}

CoglPrimitive *
rut_pointalism_grid_get_primitive (RutObject *object)
{
  RutPointalismGrid *grid = object;
  return grid->slice->primitive;
}

RutMesh *
rut_pointalism_grid_get_pick_mesh (RutObject *self)
{
  RutPointalismGrid *grid = self;
  return grid->pick_mesh;
}

float
rut_pointalism_grid_get_scale (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->pointalism_scale;
}

void
rut_pointalism_grid_set_scale (RutObject *obj,
                               float scale)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (scale == grid->pointalism_scale)
    return;

  grid->pointalism_scale = scale;

  entity = grid->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                     &grid->properties[RUT_POINTALISM_GRID_PROP_SCALE]);
}

float
rut_pointalism_grid_get_z (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->pointalism_z;
}



void
rut_pointalism_grid_set_z (RutObject *obj,
                           float z)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (z == grid->pointalism_z)
    return;

  grid->pointalism_z = z;

  entity = grid->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                     &grid->properties[RUT_POINTALISM_GRID_PROP_Z]);
}

CoglBool
rut_pointalism_grid_get_lighter (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->pointalism_lighter;
}

void
rut_pointalism_grid_set_lighter (RutObject *obj,
                                 CoglBool lighter)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (lighter == grid->pointalism_lighter)
    return;

  grid->pointalism_lighter = lighter;

  entity = grid->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                 &grid->properties[RUT_POINTALISM_GRID_PROP_LIGHTER]);
}

float
rut_pointalism_grid_get_columns (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->cols;
}

void
rut_pointalism_grid_set_columns (RutObject *obj,
                                 float cols)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (cols == grid->cols)
    return;

  grid->cols = cols;

  entity = grid->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                      &grid->properties[RUT_POINTALISM_GRID_PROP_COLUMNS]);

  pointalism_generate_grid (grid->slice, grid->ctx, grid->tex_width,
                            grid->tex_height, grid->cols, grid->rows);
}

float
rut_pointalism_grid_get_rows (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->rows;
}

void
rut_pointalism_grid_set_rows (RutObject *obj,
                              float rows)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (rows == grid->rows)
    return;

  grid->rows = rows;

  entity = grid->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                      &grid->properties[RUT_POINTALISM_GRID_PROP_ROWS]);

  pointalism_generate_grid (grid->slice, grid->ctx, grid->tex_width,
                            grid->tex_height, grid->cols, grid->rows);
}
