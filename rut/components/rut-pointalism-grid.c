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
    .getter.float_type = rut_pontalism_grid_get_pointalism_scale,
    .setter.float_type = rut_pontalism_grid_set_pointalism_scale,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
  },
  {
    .name = "pointalism-z",
    .nick = "Pointalism Z Factor",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_pontalism_grid_get_pointalism_z,
    .setter.float_type = rut_pontalism_grid_set_pointalism_z,
    .flags = RUT_PROPERTY_FLAG_READWRITE |
      RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 0, 100 }},
  },
  {
    .name = "pointalism-lighter",
    .nick = "Pointalism Lighter",
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .getter.boolean_type = rut_pontalism_grid_get_pointalism_lighter,
    .setter.boolean_type = rut_pontalism_grid_set_pointalism_lighter,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
  },
  { NULL }
};

static void
_pointalism_grid_slice_free (void *object)
{
  RutPointalismGridSlice *pointalism_grid_slice = object;

  cogl_object_unref (pointalism_grid_slice->primitive);
  cogl_object_unref (pointalism_grid_slice->indices);

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

static RutPointalismGridSlice *
pointalism_grid_slice_new (RutContext *ctx,
                           int tex_width,
                           int tex_height,
                           int columns,
                           int rows)
{
  float size_x = tex_width / columns;
  float size_y = tex_height / rows;
  float s_iter = 1.0 / columns;
  float t_iter = 1.0 / rows;
  float start_x = -1 * ((size_x * columns) / 2);
  float start_y = -1 * ((size_y * rows) / 2);
  int i = 0;
  int col_counter;
  int row_counter;
  int n_vertices = (columns * rows) * 4;
  GridVertex *vertices = g_new (GridVertex, n_vertices);


  RutPointalismGridSlice *grid_slice = g_slice_new (RutPointalismGridSlice);

  rut_object_init (&grid_slice->_parent, &rut_pointalism_grid_slice_type);

  grid_slice->ref_count = 1;

  for (row_counter = 0; row_counter < rows; row_counter++)
    {
      for (col_counter = 0; col_counter < columns; col_counter++)
        {
          float x, y;

          x = start_x + (size_x / 2);
          y = start_y + (size_y / 2);

          vertices[i].x0 = (-1 * (size_x / 2));
          vertices[i].y0 = (-1 * (size_y / 2));
          vertices[i].s0 = 0;
          vertices[i].t0 = 0;
          vertices[i].x1 = x;
          vertices[i].y1 = y;
          vertices[i].s1 = col_counter * s_iter;
          vertices[i].t1 = row_counter * t_iter;
          vertices[i].s2 = (col_counter + 1) * s_iter;
          vertices[i].t2 = (row_counter + 1) * t_iter;
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

          vertices[i].x0 = (size_x / 2);
          vertices[i].y0 = (-1 * (size_y / 2));
          vertices[i].s0 = 1;
          vertices[i].t0 = 0;
          vertices[i].x1 = x;
          vertices[i].y1 = y;
          vertices[i].s1 = col_counter * s_iter;
          vertices[i].t1 = row_counter * t_iter;
          vertices[i].s2 = (col_counter + 1) * s_iter;
          vertices[i].t2 = (row_counter + 1) * t_iter;
          vertices[i].s3 = (col_counter + 1) * s_iter;
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

          vertices[i].x0 = (size_x / 2);
          vertices[i].y0 = (size_y / 2);
          vertices[i].s0 = 1;
          vertices[i].t0 = 1;
          vertices[i].x1 = x;
          vertices[i].y1 = y;
          vertices[i].s1 = col_counter * s_iter;
          vertices[i].t1 = row_counter * t_iter;
          vertices[i].s2 = (col_counter + 1) * s_iter;
          vertices[i].t2 = (row_counter + 1) * t_iter;
          vertices[i].s3 = (col_counter + 1) * s_iter;
          vertices[i].t3 = (row_counter + 1) * t_iter;
          #ifdef MESA_CONST_ATTRIB_BUG_WORKAROUND
          vertices[i].nx = 0;
          vertices[i].ny = 0;
          vertices[i].nz = 1;

          vertices[i].tx = 1;
          vertices[i].ty = 0;
          vertices[i].tz = 0;
          #endif
          i++;

          vertices[i].x0 = (-1 * (size_x / 2));
          vertices[i].y0 = (size_y / 2);
          vertices[i].s0 = 0;
          vertices[i].t0 = 1;
          vertices[i].x1 = x;
          vertices[i].y1 = y;
          vertices[i].s1 = col_counter * s_iter;
          vertices[i].t1 = row_counter * t_iter;
          vertices[i].s2 = (col_counter + 1) * s_iter;
          vertices[i].t2 = (row_counter + 1) * t_iter;
          vertices[i].s3 = col_counter * s_iter;
          vertices[i].t3 = (row_counter + 1) * t_iter;
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

      start_x = -1 * ((size_x * columns) / 2);
      start_y += size_y;
    }


  grid_slice->primitive = primitive_new_grid (ctx->cogl_context,
                                              COGL_VERTICES_MODE_TRIANGLES,
                                              n_vertices, vertices);

  grid_slice->indices = cogl_get_rectangle_indices (ctx->cogl_context,
                                                   (columns * rows) * 6);

  cogl_primitive_set_indices (grid_slice->primitive, grid_slice->indices,
                             (columns * rows) * 6);

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
                         int columns,
                         int rows)
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

  /* XXX: It could be worth maintaining a cache of grid slices
   * indexed by the <size, tex_width, tex_height> tuple... */
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
rut_pontalism_grid_get_pointalism_scale (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->pointalism_scale;
}

void
rut_pontalism_grid_set_pointalism_scale (RutObject *obj,
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
rut_pontalism_grid_get_pointalism_z (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->pointalism_z;
}



void
rut_pontalism_grid_set_pointalism_z (RutObject *obj,
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
rut_pontalism_grid_get_pointalism_lighter (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->pointalism_lighter;
}

void
rut_pontalism_grid_set_pointalism_lighter (RutObject *obj,
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
