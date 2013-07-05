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
    .name = "pointalism-cell-size",
    .nick = "Cell Size",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_pointalism_grid_get_cell_size,
    .setter.float_type = rut_pointalism_grid_set_cell_size,
    .flags = RUT_PROPERTY_FLAG_READWRITE | RUT_PROPERTY_FLAG_VALIDATE,
    .validation = { .float_range = { 1, 100 }},
    .animatable = TRUE
  },
  { NULL }
};

static void
_pointalism_grid_slice_free (void *object)
{
  RutPointalismGridSlice *pointalism_grid_slice = object;

  rut_refable_unref (pointalism_grid_slice->mesh);

  g_slice_free (RutPointalismGridSlice, object);
}

RutType rut_pointalism_grid_slice_type;

void
_rut_pointalism_grid_slice_init_type (void)
{
  static RutRefCountableVTable refable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _pointalism_grid_slice_free
  };

  RutType *type = &rut_pointalism_grid_slice_type;

#define TYPE RutPointalismGridSlice

  rut_type_init (type, G_STRINGIFY (TYPE));
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &refable_vtable);

#undef TYPE
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

static RutMesh *
mesh_new_grid (CoglVerticesMode mode,
               int n_vertices,
               int n_indices,
               GridVertex *vertices,
               unsigned int *indices)
{
  RutMesh *mesh;
  RutAttribute *attributes[9];
  RutBuffer *vertex_buffer;
  RutBuffer *index_buffer;

  vertex_buffer = rut_buffer_new (sizeof (GridVertex) * n_vertices);
  index_buffer = rut_buffer_new (sizeof (unsigned int) * n_indices);

  memcpy (vertex_buffer->data, vertices, sizeof (GridVertex) * n_vertices);
  memcpy (index_buffer->data, indices, sizeof (unsigned int) * n_indices);

  attributes[0] = rut_attribute_new (vertex_buffer,
                                     "cogl_position_in",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, x0),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[1] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord0_in",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, s0),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[2] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord1_in",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, s3),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[3] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord4_in",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, s3),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[4] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord7_in",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, s3),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[5] = rut_attribute_new (vertex_buffer,
                                     "cogl_normal_in",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, nx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[6] = rut_attribute_new (vertex_buffer,
                                     "tangent_in",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, tx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[7] = rut_attribute_new (vertex_buffer,
                                     "cell_xy",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, x1),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[8] = rut_attribute_new (vertex_buffer,
                                     "cell_st",
                                     sizeof (GridVertex),
                                     offsetof (GridVertex, s1),
                                     4,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  mesh = rut_mesh_new (mode, n_vertices, attributes, 9);
  rut_mesh_set_indices (mesh,
                        COGL_INDICES_TYPE_UNSIGNED_INT,
                        index_buffer,
                        n_indices);

  g_free (vertices);
  g_free (indices);

  return mesh;
}

void
pointalism_generate_grid (RutPointalismGridSlice *slice,
                          int tex_width,
                          int tex_height,
                          float size)
{
  int columns = abs (tex_width / size);
  int rows = abs (tex_height / size);
  int i, j, k, l;
  float cell_s = 1.0 / columns;
  float cell_t = 1.0 / rows;
  float start_x = -1.0 * ((size * columns) / 2.0);
  float start_y = -1.0 * ((size * rows) / 2.0);
  GridVertex *vertices = g_new (GridVertex, (columns * rows) * 4);
  unsigned int *indices = g_new (unsigned int, (columns * rows) * 6);

  k = 0;
  l = 0;

  for (i = 0; i < rows; i++)
  {
    for (j = 0; j < columns; j++)
    {
      float x = start_x + (size / 2);
      float y = start_y + (size / 2);

      vertices[k].x0 = -1 * (size / 2);
      vertices[k].y0 = -1 * (size / 2);
      vertices[k].s0 = 0;
      vertices[k].t0 = 0;
      vertices[k].x1 = x;
      vertices[k].y1 = y;
      vertices[k].s1 = j * cell_s;
      vertices[k].t1 = i * cell_t;
      vertices[k].s2 = (j + 1) * cell_s;
      vertices[k].t2 = (i + 1) * cell_t;
      vertices[k].s3 = j * cell_s;
      vertices[k].t3 = i * cell_t;
      vertices[k].nx = 0;
      vertices[k].ny = 0;
      vertices[k].nz = 1;
      vertices[k].tx = 1;
      vertices[k].ty = 0;
      vertices[k].tz = 0;
      k++;

      vertices[k].x0 = size / 2;
      vertices[k].y0 = -1 * (size / 2);
      vertices[k].s0 = 1;
      vertices[k].t0 = 0;
      vertices[k].x1 = x;
      vertices[k].y1 = y;
      vertices[k].s1 = j * cell_s;
      vertices[k].t1 = i * cell_t;
      vertices[k].s2 = (j + 1) * cell_s;
      vertices[k].t2 = (i + 1) * cell_t;
      vertices[k].s3 = (j + 1) * cell_s;
      vertices[k].t3 = i * cell_t;
      vertices[k].nx = 0;
      vertices[k].ny = 0;
      vertices[k].nz = 1;
      vertices[k].tx = 1;
      vertices[k].ty = 0;
      vertices[k].tz = 0;
      k++;

      vertices[k].x0 = size / 2;
      vertices[k].y0 = size / 2;
      vertices[k].s0 = 1;
      vertices[k].t0 = 1;
      vertices[k].x1 = x;
      vertices[k].y1 = y;
      vertices[k].s1 = j * cell_s;
      vertices[k].t1 = i * cell_t;
      vertices[k].s2 = (j + 1) * cell_s;
      vertices[k].t2 = (i + 1) * cell_t;
      vertices[k].s3 = (j + 1) * cell_s;
      vertices[k].t3 = (i + 1) * cell_t;
      vertices[k].nx = 0;
      vertices[k].ny = 0;
      vertices[k].nz = 1;
      vertices[k].tx = 1;
      vertices[k].ty = 0;
      vertices[k].tz = 0;
      k++;

      vertices[k].x0 = -1 * (size / 2);
      vertices[k].y0 = size / 2;
      vertices[k].s0 = 0;
      vertices[k].t0 = 1;
      vertices[k].x1 = x;
      vertices[k].y1 = y;
      vertices[k].s1 = j * cell_s;
      vertices[k].t1 = i * cell_t;
      vertices[k].s2 = (j + 1) * cell_s;
      vertices[k].t2 = (i + 1) * cell_t;
      vertices[k].s3 = j * cell_s;
      vertices[k].t3 = (i + 1) * cell_t;
      vertices[k].nx = 0;
      vertices[k].ny = 0;
      vertices[k].nz = 1;
      vertices[k].tx = 1;
      vertices[k].ty = 0;
      vertices[k].tz = 0;
      k++;

      indices[l] = k;
      indices[l + 1] = k + 1;
      indices[l + 2] = k + 2;
      indices[l + 3] = k + 2;
      indices[l + 4] = k + 3;
      indices[l + 5] = k;

      l += 6;
      start_x += size;
    }
    start_x = -1.0 * ((size * columns) / 2.0);
    start_y += size;
  }

  if (slice->mesh)
    rut_refable_unref (slice->mesh);

  slice->mesh = mesh_new_grid (COGL_VERTICES_MODE_TRIANGLES,
                               (columns * rows) * 4,
                               (columns * rows) * 6,
                               vertices, indices);
}

static RutPointalismGridSlice *
pointalism_grid_slice_new (int tex_width,
                           int tex_height,
                           float size)
{
  RutPointalismGridSlice *grid_slice = g_slice_new (RutPointalismGridSlice);

  rut_object_init (&grid_slice->_parent, &rut_pointalism_grid_slice_type);

  grid_slice->ref_count = 1;
  grid_slice->mesh = NULL;

  pointalism_generate_grid (grid_slice, tex_width, tex_height, size);

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

void
_rut_pointalism_grid_init_type (void)
{
  static RutRefCountableVTable refable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _rut_pointalism_grid_free
  };

  static RutComponentableVTable componentable_vtable = {
    NULL
  };

  static RutPrimableVTable primable_vtable = {
    .get_primitive = rut_pointalism_grid_get_primitive
  };

  static RutPickableVTable pickable_vtable = {
    .get_mesh = rut_pointalism_grid_get_pick_mesh
  };

  static RutIntrospectableVTable introspectable_vtable = {
    rut_simple_introspectable_lookup_property,
    rut_simple_introspectable_foreach_property
  };

  RutType *type = &rut_pointalism_grid_type;

#define TYPE RutPointalismGrid

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

  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0,
                          &introspectable_vtable);

  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (TYPE, introspectable),
                          NULL);

#undef TYPE
}

RutPointalismGrid *
rut_pointalism_grid_new (RutContext *ctx,
                         float size,
                         int tex_width,
                         int tex_height)
{
  RutPointalismGrid *grid = g_slice_new0 (RutPointalismGrid);
  RutBuffer *buffer = rut_buffer_new (sizeof (CoglVertexP3) * 6);
  RutMesh *pick_mesh = rut_mesh_new_from_buffer_p3 (COGL_VERTICES_MODE_TRIANGLES,
                                                    6,
                                                    buffer);
  CoglVertexP3 *pick_vertices = (CoglVertexP3 *)buffer->data;
  float half_tex_width;
  float half_tex_height;

  rut_object_init (&grid->_parent, &rut_pointalism_grid_type);

  grid->ref_count = 1;

  grid->component.type = RUT_COMPONENT_TYPE_GEOMETRY;

  grid->ctx = rut_refable_ref (ctx);


  grid->slice = pointalism_grid_slice_new (tex_width, tex_height,
                                           size);

  half_tex_width = tex_width / 2.0f;
  half_tex_height = tex_height / 2.0f;

  pick_vertices[0].x = -half_tex_width;
  pick_vertices[0].y = -half_tex_height;
  pick_vertices[1].x = -half_tex_width;
  pick_vertices[1].y = half_tex_height;
  pick_vertices[2].x = half_tex_width;
  pick_vertices[2].y = half_tex_height;
  pick_vertices[3] = pick_vertices[0];
  pick_vertices[4] = pick_vertices[2];
  pick_vertices[5].x = half_tex_width;
  pick_vertices[5].y = -half_tex_height;

  grid->pick_mesh = pick_mesh;
  grid->pointalism_scale = 1;
  grid->pointalism_z = 1;
  grid->pointalism_lighter = TRUE;
  grid->cell_size = size;
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
  CoglPrimitive *primitive = rut_mesh_create_primitive (grid->ctx,
                                                        grid->slice->mesh);
  return primitive;
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
rut_pointalism_grid_get_cell_size (RutObject *obj)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);

  return grid->cell_size;
}

void
rut_pointalism_grid_set_cell_size (RutObject *obj,
                                   float cell_size)
{
  RutPointalismGrid *grid = RUT_POINTALISM_GRID (obj);
  RutEntity *entity;
  RutContext *ctx;

  if (cell_size == grid->cell_size)
    return;

  grid->cell_size = cell_size;

  entity = grid->component.entity;
  ctx = rut_entity_get_context (entity);
  rut_property_dirty (&ctx->property_ctx,
                      &grid->properties[RUT_POINTALISM_GRID_PROP_CELL_SIZE]);

  pointalism_generate_grid (grid->slice, grid->tex_width,
                            grid->tex_height, grid->cell_size);
}
