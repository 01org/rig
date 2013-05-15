/*
 * Rut
 *
 * A tiny toolkit
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

#include "rut-context.h"
#include "rut-interfaces.h"
#include "rut-ui-viewport.h"
#include "rut-scroll-bar.h"
#include "rut-transform.h"

enum {
  RUT_UI_VIEWPORT_PROP_WIDTH,
  RUT_UI_VIEWPORT_PROP_HEIGHT,
  RUT_UI_VIEWPORT_PROP_DOC_WIDTH,
  RUT_UI_VIEWPORT_PROP_DOC_HEIGHT,
  RUT_UI_VIEWPORT_PROP_DOC_X,
  RUT_UI_VIEWPORT_PROP_DOC_Y,
  RUT_UI_VIEWPORT_PROP_SYNC_WIDGET,
  RUT_UI_VIEWPORT_PROP_X_PANNABLE,
  RUT_UI_VIEWPORT_PROP_Y_PANNABLE,
  RUT_UI_VIEWPORT_N_PROPS
};

struct _RutUIViewport
{
  RutObjectProps _parent;

  RutContext *ctx;

  int ref_count;

  RutGraphableProps graphable;

  float width;
  float height;

  float doc_width;
  float doc_height;
  float doc_scale_x;
  float doc_scale_y;

  RutObject *sync_widget;
  RutClosure *sync_widget_preferred_size_closure;
  RutList preferred_size_cb_list;

  CoglBool x_pannable;
  CoglBool y_pannable;

  RutTransform *scroll_bar_x_transform;
  RutScrollBar *scroll_bar_x;
  CoglBool scroll_bar_x_visible;
  RutTransform *scroll_bar_y_transform;
  RutScrollBar *scroll_bar_y;
  CoglBool scroll_bar_y_visible;

  RutTransform *doc_transform;

  float grab_x;
  float grab_y;
  float grab_doc_x;
  float grab_doc_y;

  RutSimpleIntrospectableProps introspectable;
  RutProperty properties[RUT_UI_VIEWPORT_N_PROPS];

  RutInputableProps inputable;
};

static RutPropertySpec _rut_ui_viewport_prop_specs[] = {
  {
    .name = "width",
    .flags = RUT_PROPERTY_FLAG_READABLE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutUIViewport, width),
  },
  {
    .name = "height",
    .flags = RUT_PROPERTY_FLAG_READABLE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutUIViewport, height),
  },
  {
    .name = "doc-width",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutUIViewport, doc_width),
    .setter.float_type = rut_ui_viewport_set_doc_width
  },
  {
    .name = "doc-height",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutUIViewport, doc_height),
    .setter.float_type = rut_ui_viewport_set_doc_height
  },
  {
    .name = "doc-x",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_ui_viewport_get_doc_x,
    .setter.float_type = rut_ui_viewport_set_doc_x
  },
  {
    .name = "doc-y",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .getter.float_type = rut_ui_viewport_get_doc_y,
    .setter.float_type = rut_ui_viewport_set_doc_y
  },
  {
    .name = "sync-widget",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_OBJECT,
    .data_offset = offsetof (RutUIViewport, sync_widget),
    .setter.object_type = rut_ui_viewport_set_sync_widget
  },
  {
    .name = "x-pannable",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .data_offset = offsetof (RutUIViewport, x_pannable),
    .getter.boolean_type = rut_ui_viewport_get_x_pannable,
    .setter.boolean_type = rut_ui_viewport_set_x_pannable
  },
  {
    .name = "y-pannable",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_BOOLEAN,
    .data_offset = offsetof (RutUIViewport, y_pannable),
    .getter.boolean_type = rut_ui_viewport_get_y_pannable,
    .setter.boolean_type = rut_ui_viewport_set_y_pannable
  },

  { 0 } /* XXX: Needed for runtime counting of the number of properties */
};

static void
_rut_ui_viewport_set_size (RutObject *object,
                           float width,
                           float height);

static void
_rut_ui_viewport_get_size (RutObject *object,
                           float *width,
                           float *height);

static void
_rut_ui_viewport_free (void *object)
{
  RutUIViewport *ui_viewport = object;

  rut_ui_viewport_set_sync_widget (ui_viewport, NULL);

  rut_closure_list_disconnect_all (&ui_viewport->preferred_size_cb_list);

  rut_refable_unref (ui_viewport->doc_transform);

  rut_refable_unref (ui_viewport->inputable.input_region);

  rut_simple_introspectable_destroy (ui_viewport);
  rut_graphable_destroy (ui_viewport);

  g_slice_free (RutUIViewport, object);
}

static RutRefCountableVTable _rut_ui_viewport_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_ui_viewport_free
};

static RutGraphableVTable _rut_ui_viewport_graphable_vtable = {
  NULL, /* child_removed */
  NULL, /* child_added */
  NULL, /* parent_changed */
};

static void
rut_ui_viewport_get_preferred_width (void *sizable,
                                     float for_height,
                                     float *min_width_p,
                                     float *natural_width_p)
{
  RutUIViewport *ui_viewport = sizable;

  if (min_width_p)
    *min_width_p = 0;

  if (ui_viewport->sync_widget)
    {
      rut_sizable_get_preferred_width (ui_viewport->sync_widget,
                                       for_height,
                                       NULL,
                                       natural_width_p);
    }
  else
    {
      if (natural_width_p)
        *natural_width_p = ui_viewport->doc_width;
    }
}

static void
rut_ui_viewport_get_preferred_height (void *sizable,
                                      float for_width,
                                      float *min_height_p,
                                      float *natural_height_p)
{
  RutUIViewport *ui_viewport = sizable;

  if (min_height_p)
    *min_height_p = 0;

  if (ui_viewport->sync_widget)
    {
      rut_sizable_get_preferred_height (ui_viewport->sync_widget,
                                        for_width,
                                        NULL,
                                        natural_height_p);
    }
  else
    {
      if (natural_height_p)
        *natural_height_p = ui_viewport->doc_height;
    }
}

static RutClosure *
rut_ui_viewport_add_preferred_size_callback (void *object,
                                             RutSizablePreferredSizeCallback cb,
                                             void *user_data,
                                             RutClosureDestroyCallback destroy)
{
  RutUIViewport *ui_viewport = object;

  return rut_closure_list_add (&ui_viewport->preferred_size_cb_list,
                               cb,
                               user_data,
                               destroy);
}

static RutSizableVTable _rut_ui_viewport_sizable_vtable = {
  _rut_ui_viewport_set_size,
  _rut_ui_viewport_get_size,
  rut_ui_viewport_get_preferred_width,
  rut_ui_viewport_get_preferred_height,
  rut_ui_viewport_add_preferred_size_callback
};

static RutIntrospectableVTable _rut_ui_viewport_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};

RutType rut_ui_viewport_type;

void
_rut_ui_viewport_init_type (void)
{
  rut_type_init (&rut_ui_viewport_type, "RigUiViewport");
  rut_type_add_interface (&rut_ui_viewport_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutUIViewport, ref_count),
                          &_rut_ui_viewport_ref_countable_vtable);
  rut_type_add_interface (&rut_ui_viewport_type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (RutUIViewport, graphable),
                          &_rut_ui_viewport_graphable_vtable);
  rut_type_add_interface (&rut_ui_viewport_type,
                          RUT_INTERFACE_ID_SIZABLE,
                          0, /* no implied properties */
                          &_rut_ui_viewport_sizable_vtable);
  rut_type_add_interface (&rut_ui_viewport_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &_rut_ui_viewport_introspectable_vtable);
  rut_type_add_interface (&rut_ui_viewport_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RutUIViewport, introspectable),
                          NULL); /* no implied vtable */
  rut_type_add_interface (&rut_ui_viewport_type,
                          RUT_INTERFACE_ID_INPUTABLE,
                          offsetof (RutUIViewport, inputable),
                          NULL); /* no implied vtable */
}

static void
_rut_ui_viewport_update_doc_matrix (RutUIViewport *ui_viewport)
{
  float doc_x = rut_ui_viewport_get_doc_x (ui_viewport);
  float doc_y = rut_ui_viewport_get_doc_y (ui_viewport);

  /* Align the translation to a pixel if the scale is 1 so that it
   * won't needlessly start misaligning text */
  if (ui_viewport->doc_scale_x == 1.0f)
    doc_x = nearbyintf (doc_x);
  if (ui_viewport->doc_scale_y == 1.0f)
    doc_y = nearbyintf (doc_y);

  rut_transform_init_identity (ui_viewport->doc_transform);
  rut_transform_translate (ui_viewport->doc_transform,
                           -doc_x,
                           -doc_y,
                           0);
  rut_transform_scale (ui_viewport->doc_transform,
                       ui_viewport->doc_scale_x,
                       ui_viewport->doc_scale_y,
                       1);
}

static RutInputEventStatus
ui_viewport_grab_input_cb (RutInputEvent *event, void *user_data)
{
  RutUIViewport *ui_viewport = user_data;

  if (rut_input_event_get_type (event) != RUT_INPUT_EVENT_TYPE_MOTION)
    return RUT_INPUT_EVENT_STATUS_UNHANDLED;

  if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_MOVE)
    {
      RutButtonState state = rut_motion_event_get_button_state (event);

      if (state & RUT_BUTTON_STATE_2)
        {
          float x = rut_motion_event_get_x (event);
          float y = rut_motion_event_get_y (event);
          float dx = x - ui_viewport->grab_x;
          float dy = y - ui_viewport->grab_y;
          float x_scale = rut_ui_viewport_get_doc_scale_x (ui_viewport);
          float y_scale = rut_ui_viewport_get_doc_scale_y (ui_viewport);
          float inv_x_scale = 1.0 / x_scale;
          float inv_y_scale = 1.0 / y_scale;

          if (ui_viewport->x_pannable)
            rut_ui_viewport_set_doc_x (ui_viewport,
                                       ui_viewport->grab_doc_x +
                                       (dx * inv_x_scale));

          if (ui_viewport->y_pannable)
            rut_ui_viewport_set_doc_y (ui_viewport,
                                       ui_viewport->grab_doc_y +
                                       (dy * inv_y_scale));

          rut_shell_queue_redraw (ui_viewport->ctx->shell);
          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
    }
  else if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_UP)
    {
      rut_shell_ungrab_input (ui_viewport->ctx->shell,
                              ui_viewport_grab_input_cb,
                              user_data);
      return RUT_INPUT_EVENT_STATUS_HANDLED;
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static RutInputEventStatus
_rut_ui_viewport_input_cb (RutInputEvent *event,
                           void *user_data)
{
  RutUIViewport *ui_viewport = user_data;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      switch (rut_motion_event_get_action (event))
        {
        case RUT_MOTION_EVENT_ACTION_DOWN:
          {
            RutButtonState state = rut_motion_event_get_button_state (event);

            if (state & RUT_BUTTON_STATE_2)
              {
                ui_viewport->grab_x = rut_motion_event_get_x (event);
                ui_viewport->grab_y = rut_motion_event_get_y (event);

                ui_viewport->grab_doc_x = rut_ui_viewport_get_doc_x (ui_viewport);
                ui_viewport->grab_doc_y = rut_ui_viewport_get_doc_y (ui_viewport);

                /* TODO: Add rut_shell_implicit_grab_input() that handles releasing
                 * the grab for you */
                rut_shell_grab_input (ui_viewport->ctx->shell,
                                      rut_input_event_get_camera (event),
                                      ui_viewport_grab_input_cb,
                                      ui_viewport);
                return RUT_INPUT_EVENT_STATUS_HANDLED;
              }
          }
	default:
	  break;
        }
    }
  else if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_KEY &&
           rut_key_event_get_action (event) == RUT_KEY_EVENT_ACTION_DOWN)
    {
      switch (rut_key_event_get_keysym (event))
        {
        case RUT_KEY_Page_Up:
          if (ui_viewport->y_pannable)
            {
              float viewport =
                rut_scroll_bar_get_virtual_viewport (ui_viewport->scroll_bar_y);
              float old_y =
                rut_scroll_bar_get_virtual_offset (ui_viewport->scroll_bar_y);

              rut_scroll_bar_set_virtual_offset (ui_viewport->scroll_bar_y,
                                                 old_y - viewport);
            }
          break;
        case RUT_KEY_Page_Down:
          if (ui_viewport->y_pannable)
            {
              float viewport =
                rut_scroll_bar_get_virtual_viewport (ui_viewport->scroll_bar_y);
              float old_y =
                rut_scroll_bar_get_virtual_offset (ui_viewport->scroll_bar_y);

              rut_scroll_bar_set_virtual_offset (ui_viewport->scroll_bar_y,
                                                 old_y + viewport);
            }
          break;
        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static RutInputEventStatus
_rut_ui_viewport_input_region_cb (RutInputRegion *region,
                                  RutInputEvent *event,
                                  void *user_data)
{
  return _rut_ui_viewport_input_cb (event, user_data);
}

static void
get_sync_widget_size_for_allocation (RutUIViewport *ui_viewport,
                                     float allocation_width,
                                     float allocation_height,
                                     float *doc_width,
                                     float *doc_height)
{
  if (!ui_viewport->x_pannable)
    {
      *doc_width = allocation_width;
      rut_sizable_get_preferred_height (ui_viewport->sync_widget,
                                        allocation_width, /* for_width */
                                        NULL, /* min_height */
                                        doc_height);
    }
  else if (!ui_viewport->y_pannable)
    {
      rut_sizable_get_preferred_width (ui_viewport->sync_widget,
                                       allocation_height,
                                       NULL, /* min_width */
                                       doc_width);
      *doc_height = allocation_height;
    }
  else
    {
      rut_sizable_get_preferred_width (ui_viewport->sync_widget,
                                       allocation_width,
                                       NULL, /* min_width */
                                       doc_width);
      rut_sizable_get_preferred_height (ui_viewport->sync_widget,
                                        *doc_width, /* for_width */
                                        NULL, /* min_height */
                                        doc_height);
    }
}

static void
allocate_cb (RutObject *graphable,
              void *user_data)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (graphable);
  CoglBool need_scroll_bar_x, need_scroll_bar_y;
  float x_scroll_bar_thickness, y_scroll_bar_thickness;
  float viewport_width = ui_viewport->width;
  float viewport_height = ui_viewport->height;
  float doc_width, doc_height;

  /* If there is a sync widget then the document size will be directly
   * taken from the widget's preferred size */
  if (ui_viewport->sync_widget)
    {
      get_sync_widget_size_for_allocation (ui_viewport,
                                           viewport_width,
                                           viewport_height,
                                           &doc_width,
                                           &doc_height);
    }
  else
    {
      doc_width = ui_viewport->doc_width;
      doc_height = ui_viewport->doc_height;
    }

  x_scroll_bar_thickness =
    rut_scroll_bar_get_thickness (ui_viewport->scroll_bar_x);
  y_scroll_bar_thickness =
    rut_scroll_bar_get_thickness (ui_viewport->scroll_bar_y);

  need_scroll_bar_y = (ui_viewport->y_pannable &&
                       viewport_height < doc_height * ui_viewport->doc_scale_y);

  if (need_scroll_bar_y)
    {
      viewport_width -= y_scroll_bar_thickness;

      /* If the sync widget is being expanded to fit the width of the
       * viewport then we we need to re-sync the document size now
       * that the width has changed... */
      if (ui_viewport->sync_widget && !ui_viewport->x_pannable)
        {
          get_sync_widget_size_for_allocation (ui_viewport,
                                               viewport_width,
                                               viewport_height,
                                               &doc_width,
                                               &doc_height);
        }
    }

  need_scroll_bar_x = (ui_viewport->x_pannable &&
                       viewport_width < doc_width * ui_viewport->doc_scale_x);

  if (need_scroll_bar_x)
    {
      viewport_height -= x_scroll_bar_thickness;

      /* Enabling the x scroll bar may make it now need the y scroll bar */
      if (!need_scroll_bar_y)
        {
          need_scroll_bar_y = (ui_viewport->y_pannable &&
                               viewport_height < doc_height *
                               ui_viewport->doc_scale_y);
          if (need_scroll_bar_y)
            viewport_width -= y_scroll_bar_thickness;
        }

      /* If the sync widget is being expanded to fit the height of the
       * viewport then we we need to re-sync the document size now
       * that the height has changed... */
      if (ui_viewport->sync_widget && !ui_viewport->y_pannable)
        {
          get_sync_widget_size_for_allocation (ui_viewport,
                                               viewport_width,
                                               viewport_height,
                                               &doc_width,
                                               &doc_height);
        }
    }

  if (ui_viewport->sync_widget)
    {
      rut_sizable_set_size (ui_viewport->sync_widget, doc_width, doc_height);
      rut_ui_viewport_set_doc_width (ui_viewport, doc_width);
      rut_ui_viewport_set_doc_height (ui_viewport, doc_height);
    }

  rut_scroll_bar_set_virtual_length (ui_viewport->scroll_bar_y,
                                     doc_height * ui_viewport->doc_scale_y);

  rut_scroll_bar_set_virtual_viewport (ui_viewport->scroll_bar_y,
                                       viewport_height /
                                       ui_viewport->doc_scale_y);

  if (need_scroll_bar_y)
    {
      rut_transform_init_identity (ui_viewport->scroll_bar_y_transform);

      rut_transform_translate (ui_viewport->scroll_bar_y_transform,
                               ui_viewport->width - y_scroll_bar_thickness,
                               0,
                               0);

      if (!ui_viewport->scroll_bar_y_visible)
        rut_graphable_add_child (ui_viewport,
                                 ui_viewport->scroll_bar_y_transform);
    }
  else if (ui_viewport->scroll_bar_y_visible)
    rut_graphable_remove_child (ui_viewport->scroll_bar_y_transform);

  rut_scroll_bar_set_virtual_length (ui_viewport->scroll_bar_x,
                                     doc_width * ui_viewport->doc_scale_x);

  rut_scroll_bar_set_virtual_viewport (ui_viewport->scroll_bar_x,
                                       viewport_width /
                                       ui_viewport->doc_scale_x);

  if (need_scroll_bar_x)
    {
      rut_transform_init_identity (ui_viewport->scroll_bar_x_transform);

      rut_transform_translate (ui_viewport->scroll_bar_x_transform,
                               0,
                               ui_viewport->height - x_scroll_bar_thickness,
                               0);

      if (!ui_viewport->scroll_bar_x_visible)
        rut_graphable_add_child (ui_viewport,
                                 ui_viewport->scroll_bar_x_transform);
    }
  else if (ui_viewport->scroll_bar_x_visible)
    rut_graphable_remove_child (ui_viewport->scroll_bar_x_transform);

  ui_viewport->scroll_bar_x_visible = need_scroll_bar_x;
  ui_viewport->scroll_bar_y_visible = need_scroll_bar_y;
}

static void
queue_allocation (RutUIViewport *ui_viewport)
{
  rut_shell_add_pre_paint_callback (ui_viewport->ctx->shell,
                                    ui_viewport,
                                    allocate_cb,
                                    NULL /* user_data */);
}

static void
update_doc_xy_cb (RutProperty *target_property,
                  RutProperty *source_property,
                  void *user_data)
{
  RutUIViewport *ui_viewport = user_data;

  _rut_ui_viewport_update_doc_matrix (ui_viewport);
}

RutUIViewport *
rut_ui_viewport_new (RutContext *ctx,
                     float width,
                     float height)
{
  RutUIViewport *ui_viewport = g_slice_new0 (RutUIViewport);
  static CoglBool initialized = FALSE;

  if (initialized == FALSE)
    {
      _rut_init ();
      _rut_ui_viewport_init_type ();

      initialized = TRUE;
    }

  rut_object_init (RUT_OBJECT (ui_viewport), &rut_ui_viewport_type);

  ui_viewport->ctx = ctx;

  ui_viewport->ref_count = 1;

  rut_simple_introspectable_init (ui_viewport,
                                  _rut_ui_viewport_prop_specs,
                                  ui_viewport->properties);

  rut_graphable_init (RUT_OBJECT (ui_viewport));

  ui_viewport->width = width;
  ui_viewport->height = height;
  ui_viewport->doc_width = 0;
  ui_viewport->doc_height = 0;
  ui_viewport->doc_scale_x = 1;
  ui_viewport->doc_scale_y = 1;

  rut_list_init (&ui_viewport->preferred_size_cb_list);

  ui_viewport->x_pannable = TRUE;
  ui_viewport->y_pannable = TRUE;

  ui_viewport->scroll_bar_x_transform = rut_transform_new (ctx);
  ui_viewport->scroll_bar_x = rut_scroll_bar_new (ctx,
                                                  RUT_AXIS_X,
                                                  width, /* len */
                                                  width * 2, /* virtual len */
                                                  width); /* viewport len */
  rut_graphable_add_child (ui_viewport->scroll_bar_x_transform,
                           ui_viewport->scroll_bar_x);

  ui_viewport->scroll_bar_y_transform = rut_transform_new (ctx);
  ui_viewport->scroll_bar_y = rut_scroll_bar_new (ctx,
                                                  RUT_AXIS_Y,
                                                  height, /* len */
                                                  height * 2, /* virtual len */
                                                  height); /* viewport len */
  rut_graphable_add_child (ui_viewport->scroll_bar_y_transform,
                           ui_viewport->scroll_bar_y);

  rut_property_set_binding (&ui_viewport->properties[RUT_UI_VIEWPORT_PROP_DOC_X],
                            update_doc_xy_cb,
                            ui_viewport,
                            rut_introspectable_lookup_property (ui_viewport->scroll_bar_x,
                                                                "virtual_offset"),
                            NULL);
  rut_property_set_binding (&ui_viewport->properties[RUT_UI_VIEWPORT_PROP_DOC_Y],
                            update_doc_xy_cb,
                            ui_viewport,
                            rut_introspectable_lookup_property (ui_viewport->scroll_bar_y,
                                                                "virtual_offset"),
                            NULL);

  ui_viewport->doc_transform = rut_transform_new (ctx);
  rut_graphable_add_child (ui_viewport, ui_viewport->doc_transform);

  _rut_ui_viewport_update_doc_matrix (ui_viewport);

  ui_viewport->inputable.input_region =
    rut_input_region_new_rectangle (0, 0,
                                    ui_viewport->width,
                                    ui_viewport->height,
                                    _rut_ui_viewport_input_region_cb,
                                    ui_viewport);

  queue_allocation (ui_viewport);

  return ui_viewport;
}

void
rut_ui_viewport_add (RutUIViewport *ui_viewport,
                     RutObject *child)
{
  rut_graphable_add_child (ui_viewport->doc_transform, child);
}

static void
_rut_ui_viewport_set_size (RutObject *object,
                           float width,
                           float height)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (object);
  float spacing;

  if (width == ui_viewport->width && height == ui_viewport->height)
    return;

  ui_viewport->width = width;
  ui_viewport->height = height;

  rut_input_region_set_rectangle (ui_viewport->inputable.input_region,
                                  0, 0,
                                  width,
                                  height);

  /* If we might need to show both scroll bars at some point then leave a space
   * in the corner so we don't have to deal with the chicken and egg situation
   * of one scroll bar affecting whether the other scrollbar should be visible
   * or not.
   */
  if (ui_viewport->x_pannable && ui_viewport->y_pannable)
    spacing = rut_scroll_bar_get_thickness (ui_viewport->scroll_bar_x);
  else
    spacing = 0;

  rut_scroll_bar_set_length (ui_viewport->scroll_bar_x, width - spacing);
  rut_scroll_bar_set_length (ui_viewport->scroll_bar_y, height - spacing);

  queue_allocation (ui_viewport);

  rut_property_dirty (&ui_viewport->ctx->property_ctx,
                      &ui_viewport->properties[RUT_UI_VIEWPORT_PROP_WIDTH]);
  rut_property_dirty (&ui_viewport->ctx->property_ctx,
                      &ui_viewport->properties[RUT_UI_VIEWPORT_PROP_HEIGHT]);
}

static void
_rut_ui_viewport_get_size (RutObject *object,
                           float *width,
                           float *height)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (object);

  *width = ui_viewport->width;
  *height = ui_viewport->height;
}

void
rut_ui_viewport_set_doc_x (RutObject *obj, float doc_x)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);

  rut_scroll_bar_set_virtual_offset (ui_viewport->scroll_bar_x, doc_x);
}

void
rut_ui_viewport_set_doc_y (RutObject *obj, float doc_y)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);

  rut_scroll_bar_set_virtual_offset (ui_viewport->scroll_bar_y, doc_y);
}

void
rut_ui_viewport_set_doc_width (RutObject *obj, float doc_width)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);

  ui_viewport->doc_width = doc_width;

  if (!ui_viewport->sync_widget)
    queue_allocation (ui_viewport);

  rut_property_dirty (&ui_viewport->ctx->property_ctx,
                      &ui_viewport->properties[RUT_UI_VIEWPORT_PROP_DOC_WIDTH]);
}

void
rut_ui_viewport_set_doc_height (RutObject *obj, float doc_height)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);

  ui_viewport->doc_height = doc_height;

  if (!ui_viewport->sync_widget)
    queue_allocation (ui_viewport);

  rut_property_dirty (&ui_viewport->ctx->property_ctx,
                      &ui_viewport->properties[RUT_UI_VIEWPORT_PROP_DOC_HEIGHT]);
}

void
rut_ui_viewport_set_doc_scale_x (RutUIViewport *ui_viewport, float doc_scale_x)
{
  ui_viewport->doc_scale_x = doc_scale_x;
  _rut_ui_viewport_update_doc_matrix (ui_viewport);

  if (!ui_viewport->sync_widget)
    queue_allocation (ui_viewport);
}

void
rut_ui_viewport_set_doc_scale_y (RutUIViewport *ui_viewport, float doc_scale_y)
{
  ui_viewport->doc_scale_y = doc_scale_y;
  _rut_ui_viewport_update_doc_matrix (ui_viewport);

  if (!ui_viewport->sync_widget)
    queue_allocation (ui_viewport);
}

float
rut_ui_viewport_get_width (RutUIViewport *ui_viewport)
{
  return ui_viewport->width;
}

float
rut_ui_viewport_get_height (RutUIViewport *ui_viewport)
{
  return ui_viewport->height;
}

float
rut_ui_viewport_get_doc_x (RutObject *object)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (object);

  return rut_scroll_bar_get_virtual_offset (ui_viewport->scroll_bar_x);
}

float
rut_ui_viewport_get_doc_y (RutObject *object)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (object);

  return rut_scroll_bar_get_virtual_offset (ui_viewport->scroll_bar_y);
}

float
rut_ui_viewport_get_doc_scale_x (RutUIViewport *ui_viewport)
{
  return ui_viewport->doc_scale_x;
}

float
rut_ui_viewport_get_doc_scale_y (RutUIViewport *ui_viewport)
{
  return ui_viewport->doc_scale_y;
}

const CoglMatrix *
rut_ui_viewport_get_doc_matrix (RutUIViewport *ui_viewport)
{
  return rut_transform_get_matrix (ui_viewport->doc_transform);
}

void
rut_ui_viewport_set_x_pannable (RutObject *obj,
                                CoglBool pannable)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);

  ui_viewport->x_pannable = pannable;

  queue_allocation (ui_viewport);
}

CoglBool
rut_ui_viewport_get_x_pannable (RutObject *obj)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);

  return ui_viewport->x_pannable;
}

void
rut_ui_viewport_set_y_pannable (RutObject *obj,
                                CoglBool pannable)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);

  ui_viewport->y_pannable = pannable;

  queue_allocation (ui_viewport);
}

CoglBool
rut_ui_viewport_get_y_pannable (RutObject *obj)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);

  return ui_viewport->y_pannable;
}

static void
preferred_size_changed (RutUIViewport *ui_viewport)
{
  rut_closure_list_invoke (&ui_viewport->preferred_size_cb_list,
                           RutSizablePreferredSizeCallback,
                           ui_viewport);
}

static void
preferred_size_change_cb (RutObject *child,
                          void *user_data)
{
  RutUIViewport *ui_viewport = user_data;

  g_warn_if_fail (ui_viewport->sync_widget == child);

  preferred_size_changed (ui_viewport);
  queue_allocation (ui_viewport);
}

void
rut_ui_viewport_set_sync_widget (RutObject *obj,
                                 RutObject *widget)
{
  RutUIViewport *ui_viewport = RUT_UI_VIEWPORT (obj);
  RutClosure *preferred_size_closure = NULL;
  RutProperty *property =
    &ui_viewport->properties[RUT_UI_VIEWPORT_PROP_SYNC_WIDGET];

  if (widget)
    {
      g_return_if_fail (rut_object_is (widget, RUT_INTERFACE_ID_SIZABLE));
      rut_refable_ref (widget);
      queue_allocation (ui_viewport);
      preferred_size_closure =
        rut_sizable_add_preferred_size_callback (widget,
                                                 preferred_size_change_cb,
                                                 ui_viewport,
                                                 NULL /* destroy_cb */);
    }

  if (ui_viewport->sync_widget)
    {
      rut_closure_disconnect (ui_viewport->sync_widget_preferred_size_closure);
      rut_refable_unref (ui_viewport->sync_widget);
    }

  ui_viewport->sync_widget_preferred_size_closure = preferred_size_closure;
  ui_viewport->sync_widget = widget;

  rut_property_dirty (&ui_viewport->ctx->property_ctx, property);
}

void
rut_ui_viewport_set_scroll_bar_color (RutUIViewport *ui_viewport,
                                      const CoglColor *color)
{
  rut_scroll_bar_set_color (ui_viewport->scroll_bar_x, color);
  rut_scroll_bar_set_color (ui_viewport->scroll_bar_y, color);
}
