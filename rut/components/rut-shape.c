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

#include "rut-shape.h"
#include "rut-global.h"
#include "math.h"

#define MESA_CONST_ATTRIB_BUG_WORKAROUND

static RutPropertySpec _rut_shape_prop_specs[] = {
  {
    .name = "shaped",
    .nick = "Shaped",
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .data_offset = G_STRUCT_OFFSET (RutShape, shaped),
    .setter.boolean_type = rut_shape_set_shaped,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
  },
  { NULL }
};

static void
_shape_model_free (void *object)
{
  RutShapeModel *shape_model = object;

  cogl_object_unref (shape_model->shape_texture);
  rut_refable_unref (shape_model->pick_mesh);
  rut_refable_unref (shape_model->shape_mesh);

  g_slice_free (RutShapeModel, object);
}

RutType rut_shape_model_type;

void
_rut_shape_model_init_type (void)
{
  static RutRefCountableVTable ref_countable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _shape_model_free
  };

  RutType *type = &rut_shape_model_type;

#define TYPE RutShapeModel

  rut_type_init (type, "RigShapeModel");
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &ref_countable_vtable);

#undef TYPE
}

typedef struct _VertexP2T2T2
{
  float x, y, s0, t0, s1, t1;
#ifdef MESA_CONST_ATTRIB_BUG_WORKAROUND
  float Nx, Ny, Nz;
  float Tx, Ty, Tz;
#endif
} VertexP2T2T2;

static RutMesh *
mesh_new_p2t2t2 (CoglVerticesMode mode,
                 int n_vertices,
                 const VertexP2T2T2 *data)
{
  RutMesh *mesh;
  RutAttribute *attributes[7];
  RutBuffer *vertex_buffer;

  vertex_buffer = rut_buffer_new (sizeof (VertexP2T2T2) * n_vertices);
  memcpy (vertex_buffer->data, data, sizeof (VertexP2T2T2) * n_vertices);

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

  return mesh;
}

static RutShapeModel *
shape_model_new (RutContext *ctx,
                 CoglBool shaped,
                 float tex_width,
                 float tex_height)
{
  RutShapeModel *shape_model = g_slice_new (RutShapeModel);
  RutBuffer *buffer = rut_buffer_new (sizeof (CoglVertexP3) * 6);
  RutMesh *pick_mesh = rut_mesh_new_from_buffer_p3 (COGL_VERTICES_MODE_TRIANGLES,
                                                    6,
                                                    buffer);
  CoglVertexP3 *pick_vertices = (CoglVertexP3 *)buffer->data;
  CoglMatrix matrix;
  float tex_aspect;
  float size_x;
  float size_y;
  float half_size_x;
  float half_size_y;
  float geom_size_x;
  float geom_size_y;
  float half_geom_size_x;
  float half_geom_size_y;

  rut_object_init (&shape_model->_parent, &rut_shape_model_type);

  shape_model->ref_count = 1;

  if (shaped)
    {
      /* In this case we are using a shape mask texture which is has a
       * square size and is padded with transparent pixels to provide
       * antialiasing. The shape mask is half the size of the texture
       * itself so we make the geometry twice as large to compensate.
       */
      size_x = MIN (tex_width, tex_height);
      size_y = size_x;
      geom_size_x = size_x * 2.0;
      geom_size_y = geom_size_x;
    }
  else
    {
      size_x = tex_width;
      size_y = tex_height;
      geom_size_x = tex_width;
      geom_size_y = tex_height;
    }

  half_size_x = size_x / 2.0;
  half_size_y = size_y / 2.0;
  half_geom_size_x = geom_size_x / 2.0;
  half_geom_size_y = geom_size_y / 2.0;

    {
      int n_vertices;
      int i;

      VertexP2T2T2 vertices[] =
        {
          { -half_geom_size_x, -half_geom_size_y, 0, 0, 0, 0 },
          { -half_geom_size_x,  half_geom_size_y, 0, 1, 0, 1 },
          {  half_geom_size_x,  half_geom_size_y, 1, 1, 1, 1 },

          { -half_geom_size_x, -half_geom_size_y, 0, 0, 0, 0 },
          {  half_geom_size_x,  half_geom_size_y, 1, 1, 1, 1 },
          {  half_geom_size_x, -half_geom_size_y, 1, 0, 1, 0 },
        };

      cogl_matrix_init_identity (&matrix);
      tex_aspect = (float)tex_width / (float)tex_height;

      if (shaped)
        {
          float s_scale, t_scale;
          float s0, t0;

          /* NB: The circle mask texture has a centered circle that is
           * half the width of the texture itself. We want the primary
           * texture to be mapped to this center circle. */

          s_scale = 2;
          t_scale = 2;

          if (tex_aspect < 1) /* taller than it is wide */
            t_scale *= tex_aspect;
          else /* wider than it is tall */
            {
              float inverse_aspect = 1.0f / tex_aspect;
              s_scale *= inverse_aspect;
            }

          s0 = 0.5 - (s_scale / 2.0);
          t0 = 0.5 - (t_scale / 2.0);

          cogl_matrix_translate (&matrix, s0, t0, 0);
          cogl_matrix_scale (&matrix, s_scale, t_scale, 1);
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
          vertices[i].Nx = 0;
          vertices[i].Ny = 0;
          vertices[i].Nz = 1;

          vertices[i].Tx = 1;
          vertices[i].Ty = 0;
          vertices[i].Tz = 0;
        }

      shape_model->shape_mesh =
        mesh_new_p2t2t2 (COGL_VERTICES_MODE_TRIANGLES, n_vertices, vertices);
    }

  shape_model->shape_texture = cogl_object_ref (ctx->circle_texture);

  pick_vertices[0].x = -half_size_x;
  pick_vertices[0].y = -half_size_y;
  pick_vertices[1].x = -half_size_x;
  pick_vertices[1].y = half_size_y;
  pick_vertices[2].x = half_size_x;
  pick_vertices[2].y = half_size_y;
  pick_vertices[3] = pick_vertices[0];
  pick_vertices[4] = pick_vertices[2];
  pick_vertices[5].x = half_size_x;
  pick_vertices[5].y = -half_size_y;

  shape_model->pick_mesh = pick_mesh;


  return shape_model;
}

RutType rut_shape_type;

static void
_rut_shape_free (void *object)
{
  RutShape *shape = object;

  rut_refable_unref (shape->model);

  rut_simple_introspectable_destroy (shape);

  rut_closure_list_disconnect_all (&shape->reshaped_cb_list);

  g_slice_free (RutShape, shape);
}

void
_rut_shape_init_type (void)
{
  static RutRefCountableVTable ref_countable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _rut_shape_free
  };

  static RutComponentableVTable componentable_vtable = {
    0
  };

  static RutPrimableVTable primable_vtable = {
    .get_primitive = rut_shape_get_primitive
  };

  static RutPickableVTable pickable_vtable = {
    .get_mesh = rut_shape_get_pick_mesh
  };

  static RutIntrospectableVTable introspectable_vtable = {
    rut_simple_introspectable_lookup_property,
    rut_simple_introspectable_foreach_property
  };

  RutType *type = &rut_shape_type;

#define TYPE RutShape

  rut_type_init (type, "RigShape");
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

RutShape *
rut_shape_new (RutContext *ctx,
               CoglBool shaped,
               int tex_width,
               int tex_height)
{
  RutShape *shape = g_slice_new0 (RutShape);

  rut_object_init (&shape->_parent, &rut_shape_type);

  shape->ref_count = 1;

  shape->component.type = RUT_COMPONENT_TYPE_GEOMETRY;

  shape->ctx = rut_refable_ref (ctx);

  shape->tex_width = tex_width;
  shape->tex_height = tex_height;
  shape->shaped = shaped;

  rut_list_init (&shape->reshaped_cb_list);

  rut_simple_introspectable_init (shape,
                                  _rut_shape_prop_specs,
                                  shape->properties);

  return shape;
}

static RutShapeModel *
rut_shape_get_model (RutShape *shape)
{
  if (!shape->model)
    {
      shape->model = shape_model_new (shape->ctx,
                                      shape->shaped,
                                      shape->tex_width,
                                      shape->tex_height);
    }

  return shape->model;
}

CoglPrimitive *
rut_shape_get_primitive (RutObject *object)
{
  RutShape *shape = object;
  RutShapeModel *model = rut_shape_get_model (shape);
  CoglPrimitive *primitive = rut_mesh_create_primitive (shape->ctx,
                                                        model->shape_mesh);

  return primitive;
}

CoglTexture *
rut_shape_get_shape_texture (RutShape *shape)
{
  RutShapeModel *model = rut_shape_get_model (shape);

  return model->shape_texture;
}

RutMesh *
rut_shape_get_pick_mesh (RutObject *self)
{
  RutShape *shape = self;
  RutShapeModel *model = rut_shape_get_model (shape);
  return model->pick_mesh;
}

void
rut_shape_set_shaped (RutObject *obj,
                      CoglBool shaped)
{
  RutShape *shape = RUT_SHAPE (obj);

  if (shape->shaped == shaped)
    return;

  shape->shaped = shaped;

  if (shape->model)
    {
      rut_refable_unref (shape->model);
      shape->model = NULL;
    }

  rut_closure_list_invoke (&shape->reshaped_cb_list,
                           RutShapeReShapedCallback,
                           shape);

  rut_property_dirty (&shape->ctx->property_ctx,
                      &shape->properties[RUT_SHAPE_PROP_SHAPED]);
}

CoglBool
rut_shape_get_shaped (RutObject *obj)
{
  RutShape *shape = RUT_SHAPE (obj);

  return shape->shaped;
}

RutClosure *
rut_shape_add_reshaped_callback (RutShape *shape,
                                 RutShapeReShapedCallback callback,
                                 void *user_data,
                                 RutClosureDestroyCallback destroy_cb)
{
  g_return_val_if_fail (callback != NULL, NULL);
  return rut_closure_list_add (&shape->reshaped_cb_list,
                               callback,
                               user_data,
                               destroy_cb);
}

void
rut_shape_set_texture_size (RutShape *shape,
                            int tex_width,
                            int tex_height)
{
  if (shape->tex_width == tex_width &&
      shape->tex_height == tex_height)
    return;

  shape->tex_width = tex_width;
  shape->tex_height = tex_height;

  if (shape->model)
    {
      rut_refable_unref (shape->model);
      shape->model = NULL;
    }

  rut_closure_list_invoke (&shape->reshaped_cb_list,
                           RutShapeReShapedCallback,
                           shape);
}
