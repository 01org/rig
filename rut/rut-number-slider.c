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

#include <config.h>

#include <cogl/cogl.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>

#include "rut.h"
#include "rut-number-slider.h"
#include "rut-text.h"

#define RUT_NUMBER_SLIDER_CORNER_HEIGHT 60
#define RUT_NUMBER_SLIDER_ARROW_WIDTH 8
#define RUT_NUMBER_SLIDER_ARROW_HEIGHT \
  (16 - RUT_NUMBER_SLIDER_CORNER_HEIGHT * 2)
/* The offset to the top of the arrow as a texture coordinate */
#define RUT_NUMBER_SLIDER_CORNER_SIZE           \
  (RUT_NUMBER_SLIDER_CORNER_HEIGHT /            \
   (RUT_NUMBER_SLIDER_ARROW_HEIGHT +            \
    RUT_NUMBER_SLIDER_CORNER_HEIGHT * 2.0f))

#define RUT_NUMBER_SLIDER_FONT_SIZE 10

enum {
  RUT_NUMBER_SLIDER_PROP_VALUE,
  RUT_NUMBER_SLIDER_N_PROPS
};

struct _RutNumberSlider
{
  RutObjectProps _parent;

  RutContext *context;

  RutGraphableProps graphable;
  RutPaintableProps paintable;

  RutNineSlice *background;

  char *name;

  int width, height;

  int decimal_places;

  int ref_count;

  float min_value, max_value;
  float value;
  float step;

  PangoFontDescription *font_description;

  PangoLayout *actual_layout;
  PangoRectangle actual_logical_rect;
  PangoRectangle actual_ink_rect;

  PangoLayout *long_layout;
  PangoRectangle long_logical_rect;
  PangoRectangle long_ink_rect;

  RutInputRegion *input_region;

  RutSimpleIntrospectableProps introspectable;
  RutProperty properties[RUT_NUMBER_SLIDER_N_PROPS];

  /* This is set to true after we get a motion event with the down
   * action regardless of where was clicked */
  CoglBool button_down;
  /* This is set to TRUE if cursor has moved more than a couple of
   * pixels since the button was pressed. Once this happens the press
   * is no longer considered a click but is instead interpreted as a
   * drag to change the value */
  CoglBool button_drag;
  /* Where within the widget the cursor was when the button was
   * originally pressed */
  float button_x, button_y;
  /* The original value when the button was pressed */
  float button_value;

  /* The text control that will be displayed if the user directly
   * clicks on the button. This will be NULL while it is not displayed
   * and it will be immediately destroyed once the editing has
   * finished */
  RutText *text;
  /* The transform for the text. This has the same lifetime as the
   * text control */
  RutTransform *text_transform;
};

/* Some of the pipelines are cached and attached to the CoglContext so
 * that multiple sliders created using the same CoglContext will use
 * the same pipelines */
typedef struct
{
  CoglPipeline *bg_pipeline;
  CoglPipeline *selected_bg_pipeline;
} RutNumberSliderContextData;

RutType rut_number_slider_type;

static void
rut_number_slider_remove_text (RutNumberSlider *slider);

static RutPropertySpec
_rut_number_slider_prop_specs[] =
  {
    {
      .name = "value",
      .flags = RUT_PROPERTY_FLAG_READWRITE,
      .type = RUT_PROPERTY_TYPE_FLOAT,
      .data_offset = offsetof (RutNumberSlider, value),
      .setter.float_type = rut_number_slider_set_value
    },
    { 0 } /* XXX: Needed for runtime counting of the number of properties */
  };

static RutNumberSliderContextData *
rut_number_slider_get_context_data (RutContext *context)
{
  static CoglUserDataKey context_data_key;
  RutNumberSliderContextData *context_data =
    cogl_object_get_user_data (COGL_OBJECT (context->cogl_context),
                               &context_data_key);

  if (context_data == NULL)
    {
      context_data = g_new0 (RutNumberSliderContextData, 1);
      cogl_object_set_user_data (COGL_OBJECT (context->cogl_context),
                                 &context_data_key,
                                 context_data,
                                 g_free);
    }

  return context_data;
}

static void
rut_number_slider_clear_layout (RutNumberSlider *slider)
{
  if (slider->actual_layout)
    {
      g_object_unref (slider->actual_layout);
      slider->actual_layout = NULL;
    }

  if (slider->long_layout)
    {
      g_object_unref (slider->long_layout);
      slider->long_layout = NULL;
    }
}

static void
rut_number_slider_commit_text (RutNumberSlider *slider)
{
  if (slider->text)
    {
      const char *text = rut_text_get_text (slider->text);
      double value;

      errno = 0;
      value = strtod (text, NULL);

      if (errno == 0)
        rut_number_slider_set_value (slider, value);

      rut_number_slider_remove_text (slider);
    }
}

static void
_rut_number_slider_free (void *object)
{
  RutNumberSlider *slider = object;

  rut_number_slider_remove_text (slider);

  rut_refable_unref (slider->context);
  rut_refable_unref (slider->background);

  g_free (slider->name);

  rut_number_slider_clear_layout (slider);

  rut_graphable_remove_child (slider->input_region);
  rut_refable_unref (slider->input_region);

  rut_simple_introspectable_destroy (slider);
  rut_graphable_destroy (slider);

  pango_font_description_free (slider->font_description);

  g_slice_free (RutNumberSlider, slider);
}

RutRefCountableVTable _rut_number_slider_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_number_slider_free
};

typedef struct
{
  float x1, y1, x2, y2;
  float s1, t1, s2, t2;
} RutNumberSliderRectangle;

static PangoFontDescription *
rut_number_slider_create_font_description (void)
{
  PangoFontDescription *font_description = pango_font_description_new ();

  pango_font_description_set_family (font_description, "Sans");
  pango_font_description_set_absolute_size (font_description,
                                            RUT_NUMBER_SLIDER_FONT_SIZE *
                                            PANGO_SCALE);

  return font_description;
}

static void
rut_number_slider_setup_layout (RutNumberSlider *slider,
                                PangoLayout *layout)
{
  pango_layout_set_font_description (layout, slider->font_description);
}

static PangoLayout *
rut_number_slider_ensure_actual_layout (RutNumberSlider *slider)
{
  if (slider->actual_layout == NULL)
    {
      PangoLayout *layout;
      char *text;

      layout = pango_layout_new (slider->context->pango_context);

      text = g_strdup_printf ("%s: %.*f",
                              slider->name ? slider->name : "",
                              slider->decimal_places,
                              slider->value);
      pango_layout_set_text (layout, text, -1);

      g_free (text);

      rut_number_slider_setup_layout (slider, layout);

      pango_layout_get_pixel_extents (layout,
                                      &slider->actual_ink_rect,
                                      &slider->actual_logical_rect);

      slider->actual_layout = layout;
    }

  return slider->actual_layout;
}

static PangoLayout *
rut_number_slider_ensure_long_layout (RutNumberSlider *slider)
{
  if (slider->long_layout == NULL)
    {
      PangoLayout *layout;
      char *text;
      float max_value;

      layout = pango_layout_new (slider->context->pango_context);

      /* Use whichever value is likely to have a longer string
       * representation */
      if (fabsf (slider->min_value) > fabsf (slider->max_value))
        max_value = slider->min_value;
      else
        max_value = slider->max_value;

      /* If either of the values are G_MAXFLOAT then there isn't
       * really a range so using the maximum values will make a
       * preferred size that is way too long. Instead we'll just pick
       * a reasonably long number */
      if (max_value >= G_MAXFLOAT)
        max_value = 1000000.0f;

      /* Add a load of decimal places */
      if (max_value < 0)
        max_value = floorf (max_value) - 0.0001f;
      else
        max_value = ceilf (max_value) + 0.9999f;

      text = g_strdup_printf ("%s: %.*f",
                              slider->name ? slider->name : "",
                              slider->decimal_places,
                              max_value);
      pango_layout_set_text (layout, text, -1);

      g_free (text);

      rut_number_slider_setup_layout (slider, layout);

      pango_layout_get_pixel_extents (layout,
                                      &slider->long_ink_rect,
                                      &slider->long_logical_rect);

      slider->long_layout = layout;
    }

  return slider->long_layout;
}

static void
_rut_number_slider_paint (RutObject *object,
                          RutPaintContext *paint_ctx)
{
  RutNumberSlider *slider = (RutNumberSlider *) object;
  RutCamera *camera = paint_ctx->camera;
  CoglFramebuffer *fb = rut_camera_get_framebuffer (camera);
  CoglColor font_color;

  rut_nine_slice_set_size (slider->background,
                           slider->width,
                           slider->height);
  rut_paintable_paint (slider->background, paint_ctx);

  if (slider->text == NULL)
    {
      rut_number_slider_ensure_actual_layout (slider);

      cogl_color_init_from_4ub (&font_color, 0, 0, 0, 255);
      cogl_pango_show_layout (fb,
                              slider->actual_layout,
                              slider->width / 2 -
                              slider->actual_logical_rect.width / 2,
                              slider->height / 2 -
                              slider->actual_logical_rect.height / 2,
                              &font_color);
    }
}

static void
rut_number_slider_update_text_size (RutNumberSlider *slider)
{
  rut_number_slider_ensure_actual_layout (slider);

  rut_transform_init_identity (slider->text_transform);
  rut_transform_translate (slider->text_transform,
                           RUT_NUMBER_SLIDER_ARROW_WIDTH,
                           slider->height / 2 -
                           slider->actual_logical_rect.height / 2,
                           0.0f);

  rut_sizable_set_size (slider->text,
                        slider->width - RUT_NUMBER_SLIDER_ARROW_WIDTH * 2,
                        slider->actual_logical_rect.height);
}

static RutInputEventStatus
rut_number_slider_text_grab_cb (RutInputEvent *event,
                                void *user_data)
{
  RutNumberSlider *slider = user_data;
  float x, y;

  switch (rut_input_event_get_type (event))
    {
    case RUT_INPUT_EVENT_TYPE_MOTION:
      /* Check if this is a click outside of the text control */
      if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_DOWN &&
          (!rut_motion_event_unproject (event,
                                        RUT_OBJECT (slider),
                                        &x, &y) ||
           x < RUT_NUMBER_SLIDER_ARROW_WIDTH ||
           x >= slider->width - RUT_NUMBER_SLIDER_ARROW_WIDTH ||
           y < 0 ||
           y >= slider->height))
        {
          rut_number_slider_commit_text (slider);
          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
      break;

    case RUT_INPUT_EVENT_TYPE_KEY:
      /* The escape key cancels the text control */
      if (rut_key_event_get_action (event) == RUT_KEY_EVENT_ACTION_DOWN &&
          rut_key_event_get_keysym (event) == RUT_KEY_Escape)
        rut_number_slider_remove_text (slider);
      break;

    default:
      break;
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static void
rut_number_slider_text_activate_cb (RutText *text,
                                    void *user_data)
{
  RutNumberSlider *slider = user_data;

  rut_number_slider_commit_text (slider);
}

static void
rut_number_slider_handle_click (RutNumberSlider *slider,
                                RutInputEvent *event)
{
  float x, y;

  if (!rut_motion_event_unproject (event,
                                   RUT_OBJECT (slider),
                                   &x, &y))
    return;

  if (x < RUT_NUMBER_SLIDER_ARROW_WIDTH)
    rut_number_slider_set_value (slider, slider->button_value - slider->step);
  else if (x >= slider->width - RUT_NUMBER_SLIDER_ARROW_WIDTH)
    rut_number_slider_set_value (slider, slider->button_value + slider->step);
  else
    {
      int len;
      char *text;

      slider->text_transform = rut_transform_new (slider->context);
      rut_graphable_add_child (slider, slider->text_transform);

      slider->text = rut_text_new (slider->context);
      rut_text_set_font_description (slider->text, slider->font_description);
      rut_text_set_editable (slider->text, TRUE);
      rut_text_set_activatable (slider->text, TRUE);
      rut_text_add_activate_callback (slider->text,
                                      rut_number_slider_text_activate_cb,
                                      slider,
                                      NULL /* destroy_cb */);

      text = g_strdup_printf ("%.*f",
                              slider->decimal_places,
                              slider->value);
      rut_text_set_text (slider->text, text);
      len = strlen (text);
      g_free (text);

      rut_text_set_cursor_position (slider->text, 0);
      rut_text_set_selection_bound (slider->text, len);

      rut_text_grab_key_focus (slider->text);

      rut_graphable_add_child (slider->text_transform, slider->text);

      rut_number_slider_update_text_size (slider);

      rut_shell_grab_input (slider->context->shell,
                            rut_input_event_get_camera (event),
                            rut_number_slider_text_grab_cb,
                            slider);

      rut_shell_queue_redraw (slider->context->shell);
    }
}

static void
rut_number_slider_remove_text (RutNumberSlider *slider)
{
  if (slider->text)
    {
      rut_graphable_remove_child (slider->text);
      rut_refable_unref (slider->text);

      rut_graphable_remove_child (slider->text_transform);
      rut_refable_unref (slider->text_transform);

      rut_shell_ungrab_input (slider->context->shell,
                              rut_number_slider_text_grab_cb,
                              slider);

      slider->text = NULL;
    }
}

static RutInputEventStatus
rut_number_slider_input_cb (RutInputEvent *event,
                            void *user_data)
{
  RutNumberSlider *slider = user_data;
  float x, y;

  if (rut_input_event_get_type (event) != RUT_INPUT_EVENT_TYPE_MOTION)
    return RUT_INPUT_EVENT_STATUS_UNHANDLED;

  x = rut_motion_event_get_x (event);
  y = rut_motion_event_get_y (event);

  /* If the cursor has at least a pixel since it was clicked then will
   * mark the button as a drag event so that we don't intepret it as a
   * click when the button is released */
  if (fabsf (x - slider->button_x) >= 1.0f ||
      fabsf (y - slider->button_y) >= 1.0f)
    slider->button_drag = TRUE;

  /* Update the value based on the position if we're in a drag */
  if (slider->button_drag)
    rut_number_slider_set_value (slider,
                                 slider->button_value +
                                 (x - slider->button_x) * slider->step);

  if ((rut_motion_event_get_button_state (event) & RUT_BUTTON_STATE_1) == 0)
    {
      slider->button_down = FALSE;
      rut_shell_ungrab_input (slider->context->shell,
                              rut_number_slider_input_cb,
                              user_data);

      /* If we weren't dragging then this must have been an attempt to
       * click somewhere on the widget */
      if (!slider->button_drag)
        rut_number_slider_handle_click (slider, event);

      rut_shell_queue_redraw (slider->context->shell);
    }

  return RUT_INPUT_EVENT_STATUS_HANDLED;
}

static RutInputEventStatus
rut_number_slider_input_region_cb (RutInputRegion *region,
                                   RutInputEvent *event,
                                   void *user_data)
{
  RutNumberSlider *slider = user_data;
  RutCamera *camera;

  if (slider->text == NULL &&
      !slider->button_down &&
      rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION &&
      rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_DOWN &&
      (rut_motion_event_get_button_state (event) & RUT_BUTTON_STATE_1) &&
      (camera = rut_input_event_get_camera (event)))
    {
      slider->button_down = TRUE;
      slider->button_drag = FALSE;
      slider->button_value = slider->value;
      slider->button_x = rut_motion_event_get_x (event);
      slider->button_y = rut_motion_event_get_y (event);

      rut_shell_grab_input (slider->context->shell,
                            camera,
                            rut_number_slider_input_cb,
                            slider);

      rut_shell_queue_redraw (slider->context->shell);

      return RUT_INPUT_EVENT_STATUS_HANDLED;
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static void
rut_number_slider_set_size (RutObject *object,
                            float width,
                            float height)
{
  RutNumberSlider *slider = RUT_NUMBER_SLIDER (object);

  rut_shell_queue_redraw (slider->context->shell);
  slider->width = width;
  slider->height = height;
  rut_input_region_set_rectangle (slider->input_region,
                                  0.0f, 0.0f, /* x0 / y0 */
                                  slider->width, slider->height /* x1 / y1 */);

  if (slider->text)
    rut_number_slider_update_text_size (slider);
}

static void
rut_number_slider_get_size (RutObject *object,
                            float *width,
                            float *height)
{
  RutNumberSlider *slider = RUT_NUMBER_SLIDER (object);

  *width = slider->width;
  *height = slider->height;
}

static void
rut_number_slider_get_preferred_width (RutObject *object,
                                       float for_height,
                                       float *min_width_p,
                                       float *natural_width_p)
{
  RutNumberSlider *slider = RUT_NUMBER_SLIDER (object);
  float min_width;
  int layout_width;

  rut_number_slider_ensure_actual_layout (slider);
  rut_number_slider_ensure_long_layout (slider);

  layout_width = MAX (slider->actual_logical_rect.width,
                      slider->long_logical_rect.width);

  min_width = layout_width + RUT_NUMBER_SLIDER_ARROW_WIDTH * 2;

  if (min_width_p)
    *min_width_p = min_width;
  if (natural_width_p)
    /* Leave two pixels either side of the label */
    *natural_width_p = min_width + 4;
}

static void
rut_number_slider_get_preferred_height (RutObject *object,
                                        float for_width,
                                        float *min_height_p,
                                        float *natural_height_p)
{
  RutNumberSlider *slider = RUT_NUMBER_SLIDER (object);
  float layout_height;

  rut_number_slider_ensure_actual_layout (slider);
  rut_number_slider_ensure_long_layout (slider);

  layout_height = MAX (slider->actual_logical_rect.height,
                       slider->long_logical_rect.height);

  if (min_height_p)
    *min_height_p = MAX (layout_height, RUT_NUMBER_SLIDER_ARROW_HEIGHT);
  if (natural_height_p)
    *natural_height_p = MAX (layout_height + 4,
                             RUT_NUMBER_SLIDER_ARROW_HEIGHT);
}

static RutGraphableVTable _rut_number_slider_graphable_vtable = {
  NULL, /* child removed */
  NULL, /* child addded */
  NULL /* parent changed */
};

static RutPaintableVTable _rut_number_slider_paintable_vtable = {
  _rut_number_slider_paint
};

static RutIntrospectableVTable _rut_number_slider_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};

static RutSizableVTable _rut_number_slider_sizable_vtable = {
  rut_number_slider_set_size,
  rut_number_slider_get_size,
  rut_number_slider_get_preferred_width,
  rut_number_slider_get_preferred_height,
  NULL /* add_preferred_size_callback */
};

static void
_rut_number_slider_init_type (void)
{
  rut_type_init (&rut_number_slider_type, "RigNumberSlider");
  rut_type_add_interface (&rut_number_slider_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutNumberSlider, ref_count),
                          &_rut_number_slider_ref_countable_vtable);
  rut_type_add_interface (&rut_number_slider_type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (RutNumberSlider, graphable),
                          &_rut_number_slider_graphable_vtable);
  rut_type_add_interface (&rut_number_slider_type,
                          RUT_INTERFACE_ID_PAINTABLE,
                          offsetof (RutNumberSlider, paintable),
                          &_rut_number_slider_paintable_vtable);
  rut_type_add_interface (&rut_number_slider_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &_rut_number_slider_introspectable_vtable);
  rut_type_add_interface (&rut_number_slider_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RutNumberSlider, introspectable),
                          NULL); /* no implied vtable */
  rut_type_add_interface (&rut_number_slider_type,
                          RUT_INTERFACE_ID_SIZABLE,
                          0, /* no implied properties */
                          &_rut_number_slider_sizable_vtable);
}

RutNumberSlider *
rut_number_slider_new (RutContext *context)
{
  RutNumberSlider *slider = g_slice_new0 (RutNumberSlider);
  static CoglBool initialized = FALSE;
  CoglTexture *bg_texture;

  if (initialized == FALSE)
    {
      _rut_init ();
      _rut_number_slider_init_type ();

      initialized = TRUE;
    }

  slider->ref_count = 1;
  slider->context = rut_refable_ref (context);
  slider->step = 1.0f;
  slider->decimal_places = 2;

  slider->font_description = rut_number_slider_create_font_description ();

  rut_object_init (&slider->_parent, &rut_number_slider_type);

  rut_paintable_init (RUT_OBJECT (slider));
  rut_graphable_init (RUT_OBJECT (slider));

  rut_simple_introspectable_init (slider,
                                  _rut_number_slider_prop_specs,
                                  slider->properties);

  bg_texture =
    rut_load_texture_from_data_file (context,
                                     "number-slider-background.png",
                                     NULL);

  slider->background = rut_nine_slice_new (context,
                                           bg_texture,
                                           7, 7, 7, 7,
                                           0, 0);
  cogl_object_unref (bg_texture);

  slider->input_region =
    rut_input_region_new_rectangle (0, 0, 0, 0,
                                    rut_number_slider_input_region_cb,
                                    slider);
  rut_graphable_add_child (slider, slider->input_region);

  rut_sizable_set_size (slider, 60, 30);

  return slider;
}

void
rut_number_slider_set_name (RutNumberSlider *slider,
                            const char *name)
{
  rut_shell_queue_redraw (slider->context->shell);
  g_free (slider->name);
  slider->name = g_strdup (name);
}

void
rut_number_slider_set_min_value (RutNumberSlider *slider,
                                 float min_value)
{
  rut_number_slider_clear_layout (slider);

  slider->min_value = min_value;
}

void
rut_number_slider_set_max_value (RutNumberSlider *slider,
                                 float max_value)
{
  rut_number_slider_clear_layout (slider);

  slider->max_value = max_value;
}

void
rut_number_slider_set_value (RutObject *obj,
                             float value)
{
  RutNumberSlider *slider = RUT_NUMBER_SLIDER (obj);

  value = CLAMP (value, slider->min_value, slider->max_value);

  if (value == slider->value)
    return;

  slider->value = value;

  rut_property_dirty (&slider->context->property_ctx,
                      &slider->properties[RUT_NUMBER_SLIDER_PROP_VALUE]);

  rut_shell_queue_redraw (slider->context->shell);
  rut_number_slider_clear_layout (slider);
}

float
rut_number_slider_get_value (RutNumberSlider *slider)
{
  return slider->value;
}

void
rut_number_slider_set_step (RutNumberSlider *slider,
                            float step)
{
  slider->step = step;
}

int
rut_number_slider_get_decimal_places (RutNumberSlider *slider)
{
  return slider->decimal_places;
}

void
rut_number_slider_set_decimal_places (RutNumberSlider *slider,
                                      int decimal_places)
{
  rut_shell_queue_redraw (slider->context->shell);
  rut_number_slider_clear_layout (slider);

  slider->decimal_places = decimal_places;
}
