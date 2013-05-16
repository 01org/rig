/*
 * Rig
 *
 * Copyright (C) 2012 Intel Corporation.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include "rut.h"

#include "rig-transition-view.h"

/* The number of controls to display for each property. Currently
 * there is only the label for the property name but there is an
 * expectation that we will add more controls here so the layout is
 * treated as a grid with the potential for more controls */
#define RIG_TRANSITION_VIEW_N_PROPERTY_CONTROLS 1
/* Same for the number of controls per object */
#define RIG_TRANSITION_VIEW_N_OBJECT_CONTROLS 1

#define RIG_TRANSITION_VIEW_N_COLUMNS                   \
  (MAX (RIG_TRANSITION_VIEW_N_PROPERTY_CONTROLS,        \
        RIG_TRANSITION_VIEW_N_OBJECT_CONTROLS))

#define RIG_TRANSITION_VIEW_PROPERTY_INDENTATION 10

/* Width of the progress marker */
#define RIG_TRANSITION_VIEW_PROGRESS_WIDTH 4.0f

#define RIG_TRANSITION_VIEW_UNSELECTED_COLOR \
  GUINT32_TO_BE (0x000000ff)
#define RIG_TRANSITION_VIEW_SELECTED_COLOR \
  GUINT32_TO_BE (0x007dc4ff)

#define RIG_TRANSITION_VIEW_PADDING 2

typedef struct
{
  RutObject *transform;
  RutObject *control;
} RigTransitionViewControl;

/* When the user clicks on the area with the dots then we'll delay
 * deciding what action to take until the next mouse event. This enum
 * tracks whether we've decided the action or not */
typedef enum
{
  /* The mouse button isn't down and we're not grabbing input */
  RIG_TRANSITION_VIEW_GRAB_STATE_NO_GRAB,
  /* There hasn't been an event yet since the button press event */
  RIG_TRANSITION_VIEW_GRAB_STATE_UNDECIDED,
  /* We've decided to grab the selected nodes */
  RIG_TRANSITION_VIEW_GRAB_STATE_DRAGGING_NODES,
  /* We've decided to move the timeline position */
  RIG_TRANSITION_VIEW_GRAB_STATE_MOVING_TIMELINE,
  /* The user is drawing a bounding box to select nodes */
  RIG_TRANSITION_VIEW_GRAB_STATE_DRAW_BOX
} RigTransitionViewGrabState;

typedef struct _RigTransitionViewObject RigTransitionViewObject;

typedef struct
{
  RutList list_node;

  /* Pointer back to the parent object */
  RigTransitionViewObject *object;

  RutProperty *property;
  RigPath *path;

  RigTransitionViewControl controls[RIG_TRANSITION_VIEW_N_PROPERTY_CONTROLS];

  RutClosure *path_operation_closure;

  /* True if this property currently has any selected nodes. This is
   * an optimisation so that we can generate the dots buffer slightly
   * faster by only checking in the selected nodes list for paths for
   * properties that have selected nodes */
  CoglBool has_selected_nodes;
} RigTransitionViewProperty;

struct _RigTransitionViewObject
{
  RutList list_node;

  RutObject *object;

  RutList properties;

  RigTransitionViewControl controls[RIG_TRANSITION_VIEW_N_OBJECT_CONTROLS];

  /* Pointer back to the transition view so that we can get back to it
   * if we use the property engine as the engine for the path operation
   * callback */
  RigTransitionView *view;
};

struct _RigTransitionView
{
  RutObjectProps _parent;

  RutContext *context;
  RigTransition *transition;
  RutClosure *transition_op_closure;
  RutTimeline *timeline;
  RigUndoJournal *undo_journal;

  RutList preferred_size_cb_list;

  RutInputRegion *input_region;
  RigTransitionViewGrabState grab_state;
  /* Position that the mouse was over when the drag was start */
  float drag_start_position;
  /* Current offset in time that the selected nodes are being dragged to */
  float drag_offset;
  /* Maximum offset range that we can drag to without making the nodes
   * overlap a neighbour */
  float min_drag_offset;
  float max_drag_offset;

  /* Position and size of the current bounding box. The x positions
   * are in normalised time and the y positions are an integer row
   * number */
  float box_x1, box_x2;
  int box_y1, box_y2;

  CoglPipeline *box_pipeline;
  CoglPath *box_path;

  RutObject *graph;

  RutPaintableProps paintable;
  RutGraphableProps graphable;

  int nodes_x;
  int nodes_width;
  int node_size;
  int total_width;
  int total_height;
  int row_height;

  RutList objects;

  RutList selected_nodes;

  CoglBool dots_dirty;
  CoglAttributeBuffer *dots_buffer;
  CoglPrimitive *dots_primitive;
  CoglPipeline *dots_pipeline;
  int n_dots;

  CoglPipeline *progress_pipeline;

  CoglPipeline *separator_pipeline;
  int separator_width;

  CoglPipeline *nodes_bg_pipeline;
  int nodes_grid_width;
  int nodes_grid_height;

  int ref_count;
};

typedef struct
{
  RutList list_node;

  RigTransitionViewProperty *prop_data;
  RigNode *node;

  /* While dragging nodes, this will be used to store the original
   * time that the node had */
  float original_time;
} RigTransitionViewSelectedNode;

typedef CoglVertexP2C4 RigTransitionViewDotVertex;

RutType rig_transition_view_type;

static void
rig_transition_view_property_removed (RigTransitionView *view,
                                      RutProperty *property);

static RutInputEventStatus
rig_transition_view_grab_input_cb (RutInputEvent *event,
                                   void *user_data);

static void
rig_transition_view_ungrab_input (RigTransitionView *view)
{
  if (view->grab_state != RIG_TRANSITION_VIEW_GRAB_STATE_NO_GRAB)
    {
      rut_shell_ungrab_input (view->context->shell,
                              rig_transition_view_grab_input_cb,
                              view);
      view->grab_state = RIG_TRANSITION_VIEW_GRAB_STATE_NO_GRAB;
    }
}

static void
rig_transition_view_clear_selected_nodes (RigTransitionView *view)
{
  RigTransitionViewSelectedNode *selected_node, *t;

  if (rut_list_empty (&view->selected_nodes))
    return;

  rut_list_for_each_safe (selected_node, t, &view->selected_nodes, list_node)
    {
      selected_node->prop_data->has_selected_nodes = FALSE;
      g_slice_free (RigTransitionViewSelectedNode, selected_node);
    }

  rut_list_init (&view->selected_nodes);

  view->dots_dirty = TRUE;
}

static void
_rig_transition_view_free (void *object)
{
  RigTransitionView *view = object;

  rut_closure_list_disconnect_all (&view->preferred_size_cb_list);

  rig_transition_view_ungrab_input (view);

  if (view->separator_pipeline)
    cogl_object_unref (view->separator_pipeline);
  if (view->nodes_bg_pipeline)
    cogl_object_unref (view->nodes_bg_pipeline);
  if (view->box_pipeline)
    cogl_object_unref (view->box_pipeline);
  if (view->box_path)
    cogl_object_unref (view->box_path);

  rig_transition_view_clear_selected_nodes (view);

  rut_refable_unref (view->graph);

  while (!rut_list_empty (&view->objects))
    {
      RigTransitionViewObject *object =
        rut_container_of (view->objects.next, object, list_node);
      CoglBool was_last;

      do
        {
          RigTransitionViewProperty *prop_data =
            rut_container_of (object->properties.next, prop_data, list_node);
          RutProperty *property = prop_data->property;

          was_last = prop_data->list_node.next == &object->properties;

          rig_transition_view_property_removed (view, property);
        }
      while (!was_last);
    }

  if (view->dots_buffer)
    cogl_object_unref (view->dots_buffer);
  if (view->dots_primitive)
    cogl_object_unref (view->dots_primitive);

  cogl_object_unref (view->dots_pipeline);

  rut_graphable_remove_child (view->input_region);
  rut_refable_unref (view->input_region);

  rut_refable_unref (view->timeline);

  rut_shell_remove_pre_paint_callback (view->context->shell, view);

  rut_refable_unref (view->context);

  rut_graphable_destroy (view);

  g_slice_free (RigTransitionView, view);
}

RutRefCountableVTable _rig_transition_view_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rig_transition_view_free
};

static RutGraphableVTable _rig_transition_view_graphable_vtable = {
  NULL, /* child removed */
  NULL, /* child addded */
  NULL /* parent changed */
};

static CoglAttributeBuffer *
rig_transition_view_create_dots_buffer (RigTransitionView *view)
{
  size_t size = MAX (8, view->n_dots) * sizeof (RigTransitionViewDotVertex);

  return cogl_attribute_buffer_new_with_size (view->context->cogl_context,
                                              size);
}

static CoglPrimitive *
rig_transition_view_create_dots_primitive (RigTransitionView *view)
{
  CoglAttribute *attributes[2];
  CoglPrimitive *prim;

  attributes[0] = cogl_attribute_new (view->dots_buffer,
                                      "cogl_position_in",
                                      sizeof (RigTransitionViewDotVertex),
                                      offsetof (RigTransitionViewDotVertex, x),
                                      2, /* n_components */
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[1] = cogl_attribute_new (view->dots_buffer,
                                      "cogl_color_in",
                                      sizeof (RigTransitionViewDotVertex),
                                      offsetof (RigTransitionViewDotVertex, r),
                                      4, /* n_components */
                                      COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE);

  prim = cogl_primitive_new_with_attributes (COGL_VERTICES_MODE_POINTS,
                                             view->n_dots,
                                             attributes,
                                             2 /* n_attributes */);

  cogl_object_unref (attributes[0]);
  cogl_object_unref (attributes[1]);

  return prim;
}

typedef struct
{
  RigTransitionView *view;
  RigTransitionViewProperty *prop_data;

  RigTransitionViewDotVertex *v;
  int row_pos;
} RigTransitionViewDotData;

static void
rig_transition_view_add_dot_unselected (RigTransitionViewDotData *dot_data,
                                        RigNode *node)
{
  RigTransitionViewDotVertex *v = dot_data->v;

  v->x = node->t;
  v->y = dot_data->row_pos;
  *(uint32_t *) &v->r = RIG_TRANSITION_VIEW_UNSELECTED_COLOR;

  dot_data->v++;
}

static void
rig_transition_view_add_dot_selected (RigTransitionViewDotData *dot_data,
                                      RigNode *node)
{
  RigTransitionViewDotVertex *v = dot_data->v;
  uint32_t color = RIG_TRANSITION_VIEW_UNSELECTED_COLOR;
  RigTransitionViewSelectedNode *selected_node;

  rut_list_for_each (selected_node, &dot_data->view->selected_nodes, list_node)
    {
      if (selected_node->prop_data == dot_data->prop_data &&
          selected_node->node == node)
        {
          color = RIG_TRANSITION_VIEW_SELECTED_COLOR;
          break;
        }
    }

  v->x = node->t;
  v->y = dot_data->row_pos;
  *(uint32_t *) &v->r = color;

  dot_data->v++;
}

static void
rig_transition_view_update_dots_buffer (RigTransitionView *view)
{
  RigTransitionViewObject *object;
  CoglBool buffer_is_mapped;
  RigTransitionViewDotVertex *buffer_data;
  RigTransitionViewDotData dot_data;
  size_t map_size = sizeof (RigTransitionViewDotVertex) * view->n_dots;
  CoglError *ignore_error = NULL;

  if (view->n_dots == 0)
    return;

  buffer_data = cogl_buffer_map_range (COGL_BUFFER (view->dots_buffer),
                                       0, /* offset */
                                       map_size,
                                       COGL_BUFFER_ACCESS_WRITE,
                                       COGL_BUFFER_MAP_HINT_DISCARD,
                                       &ignore_error);

  if (buffer_data == NULL)
    {
      buffer_data = g_malloc (map_size);
      buffer_is_mapped = FALSE;
      cogl_error_free (ignore_error);
    }
  else
    buffer_is_mapped = TRUE;

  dot_data.view = view;
  dot_data.v = buffer_data;
  dot_data.row_pos = 0;

  rut_list_for_each (object, &view->objects, list_node)
    {
      dot_data.row_pos++;

      rut_list_for_each (dot_data.prop_data, &object->properties, list_node)
        {
          RigPath *path =
            rig_transition_get_path_for_property (view->transition,
                                                  dot_data.prop_data->property);
          RigNode *node;

          if (dot_data.prop_data->has_selected_nodes)
            rut_list_for_each (node, &path->nodes, list_node)
              rig_transition_view_add_dot_selected (&dot_data,
                                                    node);
          else
            rut_list_for_each (node, &path->nodes, list_node)
              rig_transition_view_add_dot_unselected (&dot_data,
                                                      node);

          dot_data.row_pos++;
        }
    }

  if (buffer_is_mapped)
    cogl_buffer_unmap (COGL_BUFFER (view->dots_buffer));
  else
    {
      cogl_buffer_set_data (COGL_BUFFER (view->dots_buffer),
                            0, /* offset */
                            buffer_data,
                            map_size,
                            NULL);
      g_free (buffer_data);
    }

  g_assert (dot_data.v - buffer_data == view->n_dots);
}

static void
rig_transition_view_draw_box (RigTransitionView *view,
                              CoglFramebuffer *fb)
{
  if (view->box_pipeline == NULL)
    {
      view->box_pipeline = cogl_pipeline_new (view->context->cogl_context);
      cogl_pipeline_set_color4ub (view->box_pipeline, 0, 0, 0, 255);
    }

  if (view->box_path == NULL)
    {
      view->box_path = cogl_path_new (view->context->cogl_context);
      cogl_path_rectangle (view->box_path,
                           view->nodes_x + view->box_x1 * view->nodes_width,
                           view->box_y1 * view->row_height,
                           view->nodes_x + view->box_x2 * view->nodes_width,
                           view->box_y2 * view->row_height);
    }

  cogl_framebuffer_stroke_path (fb, view->box_pipeline, view->box_path);
}

static void
rig_transition_view_draw_nodes_background (RigTransitionView *view,
                                           CoglFramebuffer *fb)
{
  int tex_width = view->row_height * 8;
  int tex_height = view->row_height;
  int half_width = tex_width / 2;
  int quater_width = half_width / 2;

  if (tex_width < 1)
    return;

  if ((view->nodes_grid_width != tex_width ||
       view->nodes_grid_height != tex_height) &&
      view->nodes_bg_pipeline)
    {
      cogl_object_unref (view->nodes_bg_pipeline);
      view->nodes_bg_pipeline = NULL;
    }

  if (view->nodes_bg_pipeline == NULL)
    {
      CoglPipeline *pipeline;
      int rowstride;
      int y;
      CoglBitmap *bitmap;
      CoglPixelBuffer *buffer;
      CoglTexture *texture;
      uint8_t *tex_data;

      pipeline = cogl_pipeline_new (view->context->cogl_context);

      bitmap = cogl_bitmap_new_with_size (view->context->cogl_context,
                                          tex_width, tex_height,
                                          COGL_PIXEL_FORMAT_RGB_888);
      buffer = cogl_bitmap_get_buffer (bitmap);

      rowstride = cogl_bitmap_get_rowstride (bitmap);
      tex_data = cogl_buffer_map (COGL_BUFFER (buffer),
                                  COGL_BUFFER_ACCESS_WRITE,
                                  COGL_BUFFER_MAP_HINT_DISCARD,
                                  NULL);

      memset (tex_data, 0xff, rowstride * tex_height);
      for (y = 0; y < tex_height - 1; y++)
        {
          uint8_t *p = tex_data + y * rowstride;

          memset (p, 0x63, 3 * half_width);
          memset (p + half_width * 3, 0x47, 3 * (tex_width - half_width));

          memset (p + quater_width * 3, 0x74, 3);
          memset (p + (half_width + quater_width) * 3, 0x74, 3);
        }
      memset (tex_data + rowstride * (tex_height - 1), 0x74, tex_width * 3);

      cogl_buffer_unmap (COGL_BUFFER (buffer));

      texture = cogl_texture_new_from_bitmap (bitmap,
                                              COGL_TEXTURE_NO_ATLAS,
                                              COGL_PIXEL_FORMAT_ANY,
                                              NULL);

      cogl_pipeline_set_layer_texture (pipeline,
                                       0, /* layer_num */
                                       COGL_TEXTURE (texture));
      cogl_pipeline_set_layer_filters
        (pipeline,
         0, /* layer_num */
         COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST,
         COGL_PIPELINE_FILTER_LINEAR);
      cogl_pipeline_set_layer_wrap_mode
        (pipeline,
         0, /* layer_num */
         COGL_PIPELINE_WRAP_MODE_REPEAT);

      cogl_object_unref (bitmap);
      cogl_object_unref (texture);

      view->nodes_grid_width = tex_width;
      view->nodes_grid_height = tex_height;

      view->nodes_bg_pipeline = pipeline;
    }

  cogl_framebuffer_draw_textured_rectangle (fb,
                                            view->nodes_bg_pipeline,
                                            view->nodes_x,
                                            0,
                                            view->nodes_x + view->nodes_width,
                                            view->total_height,
                                            0, 0, /* s1, t1 */
                                            view->nodes_width /
                                            (float) tex_width,
                                            view->total_height /
                                            (float) tex_height);
}

static void
_rig_transition_view_paint (RutObject *object,
                            RutPaintContext *paint_ctx)
{
  RigTransitionView *view = object;
  CoglFramebuffer *fb = rut_camera_get_framebuffer (paint_ctx->camera);

  if (view->separator_pipeline)
    cogl_framebuffer_draw_rectangle (fb,
                                     view->separator_pipeline,
                                     view->nodes_x -
                                     view->separator_width,
                                     0,
                                     view->nodes_x,
                                     view->total_height);

  rig_transition_view_draw_nodes_background (view, fb);

  if (view->dots_dirty)
    {
      if (view->dots_buffer == NULL)
        view->dots_buffer = rig_transition_view_create_dots_buffer (view);
      else
        {
          int old_n_vertices =
            (cogl_buffer_get_size (COGL_BUFFER (view->dots_buffer)) /
             sizeof (RigTransitionViewDotVertex));

          if (old_n_vertices < view->n_dots)
            {
              cogl_object_unref (view->dots_buffer);
              cogl_object_unref (view->dots_primitive);
              view->dots_primitive = NULL;
              view->dots_buffer = rig_transition_view_create_dots_buffer (view);
            }
        }

      if (view->dots_primitive == NULL)
        view->dots_primitive = rig_transition_view_create_dots_primitive (view);
      else
        cogl_primitive_set_n_vertices (view->dots_primitive, view->n_dots);

      rig_transition_view_update_dots_buffer (view);

      view->dots_dirty = FALSE;
    }

  /* The transform is set up so that 0→1 along the x-axis extends
   * across the whole timeline. Along the y-axis 1 unit represents the
   * height of one row. This is done so that the changing the size of
   * the transition view doesn't require updating the dots buffer. It
   * doesn't matter that the scale isn't uniform because the dots are
   * drawn as points which are always sized in framebuffer pixels
   * regardless of the transformation */

  cogl_framebuffer_push_rectangle_clip (fb,
                                        view->nodes_x,
                                        0.0f,
                                        view->nodes_x + view->nodes_width,
                                        view->total_height);

  if (view->n_dots > 0)
    {
      cogl_framebuffer_push_matrix (fb);

      cogl_framebuffer_translate (fb,
                                  view->nodes_x,
                                  view->row_height * 0.5f,
                                  0.0f);
      cogl_framebuffer_scale (fb,
                              view->nodes_width,
                              view->row_height,
                              1.0f);

      cogl_framebuffer_draw_primitive (fb,
                                       view->dots_pipeline,
                                       view->dots_primitive);

      cogl_framebuffer_pop_matrix (fb);
    }

  {
    float progress_x =
      view->nodes_x +
      rut_timeline_get_progress (view->timeline) *
      view->nodes_width;

    cogl_framebuffer_draw_rectangle (fb,
                                     view->progress_pipeline,
                                     progress_x -
                                     RIG_TRANSITION_VIEW_PROGRESS_WIDTH / 2.0f,
                                     -10000.0f,
                                     progress_x +
                                     RIG_TRANSITION_VIEW_PROGRESS_WIDTH / 2.0f,
                                     10000.0f);
  }

  if (view->grab_state == RIG_TRANSITION_VIEW_GRAB_STATE_DRAW_BOX)
    rig_transition_view_draw_box (view, fb);

  cogl_framebuffer_pop_clip (fb);
}

RutPaintableVTable _rig_transition_view_paintable_vtable = {
  _rig_transition_view_paint
};

static void
rig_transition_view_allocate_cb (RutObject *graphable,
                                 void *user_data)
{
  RigTransitionView *view = RIG_TRANSITION_VIEW (graphable);
  float column_widths[RIG_TRANSITION_VIEW_N_COLUMNS];
  float row_height = 0.0f;
  RigTransitionViewObject *object;
  int i;
  int row_num;

  memset (column_widths, 0, sizeof (column_widths));

  /* Everything in a single column will be allocated to the same
   * width and everything will be allocated to the same height */

  rut_list_for_each (object, &view->objects, list_node)
    {
      RigTransitionViewProperty *prop_data;

      for (i = 0; i < RIG_TRANSITION_VIEW_N_OBJECT_CONTROLS; i++)
        {
          RigTransitionViewControl *control = object->controls + i;
          float width, height;

          rut_sizable_get_preferred_width (control->control,
                                           -1, /* for_height */
                                           NULL, /* min_width_p */
                                           &width);
          rut_sizable_get_preferred_height (control->control,
                                            width,
                                            NULL, /* min_height_p */
                                            &height);

          if (width > column_widths[i])
            column_widths[i] = width + RIG_TRANSITION_VIEW_PADDING;
          if (height > row_height)
            row_height = height;
        }

      rut_list_for_each (prop_data, &object->properties, list_node)
        {
          for (i = 0; i < RIG_TRANSITION_VIEW_N_PROPERTY_CONTROLS; i++)
            {
              RigTransitionViewControl *control = prop_data->controls + i;
              float width, height;

              rut_sizable_get_preferred_width (control->control,
                                               -1, /* for_height */
                                               NULL, /* min_width_p */
                                               &width);
              rut_sizable_get_preferred_height (control->control,
                                                width,
                                                NULL, /* min_height_p */
                                                &height);

              if (i == 0)
                width += RIG_TRANSITION_VIEW_PROPERTY_INDENTATION;

              if (width > column_widths[i])
                column_widths[i] = width + RIG_TRANSITION_VIEW_PADDING;
              if (height > row_height)
                row_height = height;
            }
        }
    }

  row_num = 0;

  rut_list_for_each (object, &view->objects, list_node)
    {
      RigTransitionViewProperty *prop_data;
      float x = 0.0f;

      for (i = 0; i < RIG_TRANSITION_VIEW_N_OBJECT_CONTROLS; i++)
        {
          RigTransitionViewControl *control = object->controls + i;

          rut_transform_init_identity (control->transform);
          rut_transform_translate (control->transform,
                                   nearbyintf (x + RIG_TRANSITION_VIEW_PADDING),
                                   nearbyintf (row_num * row_height),
                                   0.0f);
          rut_sizable_set_size (control->control,
                                nearbyintf (column_widths[i]),
                                nearbyintf (row_height));

          x += column_widths[i];

          row_num++;
        }

      rut_list_for_each (prop_data, &object->properties, list_node)
        {
          x = 0.0f;

          for (i = 0; i < RIG_TRANSITION_VIEW_N_PROPERTY_CONTROLS; i++)
            {
              RigTransitionViewControl *control = prop_data->controls + i;
              int width = nearbyintf (column_widths[i]);

              if (i == 0)
                {
                  x += RIG_TRANSITION_VIEW_PROPERTY_INDENTATION;
                  width -= RIG_TRANSITION_VIEW_PROPERTY_INDENTATION;
                }

              x = nearbyintf (x + RIG_TRANSITION_VIEW_PADDING);
              rut_transform_init_identity (control->transform);
              rut_transform_translate (control->transform,
                                       x,
                                       nearbyintf (row_num * row_height),
                                       0.0f);
              rut_sizable_set_size (control->control,
                                    width,
                                    nearbyintf (row_height));

              row_num++;
            }
        }
    }

  {
    float controls_width = 0;
    for (i = 0; i < RIG_TRANSITION_VIEW_N_COLUMNS; i++)
      controls_width += column_widths[i];
    controls_width = nearbyintf (controls_width + RIG_TRANSITION_VIEW_PADDING);

    view->nodes_x = controls_width + view->separator_width;
    view->nodes_width = view->total_width - view->nodes_x;
  }

  rut_input_region_set_rectangle (view->input_region,
                                  view->nodes_x, /* x0 */
                                  0.0f, /* y0 */
                                  view->nodes_x + view->nodes_width,
                                  view->total_height /* y1 */);

  view->row_height = nearbyintf (row_height);
  view->node_size = nearbyintf (view->row_height * 0.8f);

  if (view->node_size > 0)
    cogl_pipeline_set_point_size (view->dots_pipeline, view->node_size);
}

static void
rig_transition_view_queue_allocation (RigTransitionView *view)
{
  rut_shell_add_pre_paint_callback (view->context->shell,
                                    view,
                                    rig_transition_view_allocate_cb,
                                    NULL /* user_data */);
}

static void
rig_transition_view_preferred_size_changed (RigTransitionView *view)
{
  rut_closure_list_invoke (&view->preferred_size_cb_list,
                           RutSizablePreferredSizeCallback,
                           view);
}


static void
rig_transition_view_set_size (void *object,
                              float total_width,
                              float total_height)
{
  RigTransitionView *view = object;

  /* FIXME: RigTransitionView currently ignores its height and just
   * paints as tall as it wants */

  view->total_width = total_width;
  view->total_height = total_height;

  rig_transition_view_queue_allocation (view);
}

static void
handle_control_width (RigTransitionViewControl *control,
                      float indentation,
                      float *min_width_p,
                      float *natural_width_p)
{
  float min_width, natural_width;

  rut_sizable_get_preferred_width (control->control,
                                   -1, /* for_height */
                                   &min_width,
                                   &natural_width);

  min_width += indentation;
  natural_width += indentation;

  if (natural_width > *natural_width_p)
    *natural_width_p = natural_width;
  if (min_width > *min_width_p)
    *min_width_p = min_width;
}

static void
rig_transition_view_get_preferred_width (void *sizable,
                                         float for_height,
                                         float *min_width_p,
                                         float *natural_width_p)
{
  RigTransitionView *view = RIG_TRANSITION_VIEW (sizable);
  float min_column_widths[RIG_TRANSITION_VIEW_N_COLUMNS];
  float natural_column_widths[RIG_TRANSITION_VIEW_N_COLUMNS];
  float total_min_width = 0.0f, total_natural_width = 0.0f;
  RigTransitionViewObject *object;
  int i;

  memset (min_column_widths, 0, sizeof (min_column_widths));
  memset (natural_column_widths, 0, sizeof (natural_column_widths));

  /* Everything in a single column will be allocated to the same
   * width */

  rut_list_for_each (object, &view->objects, list_node)
    {
      RigTransitionViewProperty *prop_data;

      for (i = 0; i < RIG_TRANSITION_VIEW_N_OBJECT_CONTROLS; i++)
        {
          RigTransitionViewControl *control = object->controls + i;

          handle_control_width (control,
                                0.0f, /* indentation */
                                min_column_widths + i,
                                natural_column_widths + i);
        }

      rut_list_for_each (prop_data, &object->properties, list_node)
        {
          for (i = 0; i < RIG_TRANSITION_VIEW_N_PROPERTY_CONTROLS; i++)
            {
              RigTransitionViewControl *control = prop_data->controls + i;

              handle_control_width (control,
                                    i == 0.0f ?
                                    RIG_TRANSITION_VIEW_PROPERTY_INDENTATION :
                                    0.0f,
                                    min_column_widths + i,
                                    natural_column_widths + i);
            }
        }
    }

  for (i = 0; i < RIG_TRANSITION_VIEW_N_COLUMNS; i++)
    {
      total_min_width += min_column_widths[i];
      total_natural_width += natural_column_widths[i];
    }

  if (min_width_p)
    *min_width_p = nearbyintf (total_min_width);
  if (natural_width_p)
    *natural_width_p = nearbyintf (total_natural_width);
}

static void
handle_control_height (RigTransitionViewControl *control,
                       float *row_height)
{
  float natural_height;

  rut_sizable_get_preferred_height (control->control,
                                    -1, /* for_width */
                                    NULL, /* min_height */
                                    &natural_height);

  if (natural_height > *row_height)
    *row_height = natural_height;
}

static void
rig_transition_view_get_preferred_height (void *sizable,
                                          float for_width,
                                          float *min_height_p,
                                          float *natural_height_p)
{
  RigTransitionView *view = RIG_TRANSITION_VIEW (sizable);
  RigTransitionViewObject *object;
  float row_height = 0.0f;
  int n_rows = 0;
  int i;

  /* All of the rows will have the same height */

  rut_list_for_each (object, &view->objects, list_node)
    {
      RigTransitionViewProperty *prop_data;

      n_rows++;

      for (i = 0; i < RIG_TRANSITION_VIEW_N_OBJECT_CONTROLS; i++)
        {
          RigTransitionViewControl *control = object->controls + i;

          handle_control_height (control,
                                 &row_height);
        }

      rut_list_for_each (prop_data, &object->properties, list_node)
        {
          for (i = 0; i < RIG_TRANSITION_VIEW_N_PROPERTY_CONTROLS; i++)
            {
              RigTransitionViewControl *control = prop_data->controls + i;

              handle_control_height (control,
                                     &row_height);
            }

          n_rows++;
        }
    }

  if (min_height_p)
    *min_height_p = row_height * n_rows;
  if (natural_height_p)
    *natural_height_p = row_height * n_rows;
}

static RutClosure *
rig_transition_view_add_preferred_size_callback
                               (void *object,
                                RutSizablePreferredSizeCallback cb,
                                void *user_data,
                                RutClosureDestroyCallback destroy_cb)
{
  RigTransitionView *view = object;

  return rut_closure_list_add (&view->preferred_size_cb_list,
                               cb,
                               user_data,
                               destroy_cb);
}

static void
rig_transition_view_get_size (void *object,
                              float *width,
                              float *height)
{
  RigTransitionView *view = object;

  *width = view->total_width;
  *height = view->total_height;
}

static RutSizableVTable _rig_transition_view_sizable_vtable = {
  rig_transition_view_set_size,
  rig_transition_view_get_size,
  rig_transition_view_get_preferred_width,
  rig_transition_view_get_preferred_height,
  rig_transition_view_add_preferred_size_callback
};

static void
_rig_transition_view_init_type (void)
{
  rut_type_init (&rig_transition_view_type, "RigTransitionView");
  rut_type_add_interface (&rig_transition_view_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RigTransitionView, ref_count),
                          &_rig_transition_view_ref_countable_vtable);
  rut_type_add_interface (&rig_transition_view_type,
                          RUT_INTERFACE_ID_PAINTABLE,
                          offsetof (RigTransitionView, paintable),
                          &_rig_transition_view_paintable_vtable);
  rut_type_add_interface (&rig_transition_view_type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (RigTransitionView, graphable),
                          &_rig_transition_view_graphable_vtable);
  rut_type_add_interface (&rig_transition_view_type,
                          RUT_INTERFACE_ID_SIZABLE,
                          0, /* no implied properties */
                          &_rig_transition_view_sizable_vtable);
}

static void
rig_transition_view_create_label_control (RigTransitionView *view,
                                          RigTransitionViewControl *control,
                                          const char *text)
{
  RutText *label = rut_text_new (view->context);

  if (text)
    rut_text_set_text (label, text);

  rut_text_set_color_u32 (label, 0xffffffff);

  control->transform = rut_transform_new (view->context);
  control->control = label;

  rut_graphable_add_child (view, control->transform);
  rut_graphable_add_child (control->transform, label);
}

static CoglBool
rig_transition_view_select_node (RigTransitionView *view,
                                 RigTransitionViewProperty *prop_data,
                                 RigNode *node)
{
  RigTransitionViewSelectedNode *selected_node;

  /* Check if the node is already selected */
  if (prop_data->has_selected_nodes)
    {
      rut_list_for_each (selected_node, &view->selected_nodes, list_node)
        {
          if (selected_node->prop_data == prop_data &&
              selected_node->node == node)
            return TRUE;
        }
    }

  selected_node = g_slice_new (RigTransitionViewSelectedNode);
  selected_node->prop_data = prop_data;
  selected_node->node = node;

  prop_data->has_selected_nodes = TRUE;
  view->dots_dirty = TRUE;

  rut_list_insert (view->selected_nodes.prev, &selected_node->list_node);

  return FALSE;
}

static void
rig_transition_view_unselect_node (RigTransitionView *view,
                                   RigTransitionViewProperty *prop_data,
                                   RigNode *node)
{
  if (prop_data->has_selected_nodes)
    {
      RigTransitionViewSelectedNode *selected_node, *tmp;
      CoglBool has_nodes = FALSE;

      rut_list_for_each_safe (selected_node,
                              tmp,
                              &view->selected_nodes,
                              list_node)
        {
          if (selected_node->prop_data == prop_data)
            {
              if (selected_node->node == node)
                {
                  rut_list_remove (&selected_node->list_node);
                  g_slice_free (RigTransitionViewSelectedNode, selected_node);
                  view->dots_dirty = TRUE;
                  /* we don't want to break here because we want to
                   * continue searching so that we can update the
                   * has_nodes value */
                }
              else
                has_nodes = TRUE;
            }
        }

      prop_data->has_selected_nodes = has_nodes;
    }
}

static void
rig_transition_view_path_operation_cb (RigPath *path,
                                       RigPathOperation op,
                                       RigNode *node,
                                       void *user_data)
{
  RigTransitionViewProperty *prop_data = user_data;
  RigTransitionViewObject *object_data = prop_data->object;
  RigTransitionView *view = object_data->view;

  switch (op)
    {
    case RIG_PATH_OPERATION_MODIFIED:
      break;

    case RIG_PATH_OPERATION_ADDED:
      view->n_dots++;
      view->dots_dirty = TRUE;
      rut_shell_queue_redraw (view->context->shell);
      break;

    case RIG_PATH_OPERATION_REMOVED:
      rig_transition_view_unselect_node (view, prop_data, node);

      view->n_dots--;
      view->dots_dirty = TRUE;
      rut_shell_queue_redraw (view->context->shell);
      break;

    case RIG_PATH_OPERATION_MOVED:
      view->dots_dirty = TRUE;
      rut_shell_queue_redraw (view->context->shell);
      break;
    }
}

static void
rig_transition_view_update_label_property (RutProperty *target_property,
                                           RutProperty *source_property,
                                           void *user_data)
{
  RigTransitionView *view = user_data;
  const char *label;

  label = rut_property_get_text (source_property);

  if (label == NULL || *label == 0)
    label = "Object";

  rut_property_set_text (&view->context->property_ctx, target_property, label);

  rig_transition_view_queue_allocation (view);
  rig_transition_view_preferred_size_changed (view);
}

static RigTransitionViewObject *
rig_transition_view_create_object_data (RigTransitionView *view,
                                        RutObject *object)
{
  RigTransitionViewObject *object_data = g_slice_new (RigTransitionViewObject);
  RutProperty *label_property;
  RutProperty *text_property;
  RutTextBuffer *buffer;

  object_data->object = object;
  object_data->view = view;

  rig_transition_view_create_label_control (view,
                                            object_data->controls + 0,
                                            NULL);

  rut_text_set_font_name (RUT_TEXT (object_data->controls[0].control),
                          "Sans Bold");

  label_property = rut_introspectable_lookup_property (object, "label");
  buffer = rut_text_get_buffer (object_data->controls[0].control);
  text_property = rut_introspectable_lookup_property (buffer, "text");

  if (label_property && text_property)
    {
      rig_transition_view_update_label_property (text_property,
                                                 label_property,
                                                 view);
      rut_property_set_binding (text_property,
                                rig_transition_view_update_label_property,
                                view,
                                label_property,
                                NULL);
    }

  rut_list_init (&object_data->properties);

  rut_list_insert (view->objects.prev, &object_data->list_node);

  return object_data;
}

static void
rig_transition_view_property_added (RigTransitionView *view,
                                    RutProperty *property)
{
  RigTransitionViewProperty *prop_data;
  RigTransitionViewProperty *insert_pos;
  RigTransitionViewObject *object_data;
  const RutPropertySpec *spec = property->spec;
  RutObject *object;
  RigPath *path;

  object = property->object;

  /* If the property belongs to a component then we'll group the
   * property according to the component's object instead */
  if (rut_object_is (object, RUT_INTERFACE_ID_COMPONENTABLE))
    {
      RutComponentableProps *component =
        rut_object_get_properties (object, RUT_INTERFACE_ID_COMPONENTABLE);

      if (component->entity)
        object = component->entity;
    }

  /* Check if we already have this object */
  rut_list_for_each (object_data, &view->objects, list_node)
    if (object_data->object == object)
      goto have_object;

  object_data = rig_transition_view_create_object_data (view, object);

 have_object:

  prop_data = g_slice_new (RigTransitionViewProperty);

  prop_data->object = object_data;
  prop_data->property = property;
  prop_data->has_selected_nodes = FALSE;

  rig_transition_view_create_label_control (view,
                                            prop_data->controls + 0,
                                            spec->nick ?
                                            spec->nick :
                                            spec->name);

  path = rig_transition_get_path_for_property (view->transition,
                                               property);

  prop_data->path_operation_closure =
    rig_path_add_operation_callback (path,
                                     rig_transition_view_path_operation_cb,
                                     prop_data,
                                     NULL /* destroy_cb */);

  view->n_dots += path->length;
  view->dots_dirty = TRUE;

  prop_data->path = rut_refable_ref (path);

  /* Insert the property in a sorted position */
  rut_list_for_each (insert_pos, &object_data->properties, list_node)
    {
      /* If the property belongs to the same object then sort it
       * according to the property name */
      if (property->object == insert_pos->property->object)
        {
          if (strcmp (property->spec->nick ?
                      property->spec->nick :
                      property->spec->name,
                      insert_pos->property->spec->nick ?
                      insert_pos->property->spec->nick :
                      insert_pos->property->spec->name) < 0)
            break;
        }
      /* Make sure the entities properties are first */
      else if (property->object == object_data->object)
        break;
      else if (insert_pos->property->object == object_data->object)
        continue;
      /* Otherwise we'll just sort by the object pointer so that at
       * least the component properties are grouped */
      else if (property->object < insert_pos->property->object)
        break;
    }

  rut_list_insert (insert_pos->list_node.prev, &prop_data->list_node);

  rig_transition_view_queue_allocation (view);
  rig_transition_view_preferred_size_changed (view);
}

static void
rig_transition_view_destroy_control (RigTransitionViewControl *control)
{
  rut_graphable_remove_child (control->control);
  rut_refable_unref (control->control);
  rut_graphable_remove_child (control->transform);
  rut_refable_unref (control->transform);
}

static RigTransitionViewProperty *
rig_transition_view_find_property (RigTransitionView *view,
                                   RutProperty *property)
{
  RigTransitionViewObject *object_data;
  RutObject *object = property->object;

  /* If the property belongs to a component then it is grouped by
   * component's entity instead */
  if (rut_object_is (object, RUT_INTERFACE_ID_COMPONENTABLE))
    {
      RutComponentableProps *component =
        rut_object_get_properties (object, RUT_INTERFACE_ID_COMPONENTABLE);

      if (component->entity)
        object = component->entity;
    }

  rut_list_for_each (object_data, &view->objects, list_node)
    if (object_data->object == object)
      {
        RigTransitionViewProperty *prop_data;

        rut_list_for_each (prop_data, &object_data->properties, list_node)
          if (prop_data->property == property)
            return prop_data;
      }

  return NULL;
}

static void
rig_transition_view_property_removed (RigTransitionView *view,
                                      RutProperty *property)
{
  RigTransitionViewProperty *prop_data =
    rig_transition_view_find_property (view, property);
  RigTransitionViewObject *object_data;
  int i;

  if (prop_data == NULL)
    return;

  if (prop_data->has_selected_nodes)
    {
      RigTransitionViewSelectedNode *selected_node, *t;

      rut_list_for_each_safe (selected_node,
                              t,
                              &view->selected_nodes,
                              list_node)
        {
          if (selected_node->prop_data == prop_data)
            {
              rut_list_remove (&selected_node->list_node);
              g_slice_free (RigTransitionViewSelectedNode, selected_node);
            }
        }
    }

  object_data = prop_data->object;

  rut_closure_disconnect (prop_data->path_operation_closure);

  for (i = 0; i < RIG_TRANSITION_VIEW_N_PROPERTY_CONTROLS; i++)
    rig_transition_view_destroy_control (prop_data->controls + i);

  rut_list_remove (&prop_data->list_node);

  /* If that was the last property on the object then we'll also
   * destroy the object */
  if (rut_list_empty (&object_data->properties))
    {
      for (i = 0; i < RIG_TRANSITION_VIEW_N_OBJECT_CONTROLS; i++)
        rig_transition_view_destroy_control (object_data->controls + i);

      rut_list_remove (&object_data->list_node);

      g_slice_free (RigTransitionViewObject, object_data);
    }

  rut_refable_unref (prop_data->path);

  rut_shell_queue_redraw (view->context->shell);

  view->dots_dirty = TRUE;
  view->n_dots -= prop_data->path->length;

  g_slice_free (RigTransitionViewProperty, prop_data);

  rig_transition_view_queue_allocation (view);
  rig_transition_view_preferred_size_changed (view);
}

static CoglPipeline *
rig_transition_view_create_dots_pipeline (RigTransitionView *view)
{
  CoglPipeline *pipeline = cogl_pipeline_new (view->context->cogl_context);
  char *dot_filename;
  CoglBitmap *bitmap;
  CoglError *error = NULL;

  dot_filename = rut_find_data_file ("dot.png");

  if (dot_filename == NULL)
    {
      g_warning ("Couldn't find dot.png");
      bitmap = NULL;
    }
  else
    {
      bitmap = cogl_bitmap_new_from_file (view->context->cogl_context,
                                          dot_filename,
                                          &error);
      g_free (dot_filename);
    }

  if (bitmap == NULL)
    {
      g_warning ("Error loading dot.png: %s", error->message);
      cogl_error_free (error);
    }
  else
    {
      CoglTexture2D *texture =
        cogl_texture_2d_new_from_bitmap (bitmap,
                                         COGL_PIXEL_FORMAT_ANY,
                                         &error);

      if (texture == NULL)
        {
          g_warning ("Error loading dot.png: %s", error->message);
          cogl_error_free (error);
        }
      else
        {
          if (!cogl_pipeline_set_layer_point_sprite_coords_enabled (pipeline,
                                                                    0,
                                                                    TRUE,
                                                                    &error))
            {
              g_warning ("Error enabling point sprite coords: %s",
                         error->message);
              cogl_error_free (error);
              cogl_pipeline_remove_layer (pipeline, 0);
            }
          else
            {
              cogl_pipeline_set_layer_texture (pipeline,
                                               0, /* layer_num */
                                               COGL_TEXTURE (texture));
              cogl_pipeline_set_layer_filters
                (pipeline,
                 0, /* layer_num */
                 COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST,
                 COGL_PIPELINE_FILTER_LINEAR);
              cogl_pipeline_set_layer_wrap_mode
                (pipeline,
                 0, /* layer_num */
                 COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
            }

          cogl_object_unref (texture);
        }

      cogl_object_unref (bitmap);
    }

  return pipeline;
}

static void
rig_transition_view_create_separator_pipeline (RigTransitionView *view)
{
  GError *error = NULL;
  CoglTexture *texture;

  texture = rut_load_texture_from_data_file (view->context,
                                             "transition-view-separator.png",
                                             &error);

  if (texture)
    {
      CoglPipeline *pipeline = cogl_pipeline_new (view->context->cogl_context);

      view->separator_pipeline = pipeline;
      view->separator_width = cogl_texture_get_width (texture);

      cogl_pipeline_set_layer_texture (pipeline,
                                       0, /* layer_num */
                                       COGL_TEXTURE (texture));
      cogl_pipeline_set_layer_filters
        (pipeline,
         0, /* layer_num */
         COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST,
         COGL_PIPELINE_FILTER_LINEAR);
      cogl_pipeline_set_layer_wrap_mode
        (pipeline,
         0, /* layer_num */
         COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

      cogl_object_unref (texture);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }
}

static CoglPipeline *
rig_transition_view_create_progress_pipeline (RigTransitionView *view)
{
  CoglPipeline *pipeline = cogl_pipeline_new (view->context->cogl_context);

  cogl_pipeline_set_color4ub (pipeline,
                              128,
                              0,
                              0,
                              128);

  return pipeline;
}

static void
rig_transition_view_get_time_from_event (RigTransitionView *view,
                                         RutInputEvent *event,
                                         float *time,
                                         int *row)
{
  float x = rut_motion_event_get_x (event);
  float y = rut_motion_event_get_y (event);

  if (!rut_motion_event_unproject (event, view, &x, &y))
    g_error ("Failed to get inverse transform");

  if (time)
    *time = (x - view->nodes_x) / view->nodes_width;
  if (row)
    *row = nearbyintf (y / view->row_height);
}

static void
rig_transition_view_update_timeline_progress (RigTransitionView *view,
                                              RutInputEvent *event)
{
  float progress;
  rig_transition_view_get_time_from_event (view, event, &progress, NULL);
  rut_timeline_set_progress (view->timeline, progress);
  rut_shell_queue_redraw (view->context->shell);
}

static RigNode *
rig_transition_view_find_node_in_path (RigTransitionView *view,
                                       RigPath *path,
                                       float min_progress,
                                       float max_progress)
{
  RigNode *node;

  rut_list_for_each (node, &path->nodes, list_node)
    if (node->t >= min_progress && node->t <= max_progress)
      return node;

  return NULL;
}

static CoglBool
rig_transition_view_find_node (RigTransitionView *view,
                               RutInputEvent *event,
                               RigTransitionViewProperty **prop_data_out,
                               RigNode **node_out)
{
  float x = rut_motion_event_get_x (event);
  float y = rut_motion_event_get_y (event);
  float progress;
  RigTransitionViewObject *object_data;
  int row_num = 0;

  if (!rut_motion_event_unproject (event, view, &x, &y))
    {
      g_error ("Failed to get inverse transform");
      return FALSE;
    }

  progress = (x - view->nodes_x) / view->nodes_width;

  if (progress < 0.0f || progress > 1.0f)
    return FALSE;

  rut_list_for_each (object_data, &view->objects, list_node)
    {
      RigTransitionViewProperty *prop_data;

      row_num++;

      rut_list_for_each (prop_data, &object_data->properties, list_node)
        {
          if (row_num == (int) (y / view->row_height))
            {
              float scaled_dot_size =
                view->node_size / (float) view->nodes_width;
              RigNode *node;

              node =
                rig_transition_view_find_node_in_path (view,
                                                       prop_data->path,
                                                       progress -
                                                       scaled_dot_size / 2.0f,
                                                       progress +
                                                       scaled_dot_size / 2.0f);
              if (node)
                {
                  *node_out = node;
                  *prop_data_out = prop_data;
                  return TRUE;
                }

              return FALSE;
            }

          row_num++;
        }
    }

  return FALSE;
}

static void
rig_transition_view_handle_select_event (RigTransitionView *view,
                                         RutInputEvent *event)
{
  RigTransitionViewProperty *prop_data;
  RigNode *node;

  if (rig_transition_view_find_node (view, event, &prop_data, &node))
    {
      if ((rut_motion_event_get_modifier_state (event) &
           (RUT_MODIFIER_LEFT_SHIFT_ON |
            RUT_MODIFIER_RIGHT_SHIFT_ON)) == 0)
        rig_transition_view_clear_selected_nodes (view);

      /* If shift is down then we actually want to toggle the node. If
       * the node is already selected then trying to select it again
       * will return TRUE so we know to remove it. If shift wasn't
       * down then it definitely won't be selected because we'll have
       * just cleared the selection above so it doesn't matter if we
       * toggle it */
      if (rig_transition_view_select_node (view, prop_data, node))
        rig_transition_view_unselect_node (view, prop_data, node);

      rut_timeline_set_progress (view->timeline, node->t);

      rut_shell_queue_redraw (view->context->shell);
    }
  else
    {
      rig_transition_view_clear_selected_nodes (view);
      rig_transition_view_update_timeline_progress (view, event);
    }
}

static RigNode *
rig_transition_view_get_unselected_neighbour (RigTransitionView *view,
                                              RutList *head,
                                              RigNode *node,
                                              CoglBool direction)
{
  while (TRUE)
    {
      RutList *next_link;
      RigNode *next_node;
      RigTransitionViewSelectedNode *selected_node;

      if (direction)
        next_link = node->list_node.next;
      else
        next_link = node->list_node.prev;

      if (next_link == head)
        return NULL;

      next_node = rut_container_of (next_link, next_node, list_node);

      /* Ignore this node if it is also selected */
      rut_list_for_each (selected_node, &view->selected_nodes, list_node)
        {
          if (selected_node->node == next_node)
            goto node_is_selected;
        }

      return next_node;

    node_is_selected:
      node = next_node;
    }
}

static void
rig_transition_view_calculate_drag_offset_range (RigTransitionView *view)
{
  float min_drag_offset = -G_MAXFLOAT;
  float max_drag_offset = G_MAXFLOAT;
  RigTransitionViewSelectedNode *selected_node;

  /* We want to limit the range that the user can drag the selected
   * nodes to so that it won't change the order of any of the nodes */

  rut_list_for_each (selected_node, &view->selected_nodes, list_node)
    {
      RutList *node_list = &selected_node->prop_data->path->nodes;
      RigNode *node = selected_node->node, *next_node;
      float node_min, node_max;

      selected_node->original_time = node->t;

      next_node =
        rig_transition_view_get_unselected_neighbour (view,
                                                      node_list,
                                                      node,
                                                      FALSE /* direction */);

      if (next_node == NULL)
        node_min = 0.0f;
      else
        node_min = next_node->t + 0.0001;

      if (node_min > node->t)
        node_min = node->t;

      next_node =
        rig_transition_view_get_unselected_neighbour (view,
                                                      node_list,
                                                      node,
                                                      TRUE /* direction */);

      if (next_node == NULL)
        node_max = 1.0f;
      else
        node_max = next_node->t - 0.0001;

      if (node_max < node->t)
        node_max = node->t;

      if (node_min - node->t > min_drag_offset)
        min_drag_offset = node_min - node->t;
      if (node_max - node->t < max_drag_offset)
        max_drag_offset = node_max - node->t;
    }

  view->min_drag_offset = min_drag_offset;
  view->max_drag_offset = max_drag_offset;
  view->drag_offset = 0.0f;
}

static void
rig_transition_view_decide_grab_state (RigTransitionView *view,
                                       RutInputEvent *event)
{
  RigTransitionViewProperty *prop_data;
  RigNode *node;

  if ((rut_motion_event_get_modifier_state (event) &
       (RUT_MODIFIER_LEFT_SHIFT_ON |
        RUT_MODIFIER_RIGHT_SHIFT_ON)))
    {
      rig_transition_view_get_time_from_event (view,
                                               event,
                                               &view->box_x1,
                                               &view->box_y1);
      view->box_x2 = view->box_x1;
      view->box_y2 = view->box_y1;

      view->grab_state = RIG_TRANSITION_VIEW_GRAB_STATE_DRAW_BOX;
    }
  else if (rig_transition_view_find_node (view, event, &prop_data, &node))
    {
      if (!rig_transition_view_select_node (view, prop_data, node))
        {
          /* If the node wasn't already selected then we only want
           * this node to be selected */
          rig_transition_view_clear_selected_nodes (view);
          rig_transition_view_select_node (view, prop_data, node);
        }

      rig_transition_view_get_time_from_event (view,
                                               event,
                                               &view->drag_start_position,
                                               NULL);

      rig_transition_view_calculate_drag_offset_range (view);

      rut_shell_queue_redraw (view->context->shell);

      view->grab_state = RIG_TRANSITION_VIEW_GRAB_STATE_DRAGGING_NODES;
    }
  else
    {
      rig_transition_view_clear_selected_nodes (view);

      view->grab_state = RIG_TRANSITION_VIEW_GRAB_STATE_MOVING_TIMELINE;
    }
}

static void
rig_transition_view_drag_nodes (RigTransitionView *view,
                                RutInputEvent *event)
{
  RigTransitionViewSelectedNode *selected_node;
  float position;
  float offset;
  RigTransitionViewObject *object_data;

  rig_transition_view_get_time_from_event (view, event, &position, NULL);
  offset = position - view->drag_start_position;

  offset = CLAMP (offset, view->min_drag_offset, view->max_drag_offset);

  rut_list_for_each (selected_node, &view->selected_nodes, list_node)
    rig_path_move_node (selected_node->prop_data->path,
                        selected_node->node,
                        selected_node->original_time + offset);

  view->drag_offset = offset;

  /* Update all the properties that have selected nodes according to
   * the new node positions */
  rut_list_for_each (object_data, &view->objects, list_node)
    {
      RigTransitionViewProperty *prop_data;

      rut_list_for_each (prop_data, &object_data->properties, list_node)
        {
          if (prop_data->has_selected_nodes)
            rig_transition_update_property (view->transition,
                                            prop_data->property);
        }
    }
}

static void
rig_transition_view_commit_dragged_nodes (RigTransitionView *view)
{
  RigTransitionViewSelectedNode *selected_node;
  int n_nodes, i;
  RigUndoJournalPathNode *nodes;

  n_nodes = rut_list_length (&view->selected_nodes);

  nodes = g_alloca (sizeof (RigUndoJournalPathNode) * n_nodes);

  i = 0;
  rut_list_for_each (selected_node, &view->selected_nodes, list_node)
    {
      /* Reset all of the nodes to their original position so that the
       * undo journal can see it */
      selected_node->node->t = selected_node->original_time;

      nodes[i].property = selected_node->prop_data->property;
      nodes[i].node = selected_node->node;

      i++;
    }

  rig_undo_journal_move_path_nodes_and_log (view->undo_journal,
                                            view->drag_offset,
                                            nodes,
                                            n_nodes);
}

static void
rig_transition_view_update_box (RigTransitionView *view,
                                RutInputEvent *event)
{
  rig_transition_view_get_time_from_event (view,
                                           event,
                                           &view->box_x2,
                                           &view->box_y2);

  if (view->box_path)
    {
      cogl_object_unref (view->box_path);
      view->box_path = NULL;
    }

  rut_shell_queue_redraw (view->context->shell);
}

static void
rig_transition_view_commit_box (RigTransitionView *view)
{
  RigTransitionViewObject *object;
  RigTransitionViewProperty *prop_data;
  float x1, x2;
  int y1, y2;
  int row_pos = 0;

  if (view->box_x2 < view->box_x1)
    {
      x1 = view->box_x2;
      x2 = view->box_x1;
    }
  else
    {
      x1 = view->box_x1;
      x2 = view->box_x2;
    }

  if (view->box_y2 < view->box_y1)
    {
      y1 = view->box_y2;
      y2 = view->box_y1;
    }
  else
    {
      y1 = view->box_y1;
      y2 = view->box_y2;
    }

  rut_list_for_each (object, &view->objects, list_node)
    {
      row_pos++;

      rut_list_for_each (prop_data, &object->properties, list_node)
        {
          if (row_pos >= y1 && row_pos < y2)
            {
              RigNode *node;
              RigPath *path =
                rig_transition_get_path_for_property (view->transition,
                                                      prop_data->property);

              rut_list_for_each (node, &path->nodes, list_node)
                if (node->t >= x1 && node->t < x2)
                  rig_transition_view_select_node (view, prop_data, node);
            }

          row_pos++;
        }
    }

  rut_shell_queue_redraw (view->context->shell);
}

static RutInputEventStatus
rig_transition_view_grab_input_cb (RutInputEvent *event,
                                   void *user_data)
{
  RigTransitionView *view = user_data;

  if (rut_input_event_get_type (event) != RUT_INPUT_EVENT_TYPE_MOTION)
    return RUT_INPUT_EVENT_STATUS_UNHANDLED;

  if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_MOVE)
    {
      if (view->grab_state == RIG_TRANSITION_VIEW_GRAB_STATE_UNDECIDED)
        rig_transition_view_decide_grab_state (view, event);

      switch (view->grab_state)
        {
	case RIG_TRANSITION_VIEW_GRAB_STATE_NO_GRAB:
	case RIG_TRANSITION_VIEW_GRAB_STATE_UNDECIDED:
          g_assert_not_reached ();

        case RIG_TRANSITION_VIEW_GRAB_STATE_DRAGGING_NODES:
          rig_transition_view_drag_nodes (view, event);
          break;

        case RIG_TRANSITION_VIEW_GRAB_STATE_MOVING_TIMELINE:
          rig_transition_view_update_timeline_progress (view, event);
          break;

        case RIG_TRANSITION_VIEW_GRAB_STATE_DRAW_BOX:
          rig_transition_view_update_box (view, event);
          break;
        }

      return RUT_INPUT_EVENT_STATUS_HANDLED;
    }
  else if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_UP &&
           (rut_motion_event_get_button_state (event) &
            RUT_BUTTON_STATE_1) == 0)
    {
      switch (view->grab_state)
        {
	case RIG_TRANSITION_VIEW_GRAB_STATE_NO_GRAB:
          g_assert_not_reached ();
          break;

        case RIG_TRANSITION_VIEW_GRAB_STATE_MOVING_TIMELINE:
          break;

        case RIG_TRANSITION_VIEW_GRAB_STATE_UNDECIDED:
          rig_transition_view_handle_select_event (view, event);
          break;

        case RIG_TRANSITION_VIEW_GRAB_STATE_DRAGGING_NODES:
          rig_transition_view_commit_dragged_nodes (view);
          break;

        case RIG_TRANSITION_VIEW_GRAB_STATE_DRAW_BOX:
          rig_transition_view_commit_box (view);
          break;
        }

      rig_transition_view_ungrab_input (view);

      return RUT_INPUT_EVENT_STATUS_HANDLED;
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static void
rig_transition_view_delete_selected_nodes (RigTransitionView *view)
{
  if (!rut_list_empty (&view->selected_nodes))
    {
      RigUndoJournal *journal;

      /* If there is only one selected node then we'll just make a
       * single entry directly in the main undo journal. Otherwise
       * we'll create a subjournal to lump together all of the deletes
       * as one action */
      if (view->selected_nodes.next == view->selected_nodes.prev)
        journal = view->undo_journal;
      else
        journal = rig_undo_journal_new (view->undo_journal->engine);

      while (!rut_list_empty (&view->selected_nodes))
        {
          RigTransitionViewSelectedNode *node =
            rut_container_of (view->selected_nodes.next, node, list_node);

          rig_undo_journal_delete_path_node_and_log (journal,
                                                     node->prop_data->property,
                                                     node->node);
        }

      if (journal != view->undo_journal)
        rig_undo_journal_log_subjournal (view->undo_journal, journal);
    }
}

static RutInputEventStatus
rig_transition_view_input_region_cb (RutInputRegion *region,
                                     RutInputEvent *event,
                                     void *user_data)
{
  RigTransitionView *view = user_data;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_DOWN &&
          (rut_motion_event_get_button_state (event) & RUT_BUTTON_STATE_1) &&
          view->grab_state == RIG_TRANSITION_VIEW_GRAB_STATE_NO_GRAB)
        {
          /* We want to delay doing anything in response to the click
           * until the grab callback because we will do different
           * things depending on whether the next event is a move or a
           * release */
          view->grab_state = RIG_TRANSITION_VIEW_GRAB_STATE_UNDECIDED;
          rut_shell_grab_input (view->context->shell,
                                rut_input_event_get_camera (event),
                                rig_transition_view_grab_input_cb,
                                view);

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
    }
  else if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_KEY &&
           rut_key_event_get_action (event) == RUT_KEY_EVENT_ACTION_DOWN)
    {
      switch (rut_key_event_get_keysym (event))
        {
        case RUT_KEY_Delete:
          rig_transition_view_delete_selected_nodes (view);
          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static void
transition_operation_cb (RigTransition *transition,
                         RigTransitionOperation op,
                         RigTransitionPropData *prop_data,
                         void *user_data)
{
  RigTransitionView *view = user_data;

  switch (op)
    {
    case RIG_TRANSITION_OPERATION_ADDED:
      if (prop_data->animated)
        rig_transition_view_property_added (view, prop_data->property);
      break;

    case RIG_TRANSITION_OPERATION_REMOVED:
      if (prop_data->animated)
        rig_transition_view_property_removed (view, prop_data->property);
      break;

    case RIG_TRANSITION_OPERATION_ANIMATED_CHANGED:
      if (prop_data->animated)
        rig_transition_view_property_added (view, prop_data->property);
      else
        rig_transition_view_property_removed (view, prop_data->property);
      break;
    }
}

static void
rig_transition_view_add_property_cb (RigTransitionPropData *prop_data,
                                     void *user_data)
{
  RigTransitionView *view = user_data;

  if (prop_data->animated)
    rig_transition_view_property_added (view, prop_data->property);
}

RigTransitionView *
rig_transition_view_new (RutContext *ctx,
                         RutObject *graph,
                         RigTransition *transition,
                         RutTimeline *timeline,
                         RigUndoJournal *undo_journal)
{
  RigTransitionView *view = g_slice_new0 (RigTransitionView);
  static CoglBool initialized = FALSE;

  if (initialized == FALSE)
    {
      _rig_transition_view_init_type ();

      initialized = TRUE;
    }

  view->ref_count = 1;
  view->context = rut_refable_ref (ctx);
  view->graph = rut_refable_ref (graph);
  view->transition = transition;
  view->timeline = rut_refable_ref (timeline);
  view->undo_journal = undo_journal;

  rut_list_init (&view->preferred_size_cb_list);

  view->dots_dirty = TRUE;

  view->dots_pipeline = rig_transition_view_create_dots_pipeline (view);

  view->progress_pipeline = rig_transition_view_create_progress_pipeline (view);

  rig_transition_view_create_separator_pipeline (view);

  rut_object_init (&view->_parent, &rig_transition_view_type);

  rut_paintable_init (RUT_OBJECT (view));
  rut_graphable_init (RUT_OBJECT (view));

  view->input_region =
    rut_input_region_new_rectangle (0.0, 0.0, /* x0/y0 */
                                    0.0, 0.0, /* x1/y1 */
                                    rig_transition_view_input_region_cb,
                                    view);
  rut_graphable_add_child (view, view->input_region);

  rut_list_init (&view->selected_nodes);
  rut_list_init (&view->objects);

  /* Add all of the existing animated properties from the transition */
  rig_transition_foreach_property (transition,
                                   rig_transition_view_add_property_cb,
                                   view);

  /* Listen for properties that become animated or not so we can
   * update the list */
  view->transition_op_closure =
    rig_transition_add_operation_callback (transition,
                                           transition_operation_cb,
                                           view,
                                           NULL /* destroy */);

  rig_transition_view_queue_allocation (view);

  return view;
}
