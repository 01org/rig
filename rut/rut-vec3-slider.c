/*
 * Rut
 *
 * Rig Utilities
 *
 * Copyright (C) 2012 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <config.h>

#include <cogl/cogl.h>

#include "rut-number-slider.h"
#include "rut-vec3-slider.h"
#include "rut-text.h"
#include "rut-box-layout.h"
#include "rut-composite-sizable.h"

enum {
  RUT_VEC3_SLIDER_PROP_VALUE,
  RUT_VEC3_SLIDER_N_PROPS
};

typedef struct
{
  RutNumberSlider *slider;
  RutProperty *property;
} RutVec3SliderComponent;

struct _RutVec3Slider
{
  RutObjectBase _base;

  RutContext *context;

  RutGraphableProps graphable;


  RutBoxLayout *hbox;

  RutVec3SliderComponent components[3];

  bool in_set_value;
  float value[3];

  RutIntrospectableProps introspectable;
  RutProperty properties[RUT_VEC3_SLIDER_N_PROPS];
};

RutType rut_vec3_slider_type;

static RutPropertySpec
_rut_vec3_slider_prop_specs[] =
  {
    {
      .name = "value",
      .flags = RUT_PROPERTY_FLAG_READWRITE,
      .type = RUT_PROPERTY_TYPE_VEC3,
      .data_offset = offsetof (RutVec3Slider, value),
      .setter.vec3_type = rut_vec3_slider_set_value,
    },
    { 0 } /* XXX: Needed for runtime counting of the number of properties */
  };

static void
_rut_vec3_slider_free (void *object)
{
  RutVec3Slider *slider = object;

  rut_introspectable_destroy (slider);
  rut_graphable_destroy (slider);

  rut_object_free (RutVec3Slider, slider);
}


static void
_rut_vec3_slider_init_type (void)
{
  static RutGraphableVTable graphable_vtable = {
      NULL, /* child removed */
      NULL, /* child addded */
      NULL /* parent changed */
  };
  static RutSizableVTable sizable_vtable = {
      rut_composite_sizable_set_size,
      rut_composite_sizable_get_size,
      rut_composite_sizable_get_preferred_width,
      rut_composite_sizable_get_preferred_height,
      rut_composite_sizable_add_preferred_size_callback
  };

  RutType *type = &rut_vec3_slider_type;
#define TYPE RutVec3Slider

  rut_type_init (type, C_STRINGIFY (TYPE), _rut_vec3_slider_free);
  rut_type_add_trait (type,
                      RUT_TRAIT_ID_GRAPHABLE,
                      offsetof (TYPE, graphable),
                      &graphable_vtable);
  rut_type_add_trait (type,
                      RUT_TRAIT_ID_INTROSPECTABLE,
                      offsetof (TYPE, introspectable),
                      NULL); /* no implied vtable */
  rut_type_add_trait (type,
                      RUT_TRAIT_ID_SIZABLE,
                      0, /* no implied properties */
                      &sizable_vtable);
  rut_type_add_trait (type,
                      RUT_TRAIT_ID_COMPOSITE_SIZABLE,
                      offsetof (TYPE, hbox),
                      NULL); /* no vtable */

#undef TYPE
}

static void
rut_vec3_slider_property_changed_cb (RutProperty *target_property,
                                     void *user_data)
{
  RutVec3Slider *slider = user_data;
  float value[3];
  int i;

  if (slider->in_set_value)
    return;

  for (i = 0; i < 3; i++)
    value[i] = rut_number_slider_get_value (slider->components[i].slider);

  rut_vec3_slider_set_value (slider, value);
}

RutVec3Slider *
rut_vec3_slider_new (RutContext *context)
{
  RutVec3Slider *slider = rut_object_alloc0 (RutVec3Slider,
                                             &rut_vec3_slider_type,
                                             _rut_vec3_slider_init_type);
  int i;

  slider->context = context;

  rut_graphable_init (slider);

  rut_introspectable_init (slider,
                           _rut_vec3_slider_prop_specs,
                           slider->properties);

  slider->hbox = rut_box_layout_new (context,
                                     RUT_BOX_LAYOUT_PACKING_LEFT_TO_RIGHT);
  rut_graphable_add_child (slider, slider->hbox);
  rut_object_unref (slider->hbox);

  for (i = 0; i < 3; i++)
    {
      RutText *text;

      slider->components[i].slider = rut_number_slider_new (context);
      rut_box_layout_add (slider->hbox, false,
                          slider->components[i].slider);
      rut_object_unref (slider->components[i].slider);

      if (i != 2)
        {
          text = rut_text_new_with_text (context, NULL, ", ");
          rut_box_layout_add (slider->hbox, false, text);
          rut_object_unref (text);
        }

      slider->components[i].property =
        rut_introspectable_lookup_property (slider->components[i].slider,
                                            "value");
    }

  rut_number_slider_set_markup_label (slider->components[0].slider,
                                      "<span foreground=\"red\">x:</span>");
  rut_number_slider_set_markup_label (slider->components[1].slider,
                                      "<span foreground=\"green\">y:</span>");
  rut_number_slider_set_markup_label (slider->components[2].slider,
                                      "<span foreground=\"blue\">z:</span>");

  rut_property_set_binding (&slider->properties[RUT_VEC3_SLIDER_PROP_VALUE],
                            rut_vec3_slider_property_changed_cb,
                            slider,
                            slider->components[0].property,
                            slider->components[1].property,
                            slider->components[2].property,
                            NULL);

  rut_sizable_set_size (slider, 60, 30);

  return slider;
}

void
rut_vec3_slider_set_min_value (RutVec3Slider *slider,
                               float min_value)
{
  int i;

  for (i = 0; i < 3; i++)
    {
      RutVec3SliderComponent *component = slider->components + i;
      rut_number_slider_set_min_value (component->slider, min_value);
    }
}

void
rut_vec3_slider_set_max_value (RutVec3Slider *slider,
                               float max_value)
{
  int i;

  for (i = 0; i < 3; i++)
    {
      RutVec3SliderComponent *component = slider->components + i;
      rut_number_slider_set_max_value (component->slider, max_value);
    }
}

void
rut_vec3_slider_set_value (RutObject *obj,
                           const float *value)
{
  RutVec3Slider *slider = obj;
  int i;

  memcpy (slider->value, value, sizeof (float) * 3);

  /* Normally we update slider->value[] based on notifications from
   * the per-component slider controls, but since we are manually
   * updating the controls here we need to temporarily ignore
   * the notifications so we avoid any recursion
   *
   * Note: If we change property notifications be deferred to the
   * mainloop then this mechanism will become redundant
   */
  slider->in_set_value = true;
  for (i = 0; i < 3; i++)
    {
      RutVec3SliderComponent *component = slider->components + i;
      rut_number_slider_set_value (component->slider, value[i]);
    }

  slider->in_set_value = false;

  rut_property_dirty (&slider->context->property_ctx,
                      &slider->properties[RUT_VEC3_SLIDER_PROP_VALUE]);
}

void
rut_vec3_slider_set_step (RutVec3Slider *slider,
                          float step)
{
  int i;

  for (i = 0; i < 3; i++)
    {
      RutVec3SliderComponent *component = slider->components + i;
      rut_number_slider_set_step (component->slider, step);
    }
}

int
rut_vec3_slider_get_decimal_places (RutVec3Slider *slider)
{
  return rut_number_slider_get_decimal_places (slider->components[0].slider);
}

void
rut_vec3_slider_set_decimal_places (RutVec3Slider *slider,
                                    int decimal_places)
{
  int i;

  for (i = 0; i < 3; i++)
    {
      RutVec3SliderComponent *component = slider->components + i;
      rut_number_slider_set_decimal_places (component->slider, decimal_places);
    }
}
