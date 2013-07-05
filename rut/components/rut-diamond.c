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

#include "rut-diamond.h"
#include "rut-global.h"
#include "math.h"

#define MESA_CONST_ATTRIB_BUG_WORKAROUND

static void
_diamond_slice_free (void *object)
{
  RutDiamondSlice *diamond_slice = object;

  rut_refable_unref (diamond_slice->mesh);

  g_slice_free (RutDiamondSlice, object);
}

RutType rut_diamond_slice_type;

void
_rut_diamond_slice_init_type (void)
{
  static RutRefCountableVTable ref_countable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _diamond_slice_free
  };

  RutType *type = &rut_diamond_slice_type;

#define TYPE RutDiamondSlice

  rut_type_init (type, "RigDiamondSlice");
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &ref_countable_vtable);

#undef TYPE
}

typedef struct _VertexP2T2T2
{
  float x, y, s0, t0, s1, t1;
  float Nx, Ny, Nz;
  float Tx, Ty, Tz;
} VertexP2T2T2;

static RutMesh *
mesh_new_p2t2t2 (CoglVerticesMode mode,
                 int n_vertices,
                 VertexP2T2T2 *vertices)
{
  RutMesh *mesh;
  RutAttribute *attributes[7];
  RutBuffer *vertex_buffer;
  RutBuffer *index_buffer;

  vertex_buffer = rut_buffer_new (sizeof (VertexP2T2T2) * n_vertices);
  memcpy (vertex_buffer->data, vertices, sizeof (VertexP2T2T2) * n_vertices);
  index_buffer = rut_buffer_new (sizeof (_rut_nine_slice_indices_data));
  memcpy (index_buffer->data, _rut_nine_slice_indices_data,
          sizeof (_rut_nine_slice_indices_data));

  attributes[0] = rut_attribute_new (vertex_buffer,
                                     "cogl_position_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, x),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[1] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord0_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, s0),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[2] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord1_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, s1),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[3] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord4_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, s1),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[4] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord7_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, s1),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);


  attributes[5] = rut_attribute_new (vertex_buffer,
                                     "cogl_normal_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, Nx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[6] = rut_attribute_new (vertex_buffer,
                                     "tangent_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, Tx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  mesh = rut_mesh_new (mode, n_vertices, attributes, 7);
  rut_mesh_set_indices (mesh,
                        COGL_INDICES_TYPE_UNSIGNED_BYTE,
                        index_buffer,
                        sizeof (_rut_nine_slice_indices_data) /
                        sizeof (_rut_nine_slice_indices_data[0]));

  return mesh;
}

static RutDiamondSlice *
diamond_slice_new (float size,
                   int tex_width,
                   int tex_height)
{
  RutDiamondSlice *diamond_slice = g_slice_new (RutDiamondSlice);
  float width = size;
  float height = size;
#define DIAMOND_SLICE_CORNER_RADIUS 20
  CoglMatrix matrix;
  float tex_aspect;

  rut_object_init (&diamond_slice->_parent, &rut_diamond_slice_type);

  diamond_slice->ref_count = 1;

  diamond_slice->size = size;

    {
      /* x0,y0,x1,y1 and s0,t0,s1,t1 define the postion and texture
       * coordinates for the center rectangle... */
      float x0 = DIAMOND_SLICE_CORNER_RADIUS;
      float y0 = DIAMOND_SLICE_CORNER_RADIUS;
      float x1 = width - DIAMOND_SLICE_CORNER_RADIUS;
      float y1 = height - DIAMOND_SLICE_CORNER_RADIUS;

      /* The center region of the nine-slice can simply map to the
       * degenerate center of the circle */
      float s0 = 0.5;
      float t0 = 0.5;
      float s1 = 0.5;
      float t1 = 0.5;

      int n_vertices;
      int i;

      /*
       * 0,0      x0,0      x1,0      width,0
       * 0,0      s0,0      s1,0      1,0
       * 0        1         2         3
       *
       * 0,y0     x0,y0     x1,y0     width,y0
       * 0,t0     s0,t0     s1,t0     1,t0
       * 4        5         6         7
       *
       * 0,y1     x0,y1     x1,y1     width,y1
       * 0,t1     s0,t1     s1,t1     1,t1
       * 8        9         10        11
       *
       * 0,height x0,height x1,height width,height
       * 0,1      s0,1      s1,1      1,1
       * 12       13        14        15
       */

      VertexP2T2T2 vertices[] =
        {
          { 0,  0, 0, 0, 0, 0 },
          { x0, 0, s0, 0, x0, 0},
          { x1, 0, s1, 0, x1, 0},
          { width, 0, 1, 0, width, 0},

          { 0, y0, 0, t0, 0, y0},
          { x0, y0, s0, t0, x0, y0},
          { x1, y0, s1, t0, x1, y0},
          { width, y0, 1, t0, width, y0},

          { 0, y1, 0, t1, 0, y1},
          { x0, y1, s0, t1, x0, y1},
          { x1, y1, s1, t1, x1, y1},
          { width, y1, 1, t1, width, y1},

          { 0, height, 0, 1, 0, height},
          { x0, height, s0, 1, x0, height},
          { x1, height, s1, 1, x1, height},
          { width, height, 1, 1, width, height},
        };

      cogl_matrix_init_identity (&diamond_slice->rotate_matrix);
      cogl_matrix_rotate (&diamond_slice->rotate_matrix, 45, 0, 0, 1);
      cogl_matrix_translate (&diamond_slice->rotate_matrix, - width / 2.0, - height / 2.0, 0);

      n_vertices = sizeof (vertices) / sizeof (VertexP2T2T2);
      for (i = 0; i < n_vertices; i++)
        {
          float z = 0, w = 1;

          cogl_matrix_transform_point (&diamond_slice->rotate_matrix,
                                       &vertices[i].x,
                                       &vertices[i].y,
                                       &z,
                                       &w);

          vertices[i].Nx = 0;
          vertices[i].Ny = 0;
          vertices[i].Nz = 1;

          vertices[i].Tx = 1;
          vertices[i].Ty = 0;
          vertices[i].Tz = 0;
        }

      cogl_matrix_init_identity (&matrix);

      {
        float s_scale = 1.0, t_scale = 1.0;
        float s0, t0;
        float diagonal_size_scale = 1.0 / (sinf (G_PI_4) * 2.0);

        tex_aspect = (float)tex_width / (float)tex_height;

        if (tex_aspect < 1) /* taller than it is wide */
          t_scale *= tex_aspect;
        else /* wider than it is tall */
          {
            float inverse_aspect = 1.0f / tex_aspect;
            s_scale *= inverse_aspect;
          }

        s_scale *= diagonal_size_scale;
        t_scale *= diagonal_size_scale;

        s0 = 0.5 - (s_scale / 2.0);
        t0 = 0.5 - (t_scale / 2.0);

        cogl_matrix_translate (&matrix, s0, t0, 0);
        cogl_matrix_scale (&matrix, s_scale / width, t_scale / height, 1);

        cogl_matrix_translate (&matrix, width / 2.0, height / 2.0, 1);
        cogl_matrix_rotate (&matrix, 45, 0, 0, 1);
        cogl_matrix_translate (&matrix, -width / 2.0, -height / 2.0, 1);
      }

      n_vertices = sizeof (vertices) / sizeof (VertexP2T2T2);
      for (i = 0; i < n_vertices; i++)
        {
          float z = 0, w = 1;

          cogl_matrix_transform_point (&matrix,
                                       &vertices[i].s1,
                                       &vertices[i].t1,
                                       &z,
                                       &w);
        }

      diamond_slice->mesh = mesh_new_p2t2t2 (COGL_VERTICES_MODE_TRIANGLES,
                                             n_vertices, vertices);

    }

  return diamond_slice;
}

RutType rut_diamond_type;

static void
_rut_diamond_free (void *object)
{
  RutDiamond *diamond = object;

  rut_refable_unref (diamond->slice);
  rut_refable_unref (diamond->pick_mesh);

  g_slice_free (RutDiamond, diamond);
}

void
_rut_diamond_init_type (void)
{
  static RutRefCountableVTable ref_countable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _rut_diamond_free
  };

  static RutComponentableVTable componentable_vtable = {
    0
  };

  static RutPrimableVTable primable_vtable = {
    .get_primitive = rut_diamond_get_primitive
  };

  static RutPickableVTable pickable_vtable = {
    .get_mesh = rut_diamond_get_pick_mesh
  };

  RutType *type = &rut_diamond_type;

#define TYPE RutDiamond

  rut_type_init (type, "RigDiamond");
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &ref_countable_vtable);
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

RutDiamond *
rut_diamond_new (RutContext *ctx,
                 float size,
                 int tex_width,
                 int tex_height)
{
  RutDiamond *diamond = g_slice_new0 (RutDiamond);
  RutBuffer *buffer = rut_buffer_new (sizeof (CoglVertexP3) * 6);
  RutMesh *pick_mesh = rut_mesh_new_from_buffer_p3 (COGL_VERTICES_MODE_TRIANGLES,
                                                    6,
                                                    buffer);
  CoglVertexP3 *pick_vertices = (CoglVertexP3 *)buffer->data;

  rut_object_init (&diamond->_parent, &rut_diamond_type);

  diamond->ref_count = 1;

  diamond->component.type = RUT_COMPONENT_TYPE_GEOMETRY;

  diamond->ctx = rut_refable_ref (ctx);

  diamond->size = size;

  /* XXX: It could be worth maintaining a cache of diamond slices
   * indexed by the <size, tex_width, tex_height> tuple... */
  diamond->slice = diamond_slice_new (size, tex_width, tex_height);

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

  cogl_matrix_transform_points (&diamond->slice->rotate_matrix,
                                2,
                                sizeof (CoglVertexP3),
                                pick_vertices,
                                sizeof (CoglVertexP3),
                                pick_vertices,
                                6);

  diamond->pick_mesh = pick_mesh;

  return diamond;
}

float
rut_diamond_get_size (RutDiamond *diamond)
{
  return diamond->size;
}

CoglPrimitive *
rut_diamond_get_primitive (RutObject *object)
{
  RutDiamond *diamond = object;
  CoglPrimitive *primitive = rut_mesh_create_primitive (diamond->ctx,
                                                        diamond->slice->mesh);
  return primitive;
}

void
rut_diamond_apply_mask (RutDiamond *diamond,
                        CoglPipeline *pipeline)
{
  RutContext *ctx = diamond->ctx;
  cogl_pipeline_set_layer_texture (pipeline,
                                   0,
                                   ctx->circle_texture);
}

RutMesh *
rut_diamond_get_pick_mesh (RutObject *self)
{
  RutDiamond *diamond = self;
  return diamond->pick_mesh;
}
