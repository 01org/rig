/*
 * Rut
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

#include "rut-context.h"
#include "rut-shell.h"
#include "rut-interfaces.h"
#include "rut-paintable.h"
#include "rut-camera-private.h"
#include "rut-scroll-bar.h"
#include "rut-color.h"

#define THICKNESS 20
#define HANDLE_THICKNESS 15

enum {
  RUT_SCROLL_BAR_PROP_LENGTH,
  RUT_SCROLL_BAR_PROP_VIRTUAL_LENGTH,
  RUT_SCROLL_BAR_PROP_VIRTUAL_VIEWPORT,
  RUT_SCROLL_BAR_PROP_VIRTUAL_OFFSET,
  RUT_SCROLL_BAR_N_PROPS
};

struct _RutScrollBar
{
  RutObjectProps _parent;
  int ref_count;

  RutContext *ctx;

  CoglColor color;

  int thickness;

  float trough_range;

  CoglPipeline *rounded_pipeline;
  CoglPipeline *rect_pipeline;
  float handle_len;
  float handle_pos;

  RutGraphableProps graphable;
  RutPaintableProps paintable;

  RutInputRegion *input_region;

  RutAxis axis;
  float length;
  float virtual_length;
  float viewport_length;
  float offset;

  float grab_x;
  float grab_y;
  float grab_offset;

  RutSimpleIntrospectableProps introspectable;
  RutProperty properties[RUT_SCROLL_BAR_N_PROPS];
};

static RutPropertySpec _rut_scroll_bar_prop_specs[] = {
  {
    .name = "length",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutScrollBar, length),
    .setter.float_type = rut_scroll_bar_set_length
  },
  {
    .name = "virtual_length",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutScrollBar, virtual_length),
    .setter.float_type = rut_scroll_bar_set_virtual_length
  },
  {
    .name = "virtual_viewport",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutScrollBar, viewport_length),
    .setter.float_type = rut_scroll_bar_set_virtual_viewport
  },
  {
    .name = "virtual_offset",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutScrollBar, offset),
    .setter.float_type = rut_scroll_bar_set_virtual_offset
  },


  { 0 } /* XXX: Needed for runtime counting of the number of properties */
};

static void
_rut_scroll_bar_free (void *object)
{
  RutScrollBar *scroll_bar = object;

  rut_graphable_destroy (scroll_bar);

  g_slice_free (RutScrollBar, object);
}

static RutRefCountableVTable _rut_scroll_bar_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_scroll_bar_free
};

static void
_rut_scroll_bar_paint (RutObject *object, RutPaintContext *paint_ctx)
{
  RutScrollBar *scroll_bar = RUT_SCROLL_BAR (object);

  cogl_framebuffer_push_matrix (paint_ctx->camera->fb);

  if (scroll_bar->axis == RUT_AXIS_X)
    {
      cogl_framebuffer_translate (paint_ctx->camera->fb,
                                  scroll_bar->handle_pos, 0, 0);
    }
  else
    {
      cogl_framebuffer_translate (paint_ctx->camera->fb, 0,
                                  scroll_bar->handle_pos, 0);
      cogl_framebuffer_draw_rectangle (paint_ctx->camera->fb,
                                       scroll_bar->rounded_pipeline,
                                       0, 0,
                                       HANDLE_THICKNESS,
                                       HANDLE_THICKNESS);

      cogl_framebuffer_draw_rectangle (paint_ctx->camera->fb,
                                       scroll_bar->rounded_pipeline,
                                       0,
                                       scroll_bar->handle_len - HANDLE_THICKNESS,
                                       HANDLE_THICKNESS,
                                       scroll_bar->handle_len);

      cogl_framebuffer_draw_textured_rectangle (paint_ctx->camera->fb,
                                                scroll_bar->rounded_pipeline,
                                                0,
                                                HANDLE_THICKNESS / 2,
                                                HANDLE_THICKNESS,
                                                scroll_bar->handle_len - HANDLE_THICKNESS / 2,
                                                0, 0.5, 1, 0.5);
    }

  cogl_framebuffer_pop_matrix (paint_ctx->camera->fb);
}

static RutPaintableVTable _rut_scroll_bar_paintable_vtable = {
  _rut_scroll_bar_paint
};

RutGraphableVTable _rut_scroll_bar_graphable_vtable = {
  NULL, /* child remove */
  NULL, /* child add */
  NULL /* parent changed */
};

static RutIntrospectableVTable _rut_scroll_bar_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};

RutType rut_scroll_bar_type;

void
_rut_scroll_bar_init_type (void)
{
  rut_type_init (&rut_scroll_bar_type, "RigScrollBar");
  rut_type_add_interface (&rut_scroll_bar_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutScrollBar, ref_count),
                          &_rut_scroll_bar_ref_countable_vtable);
  rut_type_add_interface (&rut_scroll_bar_type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (RutScrollBar, graphable),
                          &_rut_scroll_bar_graphable_vtable);
  rut_type_add_interface (&rut_scroll_bar_type,
                          RUT_INTERFACE_ID_PAINTABLE,
                          offsetof (RutScrollBar, paintable),
                          &_rut_scroll_bar_paintable_vtable);
  rut_type_add_interface (&rut_scroll_bar_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &_rut_scroll_bar_introspectable_vtable);
  rut_type_add_interface (&rut_scroll_bar_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RutScrollBar, introspectable),
                          NULL); /* no implied vtable */
}

static RutInputEventStatus
_rut_scroll_bar_grab_input_cb (RutInputEvent *event,
                               void *user_data)
{
  RutScrollBar *scroll_bar = user_data;

  if(rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      RutShell *shell = scroll_bar->ctx->shell;
      if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_UP)
        {
          rut_shell_ungrab_input (shell,
                                  _rut_scroll_bar_grab_input_cb,
                                  user_data);
          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
      else if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_MOVE)
        {
          float diff;
          float offset;

          if (scroll_bar->axis == RUT_AXIS_X)
            diff = rut_motion_event_get_x (event) - scroll_bar->grab_x;
          else
            diff = rut_motion_event_get_y (event) - scroll_bar->grab_y;

          diff = (diff / scroll_bar->trough_range) *
            (scroll_bar->virtual_length - scroll_bar->viewport_length);

          offset = scroll_bar->grab_offset + diff;

          rut_scroll_bar_set_virtual_offset (scroll_bar, offset);

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static RutInputEventStatus
_rut_scroll_bar_input_cb (RutInputRegion *region,
                          RutInputEvent *event,
                          void *user_data)
{
  RutScrollBar *scroll_bar = user_data;
  float x, y;
  float pos;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      if (!rut_motion_event_unproject (event, scroll_bar, &x, &y))
        return RUT_INPUT_EVENT_STATUS_UNHANDLED;

      if (scroll_bar->axis == RUT_AXIS_X)
        pos = x;
      else
        pos = y;

      if (pos > scroll_bar->handle_pos &&
          pos < scroll_bar->handle_pos + scroll_bar->handle_len)
        {
          if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_DOWN)
            {
              scroll_bar->grab_x = rut_motion_event_get_x (event);
              scroll_bar->grab_y = rut_motion_event_get_y (event);
              scroll_bar->grab_offset = scroll_bar->offset;

              rut_shell_grab_input (scroll_bar->ctx->shell,
                                    rut_input_event_get_camera (event),
                                    _rut_scroll_bar_grab_input_cb,
                                    scroll_bar);
            }
        }
      else if (pos < scroll_bar->handle_pos)
        {
          //g_print ("Scroll Bar: In front of handle\n");
        }
      else
        {
          //g_print ("Scroll Bar: After handle\n");
        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static void
rut_scroll_bar_get_size (RutScrollBar *scroll_bar,
                         float *width,
                         float *height)
{
  if (scroll_bar->axis == RUT_AXIS_X)
    {
      *width = scroll_bar->length;
      *height = scroll_bar->thickness;
    }
  else
    {
      *width = scroll_bar->thickness;
      *height = scroll_bar->length;
    }
}

static void
update_handle_position (RutScrollBar *scroll_bar)
{
  scroll_bar->handle_pos = (scroll_bar->offset /
     (scroll_bar->virtual_length - scroll_bar->viewport_length)) *
    scroll_bar->trough_range;

  rut_shell_queue_redraw (scroll_bar->ctx->shell);
}

static void
update_handle_length (RutScrollBar *scroll_bar)
{
  float handle_len;

  handle_len = (scroll_bar->viewport_length /
                scroll_bar->virtual_length) * scroll_bar->length;
  handle_len = MAX (scroll_bar->thickness, handle_len);

  scroll_bar->handle_len = handle_len;

  /* The trough range is the range of motion for the handle taking into account
   * that the handle size might not reflect the relative size of the viewport
   * if it was clamped to a larger size.
   *
   * The trough_range maps to (virtual_length - viewport_length)
   */
  scroll_bar->trough_range = scroll_bar->length - scroll_bar->handle_len;
}

static void
update_geometry (RutScrollBar *scroll_bar)
{
  float width, height;

  rut_scroll_bar_get_size (scroll_bar, &width, &height);

  rut_input_region_set_rectangle (scroll_bar->input_region,
                                  0, 0, width, height);

  update_handle_length (scroll_bar);

  update_handle_position (scroll_bar);

  rut_shell_queue_redraw (scroll_bar->ctx->shell);
}

RutScrollBar *
rut_scroll_bar_new (RutContext *ctx,
                    RutAxis axis,
                    float length,
                    float virtual_length,
                    float viewport_length)
{
  RutScrollBar *scroll_bar = g_slice_new0 (RutScrollBar);

  rut_object_init (&scroll_bar->_parent, &rut_scroll_bar_type);

  scroll_bar->ref_count = 1;

  rut_simple_introspectable_init (scroll_bar,
                                  _rut_scroll_bar_prop_specs,
                                  scroll_bar->properties);

  scroll_bar->ctx = ctx;

  rut_graphable_init (RUT_OBJECT (scroll_bar));
  rut_paintable_init (RUT_OBJECT (scroll_bar));

  scroll_bar->axis = axis;
  scroll_bar->length = length;
  scroll_bar->virtual_length = virtual_length;
  scroll_bar->viewport_length = viewport_length;
  scroll_bar->offset = 0;

  rut_color_init_from_uint32 (&scroll_bar->color, 0x919191ff);

  scroll_bar->thickness = THICKNESS;

  scroll_bar->rect_pipeline = cogl_pipeline_new (ctx->cogl_context);
  cogl_pipeline_set_color (scroll_bar->rect_pipeline,
                           &scroll_bar->color);

  scroll_bar->rounded_pipeline = cogl_pipeline_copy (scroll_bar->rect_pipeline);
  cogl_pipeline_set_layer_texture (scroll_bar->rounded_pipeline,
                                   0,
                                   ctx->circle_texture);

  scroll_bar->input_region =
    rut_input_region_new_rectangle (0, 0, 1, 1,
                                    _rut_scroll_bar_input_cb,
                                    scroll_bar);
  rut_graphable_add_child (scroll_bar, scroll_bar->input_region);

  update_geometry (scroll_bar);

  return scroll_bar;
}

void
rut_scroll_bar_set_length (RutObject *obj,
                           float length)
{
  RutScrollBar *scroll_bar = RUT_SCROLL_BAR (obj);

  if (scroll_bar->length == length)
    return;

  scroll_bar->length = length;

  update_geometry (scroll_bar);
}

static float
clamp_offset (RutScrollBar *scroll_bar,
              float offset)
{
  if (offset + scroll_bar->viewport_length > scroll_bar->virtual_length)
    offset = scroll_bar->virtual_length - scroll_bar->viewport_length;

  if (offset < 0.0f)
    offset = 0.0f;

  return offset;
}

static void
reclamp_offset (RutScrollBar *scroll_bar)
{
  float offset = clamp_offset (scroll_bar, scroll_bar->offset);

  if (offset != scroll_bar->offset)
    {
      RutProperty *property =
        &scroll_bar->properties[RUT_SCROLL_BAR_PROP_VIRTUAL_OFFSET];

      scroll_bar->offset = offset;

      rut_property_dirty (&scroll_bar->ctx->property_ctx,
                          property);
    }
}

void
rut_scroll_bar_set_virtual_length (RutObject *obj,
                                   float virtual_length)
{
  RutScrollBar *scroll_bar = RUT_SCROLL_BAR (obj);

  if (scroll_bar->virtual_length == virtual_length)
    return;

  scroll_bar->virtual_length = virtual_length;

  reclamp_offset (scroll_bar);
  update_handle_length (scroll_bar);
  update_handle_position (scroll_bar);
}

void
rut_scroll_bar_set_virtual_viewport (RutObject *obj,
                                     float viewport_length)
{
  RutScrollBar *scroll_bar = RUT_SCROLL_BAR (obj);

  if (scroll_bar->viewport_length == viewport_length)
    return;

  scroll_bar->viewport_length = viewport_length;

  reclamp_offset (scroll_bar);
  update_handle_length (scroll_bar);
  update_handle_position (scroll_bar);
}

void
rut_scroll_bar_set_virtual_offset (RutObject *obj,
                                   float viewport_offset)
{
  RutScrollBar *scroll_bar = RUT_SCROLL_BAR (obj);

  viewport_offset = clamp_offset (scroll_bar, viewport_offset);

  if (scroll_bar->offset == viewport_offset)
    return;

  scroll_bar->offset = viewport_offset;

  update_handle_position (scroll_bar);

  rut_property_dirty (&scroll_bar->ctx->property_ctx,
                      &scroll_bar->properties[RUT_SCROLL_BAR_PROP_VIRTUAL_OFFSET]);
}

float
rut_scroll_bar_get_virtual_offset (RutScrollBar *scroll_bar)
{
  return scroll_bar->offset;
}

float
rut_scroll_bar_get_virtual_viewport (RutScrollBar *scroll_bar)
{
  return scroll_bar->viewport_length;
}

float
rut_scroll_bar_get_thickness (RutScrollBar *scroll_bar)
{
  return scroll_bar->thickness;
}

void
rut_scroll_bar_set_color (RutScrollBar *scroll_bar,
                          const CoglColor *color)
{
  scroll_bar->color = *color;
}
