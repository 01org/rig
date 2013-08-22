/*
 * Rut.
 *
 * Copyright (C) 2013  Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include <glib.h>

#include <cogl/cogl.h>

#include "rut-interfaces.h"
#include "rut-paintable.h"
#include "rut-camera-private.h"
#include "rut-nine-slice.h"
#include "rut-closure.h"

enum {
  RUT_NINE_SLICE_PROP_WIDTH,
  RUT_NINE_SLICE_PROP_HEIGHT,
  RUT_NINE_SLICE_PROP_LEFT,
  RUT_NINE_SLICE_PROP_RIGHT,
  RUT_NINE_SLICE_PROP_TOP,
  RUT_NINE_SLICE_PROP_BOTTOM,
  RUT_NINE_SLICE_N_PROPS
};

struct _RutNineSlice
{
  RutObjectProps _parent;
  int ref_count;

  RutContext *ctx;

  RutComponentableProps component;

  /* NB: The texture and pipeline properties are only used when using
   * a nine-slice as a traditional widget. When using a nine-slice as
   * a component then this will be NULL and the texture will be
   * defined by a material component. */
  CoglTexture *texture;
  CoglPipeline *pipeline;

  /* Since ::texture is optional so we track the width/height
   * separately */
  int tex_width;
  int tex_height;

  float left;
  float right;
  float top;
  float bottom;

  float width;
  float height;

  RutMesh *mesh;

  RutGraphableProps graphable;
  RutPaintableProps paintable;

  RutList updated_cb_list;

  RutSimpleIntrospectableProps introspectable;
  RutProperty properties[RUT_NINE_SLICE_N_PROPS];
};

static RutPropertySpec _rut_nine_slice_prop_specs[] = {
  {
    .name = "width",
    .nick = "Width",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = G_STRUCT_OFFSET (RutNineSlice, width),
    .setter.float_type = rut_nine_slice_set_width,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
  },
  {
    .name = "height",
    .nick = "Height",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = G_STRUCT_OFFSET (RutNineSlice, height),
    .setter.float_type = rut_nine_slice_set_height,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
  },
  {
    .name = "left",
    .nick = "Left",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = G_STRUCT_OFFSET (RutNineSlice, left),
    .setter.float_type = rut_nine_slice_set_left,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
  },
  {
    .name = "right",
    .nick = "Right",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = G_STRUCT_OFFSET (RutNineSlice, right),
    .setter.float_type = rut_nine_slice_set_right,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
  },
  {
    .name = "top",
    .nick = "Top",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = G_STRUCT_OFFSET (RutNineSlice, top),
    .setter.float_type = rut_nine_slice_set_top,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
  },
  {
    .name = "bottom",
    .nick = "Bottom",
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = G_STRUCT_OFFSET (RutNineSlice, bottom),
    .setter.float_type = rut_nine_slice_set_bottom,
    .flags = RUT_PROPERTY_FLAG_READWRITE,
  },
  { NULL }
};

typedef struct _VertexP2T2T2
{
  float x, y, s0, t0, s1, t1;

  /* TODO: support constant attributes in RutMesh, and also ensure
   * Mesa'a support for constant attributes gets fixed */
  float Nx, Ny, Nz;
  float Tx, Ty, Tz;
} VertexP2T2T2;

static RutMesh *
mesh_new_p2t2t2 (CoglVerticesMode mode,
                 int n_vertices,
                 VertexP2T2T2 *vertices)
{
  RutMesh *mesh;
  RutAttribute *attributes[8];
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
                                     "cogl_tex_coord11_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, s1),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);


  attributes[6] = rut_attribute_new (vertex_buffer,
                                     "cogl_normal_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, Nx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[7] = rut_attribute_new (vertex_buffer,
                                     "tangent_in",
                                     sizeof (VertexP2T2T2),
                                     offsetof (VertexP2T2T2, Tx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  mesh = rut_mesh_new (mode, n_vertices, attributes, 8);
  rut_mesh_set_indices (mesh,
                        COGL_INDICES_TYPE_UNSIGNED_BYTE,
                        index_buffer,
                        sizeof (_rut_nine_slice_indices_data) /
                        sizeof (_rut_nine_slice_indices_data[0]));

  return mesh;
}

static void
create_mesh (RutNineSlice *nine_slice)
{
  float tex_width = nine_slice->tex_width;
  float tex_height = nine_slice->tex_height;
  float width = nine_slice->width;
  float height = nine_slice->height;
  float left = nine_slice->left;
  float right = nine_slice->right;
  float top = nine_slice->top;
  float bottom = nine_slice->bottom;
  int n_vertices;

  /* x0,y0,x1,y1 and s0,t0,s1,t1 define the postion and texture
   * coordinates for the center rectangle... */
  float x0 = left;
  float y0 = top;
  float x1 = width - right;
  float y1 = height - bottom;

  /* tex coords 0 */
  float s0_1 = left / tex_width;
  float t0_1 = top / tex_height;
  float s1_1 = (tex_width - right) / tex_width;
  float t1_1 = (tex_height - bottom) / tex_height;

  /* tex coords 1 */
  float s0_0 = left / width;
  float t0_0 = top / height;
  float s1_0 = (width - right) / width;
  float t1_0 = (height - bottom) / height;

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
        { 0,     0, 0,    0, 0,    0},
        { x0,    0, s0_0, 0, s0_1, 0},
        { x1,    0, s1_0, 0, s1_1, 0},
        { width, 0, 1,    0, 1,    0},

        { 0,     y0, 0,    t0_0, 0,    t0_1},
        { x0,    y0, s0_0, t0_0, s0_1, t0_1},
        { x1,    y0, s1_0, t0_0, s1_1, t0_1},
        { width, y0, 1,    t0_0, 1,    t0_1},

        { 0,     y1, 0,    t1_0, 0,    t1_1},
        { x0,    y1, s0_0, t1_0, s0_1, t1_1},
        { x1,    y1, s1_0, t1_0, s1_1, t1_1},
        { width, y1, 1,    t1_0, 1,    t1_1},

        { 0,     height, 0,    1, 0,    1},
        { x0,    height, s0_0, 1, s0_1, 1},
        { x1,    height, s1_0, 1, s1_1, 1},
        { width, height, 1,    1, 1,    1},
    };

  /* TODO: support constant attributes in RutMesh, and also ensure
   * Mesa'a support for constant attributes gets fixed */
  n_vertices = sizeof (vertices) / sizeof (VertexP2T2T2);
  for (i = 0; i < n_vertices; i++)
    {
      vertices[i].Nx = 0;
      vertices[i].Ny = 0;
      vertices[i].Nz = 1;

      vertices[i].Tx = 1;
      vertices[i].Ty = 0;
      vertices[i].Tz = 0;
    }

  nine_slice->mesh = mesh_new_p2t2t2 (COGL_VERTICES_MODE_TRIANGLES,
                                      n_vertices,
                                      vertices);
}

static void
free_mesh (RutNineSlice *nine_slice)
{
  if (nine_slice->mesh)
    {
      rut_refable_unref (nine_slice->mesh);
      nine_slice->mesh = NULL;
    }
}

static void
_rut_nine_slice_free (void *object)
{
  RutNineSlice *nine_slice = object;

  rut_closure_list_disconnect_all (&nine_slice->updated_cb_list);

  if (nine_slice->texture)
    cogl_object_unref (nine_slice->texture);

  if (nine_slice->pipeline)
    cogl_object_unref (nine_slice->pipeline);

  free_mesh (nine_slice);

  rut_graphable_destroy (nine_slice);

  rut_simple_introspectable_destroy (nine_slice);

  g_slice_free (RutNineSlice, object);
}

static void
_rut_nine_slice_paint (RutObject *object,
                       RutPaintContext *paint_ctx)
{
  RutNineSlice *nine_slice = object;
  RutCamera *camera = paint_ctx->camera;
  CoglFramebuffer *fb = camera->fb;

  float left = nine_slice->left;
  float right = nine_slice->right;
  float top = nine_slice->top;
  float bottom = nine_slice->bottom;

  /* simple stretch */
  if (left == 0 && right == 0 && top == 0 && bottom == 0)
    {
      cogl_framebuffer_draw_rectangle (fb,
                                       nine_slice->pipeline,
                                       0, 0,
                                       nine_slice->width,
                                       nine_slice->height);
    }
  else
    {
      float width = nine_slice->width;
      float height = nine_slice->height;
      CoglTexture *texture = nine_slice->texture;
      float tex_width = cogl_texture_get_width (texture);
      float tex_height = cogl_texture_get_height (texture);

      /* s0,t0,s1,t1 define the texture coordinates for the center
       * rectangle... */
      float s0 = left / tex_width;
      float t0 = top / tex_height;
      float s1 = (tex_width - right) / tex_width;
      float t1 = (tex_height - bottom) / tex_height;

      float ex;
      float ey;

      ex = width - right;
      if (ex < left)
        ex = left;

      ey = height - bottom;
      if (ey < top)
        ey = top;

      {
        float rectangles[] =
          {
            /* top left corner */
            0, 0,
            left, top,
            0.0, 0.0,
            s0, t0,

            /* top middle */
            left, 0,
            MAX (left, ex), top,
            s0, 0.0,
            s1, t0,

            /* top right */
            ex, 0,
            MAX (ex + right, width), top,
            s1, 0.0,
            1.0, t0,

            /* mid left */
            0, top,
            left,  ey,
            0.0, t0,
            s0, t1,

            /* center */
            left, top,
            ex, ey,
            s0, t0,
            s1, t1,

            /* mid right */
            ex, top,
            MAX (ex + right, width), ey,
            s1, t0,
            1.0, t1,

            /* bottom left */
            0, ey,
            left, MAX (ey + bottom, height),
            0.0, t1,
            s0, 1.0,

            /* bottom center */
            left, ey,
            ex, MAX (ey + bottom, height),
            s0, t1,
            s1, 1.0,

            /* bottom right */
            ex, ey,
            MAX (ex + right, width), MAX (ey + bottom, height),
            s1, t1,
            1.0, 1.0
          };

          cogl_framebuffer_draw_textured_rectangles (fb,
                                                     nine_slice->pipeline,
                                                     rectangles,
                                                     9);
      }
    }
}

static RutObject *
_rut_nine_slice_copy (RutObject *object)
{
  RutNineSlice *nine_slice = object;

  return rut_nine_slice_new (nine_slice->ctx,
                             nine_slice->texture,
                             nine_slice->top,
                             nine_slice->right,
                             nine_slice->bottom,
                             nine_slice->left,
                             nine_slice->width,
                             nine_slice->height);
}

RutType rut_nine_slice_type;

static void
_rut_nine_slice_init_type (void)
{
  static RutRefableVTable refable_vtable = {
      rut_refable_simple_ref,
      rut_refable_simple_unref,
      _rut_nine_slice_free
  };

  static RutGraphableVTable graphable_vtable = {
      NULL, /* child remove */
      NULL, /* child add */
      NULL /* parent changed */
  };

  static RutComponentableVTable componentable_vtable = {
      .copy = _rut_nine_slice_copy
  };

  static RutIntrospectableVTable introspectable_vtable = {
      rut_simple_introspectable_lookup_property,
      rut_simple_introspectable_foreach_property
  };

  static RutPaintableVTable paintable_vtable = {
      _rut_nine_slice_paint
  };

  static RutPrimableVTable primable_vtable = {
      .get_primitive = rut_nine_slice_get_primitive
  };

  static RutPickableVTable pickable_vtable = {
      .get_mesh = rut_nine_slice_get_pick_mesh
  };

  static RutSizableVTable sizable_vtable = {
      rut_nine_slice_set_size,
      rut_nine_slice_get_size,
      rut_simple_sizable_get_preferred_width,
      rut_simple_sizable_get_preferred_height,
      NULL /* add_preferred_size_callback */
  };

  static RutImageSizeDependantVTable image_dependant_vtable = {
      .set_image_size = rut_nine_slice_set_image_size
  };

  RutType *type = &rut_nine_slice_type;
#define TYPE RutNineSlice

  rut_type_init (type, G_STRINGIFY (TYPE));
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &refable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (TYPE, graphable),
                          &graphable_vtable);
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
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_PAINTABLE,
                          offsetof (TYPE, paintable),
                          &paintable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_PRIMABLE,
                          0, /* no associated properties */
                          &primable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_PICKABLE,
                          0, /* no associated properties */
                          &pickable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_SIZABLE,
                          0, /* no implied properties */
                          &sizable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_IMAGE_SIZE_DEPENDENT,
                          0, /* no implied properties */
                          &image_dependant_vtable);

#undef TYPE
}

RutNineSlice *
rut_nine_slice_new (RutContext *ctx,
                    CoglTexture *texture,
                    float top,
                    float right,
                    float bottom,
                    float left,
                    float width,
                    float height)
{
  RutNineSlice *nine_slice = g_slice_new (RutNineSlice);
  static CoglBool initialized = FALSE;

  if (initialized == FALSE)
    {
      _rut_nine_slice_init_type ();
      initialized = TRUE;
    }

  rut_object_init (&nine_slice->_parent, &rut_nine_slice_type);

  nine_slice->ctx = ctx;

  nine_slice->ref_count = 1;

  nine_slice->component.type = RUT_COMPONENT_TYPE_GEOMETRY;

  rut_list_init (&nine_slice->updated_cb_list);

  rut_graphable_init (nine_slice);

  nine_slice->left = left;
  nine_slice->right = right;
  nine_slice->top = top;
  nine_slice->bottom = bottom;

  nine_slice->width = width;
  nine_slice->height = height;

  nine_slice->mesh = NULL;

  nine_slice->texture = NULL;
  nine_slice->pipeline = NULL;
  if (texture)
    rut_nine_slice_set_texture (nine_slice, texture);
  else
    {
      nine_slice->tex_width = width;
      nine_slice->tex_height = height;
    }

  rut_simple_introspectable_init (nine_slice,
                                  _rut_nine_slice_prop_specs,
                                  nine_slice->properties);

  return nine_slice;
}

CoglTexture *
rut_nine_slice_get_texture (RutNineSlice *nine_slice)
{
  return nine_slice->texture;
}

void
rut_nine_slice_set_texture (RutNineSlice *nine_slice,
                            CoglTexture *texture)
{
  if (nine_slice->texture == texture)
    return;

  free_mesh (nine_slice);

  if (nine_slice->texture)
    cogl_object_unref (nine_slice->texture);
  if (nine_slice->pipeline)
    cogl_object_unref (nine_slice->pipeline);

  nine_slice->pipeline =
    cogl_pipeline_copy (nine_slice->ctx->single_texture_2d_template);

  if (texture)
    {
      nine_slice->tex_width = cogl_texture_get_width (texture);
      nine_slice->tex_height = cogl_texture_get_height (texture);

      nine_slice->texture = cogl_object_ref (texture);
      cogl_pipeline_set_layer_texture (nine_slice->pipeline, 0, texture);
    }
  else
    {
      nine_slice->tex_width = nine_slice->width;
      nine_slice->tex_height = nine_slice->height;
      nine_slice->texture = NULL;
    }
}

void
rut_nine_slice_set_image_size (RutObject *self,
                               int width,
                               int height)
{
  RutNineSlice *nine_slice = self;

  if (nine_slice->tex_width == width &&
      nine_slice->tex_height == height)
    return;

  free_mesh (nine_slice);

  nine_slice->tex_width = width;
  nine_slice->tex_height = height;

  rut_closure_list_invoke (&nine_slice->updated_cb_list,
                           RutNineSliceUpdateCallback,
                           nine_slice);
}

void
rut_nine_slice_set_size (RutObject *self,
                         float width,
                         float height)
{
  RutNineSlice *nine_slice = self;

  free_mesh (nine_slice);

  nine_slice->width = width;
  nine_slice->height = height;
}

void
rut_nine_slice_get_size (RutObject *self,
                         float *width,
                         float *height)
{
  RutNineSlice *nine_slice = self;
  *width = nine_slice->width;
  *height = nine_slice->height;
}

CoglPipeline *
rut_nine_slice_get_pipeline (RutNineSlice *nine_slice)
{
  return nine_slice->pipeline;
}

CoglPrimitive *
rut_nine_slice_get_primitive (RutObject *object)
{
  RutNineSlice *nine_slice = object;

  if (!nine_slice->mesh)
    create_mesh (nine_slice);

  return rut_mesh_create_primitive (nine_slice->ctx, nine_slice->mesh);
}

RutMesh *
rut_nine_slice_get_pick_mesh (RutObject *object)
{
  RutNineSlice *nine_slice = object;

  if (!nine_slice->mesh)
    create_mesh (nine_slice);

  return nine_slice->mesh;
}

RutClosure *
rut_nine_slice_add_update_callback (RutNineSlice *nine_slice,
                                    RutNineSliceUpdateCallback callback,
                                    void *user_data,
                                    RutClosureDestroyCallback destroy_cb)
{
  g_return_val_if_fail (callback != NULL, NULL);
  return rut_closure_list_add (&nine_slice->updated_cb_list,
                               callback,
                               user_data,
                               destroy_cb);
}

#define SLICE_PROPERTY(PROP_LC, PROP_UC) \
void \
rut_nine_slice_set_ ## PROP_LC (RutObject *obj, float PROP_LC) \
{ \
  RutNineSlice *nine_slice = obj; \
  nine_slice->PROP_LC = PROP_LC; \
  free_mesh (nine_slice); \
  rut_property_dirty (&nine_slice->ctx->property_ctx, \
                      &nine_slice->properties[RUT_NINE_SLICE_PROP_ ## PROP_UC]); \
  rut_closure_list_invoke (&nine_slice->updated_cb_list, \
                           RutNineSliceUpdateCallback, \
                           nine_slice); \
}

SLICE_PROPERTY (width, WIDTH)
SLICE_PROPERTY (height, HEIGHT)
SLICE_PROPERTY (left, LEFT)
SLICE_PROPERTY (right, RIGHT)
SLICE_PROPERTY (top, TOP)
SLICE_PROPERTY (bottom, BOTTOM)

#undef SLICE_PROPERTY
