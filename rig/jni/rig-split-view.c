/*
 * Rut
 *
 * Copyright (C) 2012, 2013 Intel Corporation.
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

#include <cogl/cogl.h>
#include <string.h>
#include <math.h>

#include <rut.h>

#include "rig-engine.h"
#include "rig-split-view.h"

/* The width of the area which can be clicked on to change the size of
 * the split view */
#define RIG_SPLIT_VIEW_GRABBER_SIZE 2

enum {
  RIG_SPLIT_VIEW_PROP_WIDTH,
  RIG_SPLIT_VIEW_PROP_HEIGHT,
  RIG_SPLIT_VIEW_N_PROPS
};

struct _RigSplitView
{
  RutObjectProps _parent;

  RutContext *context;
#if 0
  CoglPipeline *background_pipeline;
#endif
  CoglPipeline *split_pipeline;

  RutPaintableProps paintable;
  RutGraphableProps graphable;

  int width;
  int height;

  RigSplitViewSplit split;

  /* Most of the time we should use the floating point split fraction as the
   * basis for positioning the divider since it doesn't have to be pixel
   * aligned and so it can remain stable while resizing */
  float split_fraction;

  /* The split offset is the actual pixel aligned position of the divider but
   * because it is less precise than split_fraction we should avoid deriving
   * the split_fraction from the split_offset, except when handling a mouse
   * grab to move the divider. */
  int split_offset;

  CoglPrimitive *prim;

  RutInputRegion *input_region;

  RutRectangleInt child0_geom;
  RutRectangleInt child1_geom;
  CoglBool child0_expandable;
  CoglBool child1_expandable;

  RutTransform *child1_transform;

  RutObject *child0;
  RutObject *child1;

  int ref_count;

  RutSimpleIntrospectableProps introspectable;
  RutProperty properties[RIG_SPLIT_VIEW_N_PROPS];
};

static RutPropertySpec _rig_split_view_prop_specs[] = {
  {
    .name = "width",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RigSplitView, width),
    .setter.float_type = rig_split_view_set_width
  },
  {
    .name = "height",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RigSplitView, height),
    .setter.float_type = rig_split_view_set_height
  },
  { 0 } /* XXX: Needed for runtime counting of the number of properties */
};

RutType rig_split_view_type;

static void
set_offset_from_fraction (RigSplitView *split_view)
{
  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    split_view->split_offset = split_view->split_fraction * split_view->height;
  else if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    split_view->split_offset = split_view->split_fraction * split_view->width;
}

static void
set_fraction_from_offset (RigSplitView *split_view)
{
  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    split_view->split_fraction =
      (float)split_view->split_offset / split_view->height;
  else if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    split_view->split_fraction =
      (float)split_view->split_offset / split_view->width;
}

static void
_rig_split_view_free (void *object)
{
  RigSplitView *split_view = object;

  rut_refable_unref (split_view->context);

#if 0
  cogl_object_unref (split_view->background_pipeline);
#endif
  cogl_object_unref (split_view->split_pipeline);

  rig_split_view_set_child0 (split_view, NULL);
  rig_split_view_set_child1 (split_view, NULL);

  rut_graphable_remove_child (split_view->child1_transform);
  rut_refable_unref (split_view->child1_transform);

  rut_simple_introspectable_destroy (split_view);
  rut_graphable_destroy (split_view);

  g_slice_free (RigSplitView, split_view);
}

RutRefCountableVTable _rig_split_view_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rig_split_view_free
};

static RutGraphableVTable _rig_split_view_graphable_vtable = {
  NULL, /* child removed */
  NULL, /* child addded */
  NULL /* parent changed */
};

static void
_rig_split_view_paint (RutObject *object,
                      RutPaintContext *paint_ctx)
{
  RigSplitView *split_view = (RigSplitView *) object;
  RutCamera *camera = paint_ctx->camera;
  CoglFramebuffer *fb = rut_camera_get_framebuffer (camera);
  int split_start = split_view->split_offset - RIG_SPLIT_VIEW_GRABBER_SIZE / 2;

#if 0
  if (split_view->child0 == NULL)
    {
      RutRectangleInt *geom = &split_view->child0_geom;
      cogl_framebuffer_draw_rectangle (fb,
                                       split_view->background_pipeline,
                                       geom->x,
                                       geom->y,
                                       geom->x + geom->width,
                                       geom->y + geom->height);
    }

  if (split_view->split && split_view->child1 == NULL)
    {
      RutRectangleInt *geom = &split_view->child1_geom;
      cogl_framebuffer_draw_rectangle (fb,
                                       split_view->background_pipeline,
                                       geom->x,
                                       geom->y,
                                       geom->x + geom->width,
                                       geom->y + geom->height);
    }
#endif

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    cogl_framebuffer_draw_rectangle (fb,
                                     split_view->split_pipeline,
                                     0,
                                     split_start,
                                     split_view->width,
                                     split_start + RIG_SPLIT_VIEW_GRABBER_SIZE);
  else if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    cogl_framebuffer_draw_rectangle (fb,
                                     split_view->split_pipeline,
                                     split_view->split_offset -
                                     RIG_SPLIT_VIEW_GRABBER_SIZE / 2,
                                     0,
                                     split_start + RIG_SPLIT_VIEW_GRABBER_SIZE,
                                     split_view->height);
}

RutPaintableVTable _rig_split_view_paintable_vtable = {
  _rig_split_view_paint
};

static void
rig_split_view_get_preferred_width (void *object,
                                   float for_height,
                                   float *min_width_p,
                                   float *natural_width_p)
{
  RigSplitView *split_view = RIG_SPLIT_VIEW (object);
  float max_min_width = 0.0f;
  float max_natural_width = 0.0f;

  float for_child0_height = for_height;
  float for_child1_height = for_height;

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    {
      float split_fraction = split_view->split_offset / split_view->height;
      if (for_child0_height >= 0)
        {
          for_child0_height *= split_fraction;
          for_child1_height = for_height - for_child0_height;
        }
    }

  if (split_view->child0 &&
      rut_object_is (split_view->child0, RUT_INTERFACE_ID_SIZABLE))
    {
      rut_sizable_get_preferred_width (split_view->child0,
                                       for_child0_height,
                                       &max_min_width,
                                       &max_natural_width);
    }

  if (split_view->child1 &&
      rut_object_is (split_view->child1, RUT_INTERFACE_ID_SIZABLE))
    {
      float min_width_child1;
      float natural_width_child1;

      rut_sizable_get_preferred_width (split_view->child0,
                                       for_child1_height,
                                       &min_width_child1,
                                       &natural_width_child1);

      if (min_width_child1 > max_min_width)
        max_min_width = min_width_child1;
      if (natural_width_child1 > max_natural_width)
        max_natural_width = natural_width_child1;
    }

  if (min_width_p)
    *min_width_p = max_min_width;
  if (natural_width_p)
    *natural_width_p = max_natural_width;
}

static void
rig_split_view_get_preferred_height (void *object,
                                     float for_width,
                                     float *min_height_p,
                                     float *natural_height_p)
{
  RigSplitView *split_view = RIG_SPLIT_VIEW (object);
  float max_min_height = 0.0f;
  float max_natural_height = 0.0f;

  float for_child0_width = for_width;
  float for_child1_width = for_width;

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    {
      float split_fraction = split_view->split_offset / split_view->width;
      for_child0_width *= split_fraction;
      for_child1_width = for_width - for_child0_width;
    }

  if (split_view->child0 &&
      rut_object_is (split_view->child0, RUT_INTERFACE_ID_SIZABLE))
    {
      rut_sizable_get_preferred_height (split_view->child0,
                                        for_child0_width,
                                        &max_min_height,
                                        &max_natural_height);
    }

  if (split_view->child1 &&
      rut_object_is (split_view->child1, RUT_INTERFACE_ID_SIZABLE))
    {
      float min_height_child1;
      float natural_height_child1;

      rut_sizable_get_preferred_height (split_view->child0,
                                        for_child1_width,
                                        &min_height_child1,
                                        &natural_height_child1);

      if (min_height_child1 > max_min_height)
        max_min_height = min_height_child1;
      if (natural_height_child1 > max_natural_height)
        max_natural_height = natural_height_child1;
    }

  if (min_height_p)
    *min_height_p = max_min_height;
  if (natural_height_p)
    *natural_height_p = max_natural_height;
}

void
rig_split_view_get_size (void *object,
                         float *width,
                         float *height)
{
  RigSplitView *split_view = RIG_SPLIT_VIEW (object);

  *width = split_view->width;
  *height = split_view->height;
}

static void
update_child1_transform (RigSplitView *split_view)
{
  rut_transform_init_identity (split_view->child1_transform);

  rut_transform_translate (split_view->child1_transform,
                           split_view->child1_geom.x,
                           split_view->child1_geom.y,
                           0);
}

static void
update_child_geometry (RigSplitView *split_view)
{
  int width = split_view->width;
  int height = split_view->height;
  RutRectangleInt *geom0 = &split_view->child0_geom;
  RutRectangleInt *geom1 = &split_view->child1_geom;

  geom0->x = geom0->y = geom1->x = geom1->y = 0;
  geom0->width = geom1->width = width;
  geom0->height = geom1->height = height;

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    {
      geom0->width =
        split_view->split_offset - RIG_SPLIT_VIEW_GRABBER_SIZE / 2;
      geom1->x = geom0->width + RIG_SPLIT_VIEW_GRABBER_SIZE;
      geom1->width = width - geom1->x;
    }
  else if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    {
      geom0->height =
        split_view->split_offset - RIG_SPLIT_VIEW_GRABBER_SIZE / 2;
      geom1->y = geom0->height + RIG_SPLIT_VIEW_GRABBER_SIZE;
      geom1->height = height - geom1->y;
    }
}

static void
sync_child_sizes (RigSplitView *split_view)
{
  int split_start = split_view->split_offset - RIG_SPLIT_VIEW_GRABBER_SIZE / 2;

  if (split_view->child0 &&
      rut_object_is (split_view->child0, RUT_INTERFACE_ID_SIZABLE))
    rut_sizable_set_size (split_view->child0,
                          split_view->child0_geom.width,
                          split_view->child0_geom.height);

  if (split_view->child1)
    {
      update_child1_transform (split_view);

      if (rut_object_is (split_view->child1, RUT_INTERFACE_ID_SIZABLE))
        rut_sizable_set_size (split_view->child1,
                              split_view->child1_geom.width,
                              split_view->child1_geom.height);
    }

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    {
      rut_input_region_set_rectangle (split_view->input_region,
                                      0,
                                      split_start,
                                      split_view->width,
                                      split_start +
                                      RIG_SPLIT_VIEW_GRABBER_SIZE);
    }
  else if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    {
      rut_input_region_set_rectangle (split_view->input_region,
                                      split_start,
                                      0,
                                      split_start +
                                      RIG_SPLIT_VIEW_GRABBER_SIZE,
                                      split_view->height);
    }
}

void
set_size (RigSplitView *split_view,
          float width,
          float height)
{

  split_view->width = width;
  split_view->height = height;

  update_child_geometry (split_view);
  sync_child_sizes (split_view);
}

void
set_split_offset (RigSplitView *split_view,
                  int split_offset)
{
  split_view->split_offset = split_offset;
  set_fraction_from_offset (split_view);

  update_child_geometry (split_view);
  sync_child_sizes (split_view);
}

void
rig_split_view_set_size (RigSplitView *split_view,
                         float width,
                         float height)
{
  set_size (split_view,
            width,
            height);

  rut_property_dirty (&split_view->context->property_ctx,
                      &split_view->properties[RIG_SPLIT_VIEW_PROP_WIDTH]);
  rut_property_dirty (&split_view->context->property_ctx,
                      &split_view->properties[RIG_SPLIT_VIEW_PROP_HEIGHT]);
}

void
rig_split_view_set_width (RutObject *obj,
                          float width)
{
  RigSplitView *split_view = RIG_SPLIT_VIEW (obj);

  rig_split_view_set_size (split_view, width, split_view->height);
}

void
rig_split_view_set_height (RutObject *obj,
                           float height)
{
  RigSplitView *split_view = RIG_SPLIT_VIEW (obj);

  rig_split_view_set_size (split_view, split_view->width, height);
}

static RutSizableVTable _rig_split_view_sizable_vtable = {
  rig_split_view_set_size,
  rig_split_view_get_size,
  rig_split_view_get_preferred_width,
  rig_split_view_get_preferred_height,
  NULL /* add_preferred_size_callback */
};

static RutIntrospectableVTable _rig_split_view_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};

static void
_rig_split_view_init_type (void)
{
  rut_type_init (&rig_split_view_type, "RigSplitView");
  rut_type_add_interface (&rig_split_view_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RigSplitView, ref_count),
                          &_rig_split_view_ref_countable_vtable);
  rut_type_add_interface (&rig_split_view_type,
                          RUT_INTERFACE_ID_PAINTABLE,
                          offsetof (RigSplitView, paintable),
                          &_rig_split_view_paintable_vtable);
  rut_type_add_interface (&rig_split_view_type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (RigSplitView, graphable),
                          &_rig_split_view_graphable_vtable);
  rut_type_add_interface (&rig_split_view_type,
                          RUT_INTERFACE_ID_SIZABLE,
                          0, /* no implied properties */
                          &_rig_split_view_sizable_vtable);
  rut_type_add_interface (&rig_split_view_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &_rig_split_view_introspectable_vtable);
  rut_type_add_interface (&rig_split_view_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RigSplitView, introspectable),
                          NULL); /* no implied vtable */
}

typedef struct _GrabState
{
  RutCamera *camera;
  RigSplitView *split_view;
  CoglMatrix transform;
  CoglMatrix inverse_transform;
  float grab_x;
  float grab_y;
  int grab_offset;
} GrabState;

static void
set_resize_cursor (CoglOnscreen *onscreen,
                   RigSplitView *split_view)
{
  if (onscreen == NULL)
    return;

  switch (split_view->split)
    {
    case RIG_SPLIT_VIEW_SPLIT_NONE:
      break;

    case RIG_SPLIT_VIEW_SPLIT_HORIZONTAL:
      rut_shell_set_cursor (split_view->context->shell,
                            onscreen,
                            RUT_CURSOR_SIZE_NS);
      break;

    case RIG_SPLIT_VIEW_SPLIT_VERTICAL:
      rut_shell_set_cursor (split_view->context->shell,
                            onscreen,
                            RUT_CURSOR_SIZE_WE);
      break;
    }
}

static RutInputEventStatus
_rig_split_view_grab_input_cb (RutInputEvent *event,
                               void *user_data)
{
  GrabState *state = user_data;
  RigSplitView *split_view = state->split_view;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      RutShell *shell = split_view->context->shell;
      if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_UP)
        {
          rut_shell_ungrab_input (shell, _rig_split_view_grab_input_cb, user_data);
          g_slice_free (GrabState, state);

          rut_shell_queue_redraw (split_view->context->shell);

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
      else if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_MOVE)
        {
          float x = rut_motion_event_get_x (event);
          float y = rut_motion_event_get_y (event);

          set_resize_cursor (rut_input_event_get_onscreen (event),
                             split_view);

          if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
            {
              float dy = y - state->grab_y;
              int offset = state->grab_offset + dy;
              set_split_offset (split_view, offset);
            }
          else if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
            {
              float dx = x - state->grab_x;
              int offset = state->grab_offset + dx;
              set_split_offset (split_view, offset);
            }

          rut_shell_queue_redraw (split_view->context->shell);

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static RutInputEventStatus
_rig_split_view_input_cb (RutInputRegion *region,
                          RutInputEvent *event,
                          void *user_data)
{
  RigSplitView *split_view = user_data;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      set_resize_cursor (rut_input_event_get_onscreen (event),
                         split_view);

      if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_DOWN)
        {
          RutShell *shell = split_view->context->shell;
          GrabState *state = g_slice_new (GrabState);
          const CoglMatrix *view;

          state->split_view = split_view;
          state->camera = rut_input_event_get_camera (event);
          state->grab_x = rut_motion_event_get_x (event);
          state->grab_y = rut_motion_event_get_y (event);
          state->grab_offset = split_view->split_offset;

          view = rut_camera_get_view_transform (state->camera);
          state->transform = *view;
          rut_graphable_apply_transform (split_view, &state->transform);
          if (!cogl_matrix_get_inverse (&state->transform,
                                        &state->inverse_transform))
            {
              g_warning ("Failed to calculate inverse of split_view "
                         "transform\n");
              g_slice_free (GrabState, state);
              return RUT_INPUT_EVENT_STATUS_UNHANDLED;
            }

          rut_shell_grab_input (shell,
                                state->camera,
                                _rig_split_view_grab_input_cb,
                                state);

          rut_shell_queue_redraw (split_view->context->shell);

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

RigSplitView *
rig_split_view_new (RigEngine *engine,
                    RigSplitViewSplit split,
                    float width,
                    float height)
{
  RutContext *context = engine->ctx;
  RigSplitView *split_view = g_slice_new0 (RigSplitView);
  static CoglBool initialized = FALSE;

  if (initialized == FALSE)
    {
      _rut_init ();
      _rig_split_view_init_type ();

      initialized = TRUE;
    }

  rut_object_init (&split_view->_parent, &rig_split_view_type);

  rut_simple_introspectable_init (split_view,
                                  _rig_split_view_prop_specs,
                                  split_view->properties);

  rut_paintable_init (RUT_OBJECT (split_view));
  rut_graphable_init (RUT_OBJECT (split_view));

  split_view->ref_count = 1;

  split_view->context = rut_refable_ref (context);

#if 0
  split_view->background_pipeline = cogl_pipeline_new (context->cogl_context);
  cogl_pipeline_set_color4ub (split_view->background_pipeline,
                              64, 64, 128, 128);
#endif

  split_view->split_pipeline = cogl_pipeline_new (context->cogl_context);
  cogl_pipeline_set_color4f (split_view->split_pipeline,
                             0, 0, 0, 1);

  split_view->width = width;
  split_view->height = height;

  split_view->child1_transform = rut_transform_new (context);
  rut_graphable_add_child (split_view, split_view->child1_transform);

  split_view->input_region =
    rut_input_region_new_rectangle (0, 0, 100, 100,
                                    _rig_split_view_input_cb,
                                    split_view);

  if (split)
    rig_split_view_split (split_view, split);

  return split_view;
}

void
rig_split_view_set_split_offset (RigSplitView *split_view,
                                 int offset)
{
  g_return_if_fail (split_view->split != 0);

  set_split_offset (split_view, offset);
}

void
rig_split_view_split (RigSplitView *split_view, RigSplitViewSplit split)
{
  if (split_view->split)
    {
      g_warning ("Can't split split-view multiple times");
      return;
    }

  split_view->split = split;

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    split_view->split_offset = split_view->height / 2;
  else if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    split_view->split_offset = split_view->width / 2;
  set_fraction_from_offset (split_view);

  rut_graphable_add_child (split_view, split_view->input_region);
}

void
rig_split_view_join (RigSplitView *split_view, RigSplitViewJoin join)
{
  if (split_view->split == 0)
    return;

  if (split_view->child1)
    {
      rut_graphable_remove_child (split_view->child1_transform);
      rut_refable_unref (split_view->child1);
    }

  split_view->split = 0;

  rut_graphable_remove_child (split_view->input_region);

  set_size (split_view,
            split_view->width,
            split_view->height);
}

void
rig_split_view_set_child0 (RigSplitView *split_view,
                           RutObject *child0)
{
  if (split_view->child0 == child0)
    return;

  if (split_view->child0)
    {
      rut_graphable_remove_child (split_view->child0);
      rut_refable_unref (split_view->child0);
    }

  if (child0)
    {
      rut_graphable_add_child (split_view, child0);
      rut_refable_ref (child0);
    }

  split_view->child0 = child0;
}

void
rig_split_view_set_child1 (RigSplitView *split_view,
                           RutObject *child1)
{
  if (split_view->child1 == child1)
    return;

  if (split_view->child1)
    {
      rut_graphable_remove_child (split_view->child1);
      rut_refable_unref (split_view->child1);
    }

  if (child1)
    {
      rut_graphable_add_child (split_view->child1_transform, child1);
      rut_refable_ref (child1);
    }

  split_view->child1 = child1;
}

void
rig_split_view_set_child0_expandable (RigSplitView *split_view,
                                      CoglBool expandable)
{
  split_view->child0_expandable = expandable;
}

void
rig_split_view_set_child1_expandable (RigSplitView *split_view,
                                      CoglBool expandable)
{
  split_view->child1_expandable = expandable;
}

