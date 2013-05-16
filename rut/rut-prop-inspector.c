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

#include "rut.h"
#include "rut-prop-inspector.h"
#include "rut-vec3-slider.h"
#include "rut-number-slider.h"
#include "rut-drop-down.h"
#include "rut-color-button.h"

/* A RutPropInspector represents a control to manipulate a property.
 * It can internally be composed of multiple extra controls for
 * example to toggle whether the property is animatable or not */

#define RUT_PROP_INSPECTOR_MAX_N_CONTROLS 3

/* Horizontal padding to add between controls */
#define RUT_PROP_INSPECTOR_PADDING 5

typedef struct
{
  RutTransform *transform;
  RutObject *control;

  /* If TRUE, any extra space allocated to the inspector will be
   * equally divided between this control and all other controls that
   * have this set to TRUE */
  CoglBool expand;
} RutPropInspectorControl;

struct _RutPropInspector
{
  RutObjectProps _parent;

  float width, height;

  RutContext *context;

  RutPaintableProps paintable;
  RutGraphableProps graphable;

  RutPropInspectorControl controls[RUT_PROP_INSPECTOR_MAX_N_CONTROLS];
  int n_controls;

  RutProperty *source_prop;
  RutProperty *target_prop;

  RutToggle *animated_toggle;

  /* This dummy property is used so that we can listen to changes on
   * the source property without having to directly make the target
   * property depend on it. The target property can only have one
   * dependent property so we don't want to steal that slot */
  RutProperty dummy_prop;

  RutPropInspectorCallback property_changed_cb;
  RutPropInspectorAnimatedCallback animated_changed_cb;
  void *user_data;

  /* This is set while the property is being reloaded. This will make
   * it avoid forwarding on property changes that were just caused by
   * reading the already current value. */
  CoglBool reloading_property;

  int ref_count;
};

static RutPropertySpec
dummy_property_spec =
  {
    .name = "dummy",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RutPropInspector, ref_count),
    .setter.any_type = abort,
    .getter.any_type = abort
  };

RutType rut_prop_inspector_type;

static void
_rut_prop_inspector_free (void *object)
{
  RutPropInspector *inspector = object;
  int i;

  rut_property_destroy (&inspector->dummy_prop);

  rut_refable_unref (inspector->context);

  for (i = 0; i < inspector->n_controls; i++)
    {
      RutPropInspectorControl *control = inspector->controls + i;

      rut_graphable_remove_child (control->control);
      rut_graphable_remove_child (control->transform);
      rut_refable_unref (control->control);
      rut_refable_unref (control->transform);
    }

  rut_graphable_destroy (inspector);

  g_slice_free (RutPropInspector, inspector);
}

RutRefCountableVTable _rut_prop_inspector_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_prop_inspector_free
};

static RutGraphableVTable _rut_prop_inspector_graphable_vtable = {
  NULL, /* child removed */
  NULL, /* child addded */
  NULL /* parent changed */
};

static void
_rut_prop_inspector_paint (RutObject *object,
                           RutPaintContext *paint_ctx)
{
  /* NOP */
}

RutPaintableVTable _rut_prop_inspector_paintable_vtable = {
  _rut_prop_inspector_paint
};

static void
rut_prop_inspector_set_size (void *object,
                             float total_width,
                             float total_height)
{
  RutPropInspector *inspector = RUT_PROP_INSPECTOR (object);
  float preferred_widths[RUT_PROP_INSPECTOR_MAX_N_CONTROLS];
  float total_preferred_width =
    (inspector->n_controls - 1) * RUT_PROP_INSPECTOR_PADDING;
  float total_expandable_width = 0.0f;
  float x_pos = 0.0f;
  float extra_space;
  int i;

  /* Get all of the preferred widths */
  for (i = 0; i < inspector->n_controls; i++)
    {
      rut_sizable_get_preferred_width (inspector->controls[i].control,
                                       -1, /* for_height */
                                       NULL, /* min_width */
                                       preferred_widths + i);
      total_preferred_width += preferred_widths[i];

      /* Keep track of the total width of expandable controls so that
       * we can work out the fraction of the extra width that each
       * control should receive */
      if (inspector->controls[i].expand)
        total_expandable_width += preferred_widths[i];
    }

  extra_space = total_width - total_preferred_width;

  /* If we've been allocated a space that's too small there's not much
   * we can do so we'll just go off the end */
  if (extra_space < 0.0f)
    extra_space = 0.0f;

  for (i = 0; i < inspector->n_controls; i++)
    {
      RutPropInspectorControl *control = inspector->controls + i;
      float width;
      float height;

      width = preferred_widths[i];

      if (control->expand)
        width += extra_space * width / total_expandable_width;

      rut_sizable_get_preferred_height (control->control,
                                        width,
                                        NULL,
                                        &height);

      rut_transform_init_identity (control->transform);
      rut_transform_translate (control->transform,
                               nearbyintf (x_pos),
                               nearbyintf (total_height / 2.0f -
                                           height / 2.0f),
                               0.0f);

      rut_sizable_set_size (control->control,
                            nearbyintf (width),
                            nearbyintf (height));

      x_pos += width + RUT_PROP_INSPECTOR_PADDING;
    }
}

static void
rut_prop_inspector_get_preferred_width (void *object,
                                        float for_height,
                                        float *min_width_p,
                                        float *natural_width_p)
{
  RutPropInspector *inspector = RUT_PROP_INSPECTOR (object);
  float total_natural_width =
    (inspector->n_controls - 1) * RUT_PROP_INSPECTOR_PADDING;
  float total_min_width = total_natural_width;
  int i;

  for (i = 0; i < inspector->n_controls; i++)
    {
      RutPropInspectorControl *control = inspector->controls + i;
      float min_width;
      float natural_width;

      rut_sizable_get_preferred_width (control->control,
                                       for_height,
                                       &min_width,
                                       &natural_width);

      total_min_width += min_width;
      total_natural_width += natural_width;
    }

  if (min_width_p)
    *min_width_p = total_min_width;
  if (natural_width_p)
    *natural_width_p = total_natural_width;
}

static void
rut_prop_inspector_get_preferred_height (void *object,
                                         float for_width,
                                         float *min_height_p,
                                         float *natural_height_p)
{
  RutPropInspector *inspector = RUT_PROP_INSPECTOR (object);
  float max_min_height = 0.0f;
  float max_natural_height = 0.0f;
  int i;

  for (i = 0; i < inspector->n_controls; i++)
    {
      RutPropInspectorControl *control = inspector->controls + i;
      float min_height;
      float natural_height;

      rut_sizable_get_preferred_height (control->control,
                                        -1, /* for_width */
                                        &min_height,
                                        &natural_height);

      if (min_height > max_min_height)
        max_min_height = min_height;
      if (natural_height > max_natural_height)
        max_natural_height = natural_height;
    }

  if (min_height_p)
    *min_height_p = max_min_height;
  if (natural_height_p)
    *natural_height_p = max_natural_height;
}

static void
rut_prop_inspector_get_size (void *object,
                             float *width,
                             float *height)
{
  RutPropInspector *inspector = RUT_PROP_INSPECTOR (object);

  *width = inspector->width;
  *height = inspector->height;
}

static RutSizableVTable _rut_prop_inspector_sizable_vtable = {
  rut_prop_inspector_set_size,
  rut_prop_inspector_get_size,
  rut_prop_inspector_get_preferred_width,
  rut_prop_inspector_get_preferred_height,
  NULL /* add_preferred_size_callback */
};

static void
_rut_prop_inspector_init_type (void)
{
  rut_type_init (&rut_prop_inspector_type, "RigPropInspector");
  rut_type_add_interface (&rut_prop_inspector_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutPropInspector, ref_count),
                          &_rut_prop_inspector_ref_countable_vtable);
  rut_type_add_interface (&rut_prop_inspector_type,
                          RUT_INTERFACE_ID_PAINTABLE,
                          offsetof (RutPropInspector, paintable),
                          &_rut_prop_inspector_paintable_vtable);
  rut_type_add_interface (&rut_prop_inspector_type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (RutPropInspector, graphable),
                          &_rut_prop_inspector_graphable_vtable);
  rut_type_add_interface (&rut_prop_inspector_type,
                          RUT_INTERFACE_ID_SIZABLE,
                          0, /* no implied properties */
                          &_rut_prop_inspector_sizable_vtable);
}

static RutObject *
rut_prop_inspector_create_control_for_property (RutContext *context,
                                                RutProperty *prop,
                                                RutProperty **control_prop,
                                                const char **label_text)
{
  const RutPropertySpec *spec = prop->spec;
  const char *name;
  RutText *label;

  *label_text = NULL;

  if (spec->nick)
    name = spec->nick;
  else
    name = spec->name;

  switch ((RutPropertyType) spec->type)
    {
    case RUT_PROPERTY_TYPE_BOOLEAN:
      {
        char *unselected_icon = rut_find_data_file ("toggle-unselected.png");
        char *selected_icon = rut_find_data_file ("toggle-selected.png");
        RutToggle *toggle = rut_toggle_new_with_icons (context,
                                                       unselected_icon,
                                                       selected_icon,
                                                       name);

        *control_prop = rut_introspectable_lookup_property (toggle, "state");
        return toggle;
      }

    case RUT_PROPERTY_TYPE_VEC3:
      {
        RutVec3Slider *slider = rut_vec3_slider_new (context);
        float min = -G_MAXFLOAT, max = G_MAXFLOAT;

        rut_vec3_slider_set_name (slider, name);

        if ((spec->flags & RUT_PROPERTY_FLAG_VALIDATE))
          {
            const RutPropertyValidationVec3 *validation =
              &spec->validation.vec3_range;

            min = validation->min;
            max = validation->max;
          }

        rut_vec3_slider_set_min_value (slider, min);
        rut_vec3_slider_set_max_value (slider, max);

        rut_vec3_slider_set_decimal_places (slider, 2);

        *control_prop = rut_introspectable_lookup_property (slider, "value");

        return slider;
      }

    case RUT_PROPERTY_TYPE_FLOAT:
    case RUT_PROPERTY_TYPE_INTEGER:
      {
        RutNumberSlider *slider = rut_number_slider_new (context);
        float min = -G_MAXFLOAT, max = G_MAXFLOAT;

        rut_number_slider_set_name (slider, name);

        if (spec->type == RUT_PROPERTY_TYPE_INTEGER)
          {
            rut_number_slider_set_decimal_places (slider, 0);
            rut_number_slider_set_step (slider, 1.0);

            if ((spec->flags & RUT_PROPERTY_FLAG_VALIDATE))
              {
                const RutPropertyValidationInteger *validation =
                  &spec->validation.int_range;

                min = validation->min;
                max = validation->max;
              }
          }
        else
          {
            rut_number_slider_set_decimal_places (slider, 2);
            rut_number_slider_set_step (slider, 0.1);

            if ((spec->flags & RUT_PROPERTY_FLAG_VALIDATE))
              {
                const RutPropertyValidationFloat *validation =
                  &spec->validation.float_range;

                min = validation->min;
                max = validation->max;
              }
          }

        rut_number_slider_set_min_value (slider, min);
        rut_number_slider_set_max_value (slider, max);

        *control_prop = rut_introspectable_lookup_property (slider, "value");

        return slider;
      }

    case RUT_PROPERTY_TYPE_ENUM:
      /* If the enum isn't validated then we can't get the value
       * names so we can't make a useful control */
      if ((spec->flags & RUT_PROPERTY_FLAG_VALIDATE))
        {
          RutDropDown *drop = rut_drop_down_new (context);
          int n_values, i;
          const RutUIEnum *ui_enum = spec->validation.ui_enum;
          RutDropDownValue *values;

          for (n_values = 0; ui_enum->values[n_values].nick; n_values++);

          values = g_alloca (sizeof (RutDropDownValue) * n_values);

          for (i = 0; i < n_values; i++)
            {
              values[i].name = (ui_enum->values[i].blurb ?
                                ui_enum->values[i].blurb :
                                ui_enum->values[i].nick);
              values[i].value = ui_enum->values[i].value;
            }

          rut_drop_down_set_values_array (drop, values, n_values);

          *control_prop = rut_introspectable_lookup_property (drop, "value");
          *label_text = name;

          return drop;
        }
      break;

    case RUT_PROPERTY_TYPE_TEXT:
      {
        RutEntry *entry = rut_entry_new (context);
        RutText *text = rut_entry_get_text (entry);

        rut_text_set_single_line_mode (text, TRUE);
        *control_prop = rut_introspectable_lookup_property (text, "text");
        *label_text = name;

        return entry;
      }
      break;

    case RUT_PROPERTY_TYPE_COLOR:
      {
        RutColorButton *button = rut_color_button_new (context);

        *control_prop = rut_introspectable_lookup_property (button, "color");
        *label_text = name;

        return button;
      }
      break;

    default:
      break;
    }

  label = rut_text_new (context);

  rut_text_set_text (label, name);

  *control_prop = NULL;

  return label;
}

static void
property_changed_cb (RutProperty *target_prop,
                     RutProperty *source_prop,
                     void *user_data)
{
  RutPropInspector *inspector = user_data;

  g_return_if_fail (target_prop == &inspector->dummy_prop);
  g_return_if_fail (source_prop == inspector->source_prop);

  /* If the property change was only triggered because we are
   * rereading the existing value then we won't bother notifying
   * anyone */
  if (inspector->reloading_property)
    return;

  inspector->property_changed_cb (inspector->target_prop,
                                  inspector->source_prop,
                                  inspector->user_data);
}

static void
animated_toggle_cb (RutToggle *toggle,
                    CoglBool value,
                    void *user_data)
{
  RutPropInspector *inspector = user_data;

  /* If the change was only triggered because we are rereading the
   * existing value then we won't bother updating the state */
  if (inspector->reloading_property)
    return;

  inspector->animated_changed_cb (inspector->target_prop,
                                  value,
                                  inspector->user_data);
}

static void
add_animatable_toggle (RutPropInspector *inspector,
                       RutProperty *prop)
{
  const RutPropertySpec *spec = prop->spec;

  if (spec->animatable)
    {
      char *unselected_icon = rut_find_data_file ("record-button.png");
      char *selected_icon = rut_find_data_file ("record-button-selected.png");
      RutObject *control = rut_toggle_new_with_icons (inspector->context,
                                                      unselected_icon,
                                                      selected_icon,
                                                      "");
      RutPropInspectorControl *control_data =
        inspector->controls + inspector->n_controls++;

      g_free (unselected_icon);
      g_free (selected_icon);

      rut_toggle_set_state (control, FALSE);

      rut_toggle_add_on_toggle_callback (control,
                                         animated_toggle_cb,
                                         inspector,
                                         NULL /* destroy_cb */);

      control_data->control = control;
      control_data->transform = rut_transform_new (inspector->context);
      rut_graphable_add_child (control_data->transform, control);
      rut_graphable_add_child (inspector, control_data->transform);

      inspector->animated_toggle = control;
    }
}

static void
add_control (RutPropInspector *inspector,
             RutProperty *prop)
{
  RutProperty *control_prop;
  RutObject *control;
  const char *label_text;

  control = rut_prop_inspector_create_control_for_property (inspector->context,
                                                            prop,
                                                            &control_prop,
                                                            &label_text);

  if (control)
    {
      RutPropInspectorControl *control_data =
        inspector->controls + inspector->n_controls++;

      if (label_text)
        {
          control_data->control =
            rut_text_new_with_text (inspector->context,
                                    NULL, /* font_name */
                                    label_text);
          control_data->transform = rut_transform_new (inspector->context);
          rut_graphable_add_child (control_data->transform, control_data->control);
          rut_graphable_add_child (inspector, control_data->transform);

          control_data = inspector->controls + inspector->n_controls++;
        }

      control_data->control = control;
      control_data->expand = TRUE;

      control_data->transform = rut_transform_new (inspector->context);
      rut_graphable_add_child (control_data->transform, control);
      rut_graphable_add_child (inspector, control_data->transform);

      if (control_prop)
        {
          rut_property_set_binding (&inspector->dummy_prop,
                                    property_changed_cb,
                                    inspector,
                                    control_prop,
                                    NULL);
          inspector->source_prop = control_prop;
        }
    }
}

RutPropInspector *
rut_prop_inspector_new (RutContext *ctx,
                        RutProperty *property,
                        RutPropInspectorCallback user_property_changed_cb,
                        RutPropInspectorAnimatedCallback user_animated_cb,
                        void *user_data)
{
  RutPropInspector *inspector = g_slice_new0 (RutPropInspector);
  static CoglBool initialized = FALSE;

  if (initialized == FALSE)
    {
      _rut_init ();
      _rut_prop_inspector_init_type ();

      initialized = TRUE;
    }

  inspector->ref_count = 1;
  inspector->context = rut_refable_ref (ctx);
  inspector->target_prop = property;
  inspector->property_changed_cb = user_property_changed_cb;
  inspector->animated_changed_cb = user_animated_cb;
  inspector->user_data = user_data;

  rut_object_init (&inspector->_parent, &rut_prop_inspector_type);

  rut_paintable_init (RUT_OBJECT (inspector));
  rut_graphable_init (RUT_OBJECT (inspector));

  inspector->n_controls = 0;

  rut_property_init (&inspector->dummy_prop,
                     &dummy_property_spec,
                     inspector);

  if (property->spec->animatable)
    add_animatable_toggle (inspector, property);

  add_control (inspector, property);

  rut_prop_inspector_reload_property (inspector);

  rut_prop_inspector_set_size (inspector, 10, 10);

  return inspector;
}

void
rut_prop_inspector_reload_property (RutPropInspector *inspector)
{
  if (inspector->target_prop)
    {
      CoglBool old_reloading = inspector->reloading_property;

      inspector->reloading_property = TRUE;

      if (inspector->source_prop)
        {
          if (inspector->target_prop->spec->type == RUT_PROPERTY_TYPE_ENUM &&
              inspector->source_prop->spec->type == RUT_PROPERTY_TYPE_INTEGER)
            {
              int value = rut_property_get_enum (inspector->target_prop);
              rut_property_set_integer (&inspector->context->property_ctx,
                                        inspector->source_prop,
                                        value);
            }
          else if (inspector->target_prop->spec->type ==
                   RUT_PROPERTY_TYPE_INTEGER)
            {
              int value = rut_property_get_integer (inspector->target_prop);
              rut_property_set_float (&inspector->context->property_ctx,
                                      inspector->source_prop,
                                      value);
            }
          else
            rut_property_copy_value (&inspector->context->property_ctx,
                                     inspector->source_prop,
                                     inspector->target_prop);
        }

      inspector->reloading_property = old_reloading;
    }
}

void
rut_prop_inspector_set_animated (RutPropInspector *inspector,
                                 CoglBool animated)
{
  if (inspector->animated_toggle)
    {
      CoglBool old_reloading = inspector->reloading_property;

      inspector->reloading_property = TRUE;

      rut_toggle_set_state (inspector->animated_toggle,
                            animated);

      inspector->reloading_property = old_reloading;
    }
}
