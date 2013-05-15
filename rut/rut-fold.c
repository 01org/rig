/*
 * Rig
 *
 * Copyright (C) 2013 Intel Corporation.
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

#include "rut-fixed.h"
#include "rut-fold.h"

struct _RutFold
{
  RutObjectProps _parent;

  RutContext *context;

  RutBoxLayout *vbox;
  RutText *label;
  RutFixed *fold_icon_shim;
  RutNineSlice *fold_up_icon;
  RutNineSlice *fold_down_icon;

  RutInputRegion *input_region;

  CoglBool folded;

  RutObject *child;

  RutGraphableProps graphable;

  int ref_count;
};

static void
_rut_fold_free (void *object)
{
  RutFold *fold = object;

  rut_fold_set_child (fold, NULL);

  rut_refable_unref (fold->fold_up_icon);
  rut_refable_unref (fold->fold_down_icon);

  rut_graphable_destroy (fold);

  rut_refable_unref (fold->context);

  g_slice_free (RutFold, fold);
}

static void
rut_fold_set_size (void *object,
                   float width,
                   float height)
{
  RutFold *fold = object;

  rut_sizable_set_size (fold->vbox, width, height);
}

static void
rut_fold_get_preferred_width (void *sizable,
                              float for_height,
                              float *min_width_p,
                              float *natural_width_p)
{
  RutFold *fold = sizable;

  rut_sizable_get_preferred_width (fold->vbox,
                                   for_height,
                                   min_width_p,
                                   natural_width_p);
}

static void
rut_fold_get_preferred_height (void *sizable,
                               float for_width,
                               float *min_height_p,
                               float *natural_height_p)
{
  RutFold *fold = sizable;

  rut_sizable_get_preferred_height (fold->vbox,
                                    for_width,
                                    min_height_p,
                                    natural_height_p);
}

typedef struct _ForwardingClosure
{
  RutFold *fold;
  RutSizablePreferredSizeCallback cb;
  void *user_data;
} ForwardingClosure;

void
forwarding_closure_destroy_cb (void *user_data)
{
  g_slice_free (ForwardingClosure, user_data);
}

static void
forward_preferred_size_change_cb (RutObject *vbox,
                                  void *user_data)
{
  ForwardingClosure *closure = user_data;
  closure->cb (closure->fold, closure->user_data);
}

static RutClosure *
rut_fold_add_preferred_size_callback (void *object,
                                      RutSizablePreferredSizeCallback cb,
                                      void *user_data,
                                      RutClosureDestroyCallback destroy)
{
  RutFold *fold = object;
  ForwardingClosure *closure = g_slice_new (ForwardingClosure);

  closure->fold = fold;
  closure->cb = cb;
  closure->user_data = user_data;

  return rut_sizable_add_preferred_size_callback (fold->vbox,
                                                  forward_preferred_size_change_cb,
                                                  closure,
                                                  forwarding_closure_destroy_cb);
}

static void
rut_fold_get_size (void *object,
                   float *width,
                   float *height)
{
  RutFold *fold = object;

  rut_sizable_get_size (fold->vbox, width, height);
}

RutType rut_fold_type;

static void
_rut_fold_init_type (void)
{
  static RutRefCountableVTable refable_vtable = {
      rut_refable_simple_ref,
      rut_refable_simple_unref,
      _rut_fold_free
  };
  static RutGraphableVTable graphable_vtable = {
      NULL, /* child removed */
      NULL, /* child added */
      NULL /* parent changed */
  };
  static RutSizableVTable sizable_vtable = {
      rut_fold_set_size,
      rut_fold_get_size,
      rut_fold_get_preferred_width,
      rut_fold_get_preferred_height,
      rut_fold_add_preferred_size_callback
  };

  RutType *type = &rut_fold_type;
#define TYPE RutFold

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
                          RUT_INTERFACE_ID_SIZABLE,
                          0, /* no implied properties */
                          &sizable_vtable);

#undef TYPE
}

static RutInputEventStatus
input_cb (RutInputRegion *region,
          RutInputEvent *event,
          void *user_data)
{
  RutFold *fold = user_data;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION &&
      rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_UP)
    {
      rut_fold_set_folded (fold, !fold->folded);
      return RUT_INPUT_EVENT_STATUS_HANDLED;
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

RutFold *
rut_fold_new (RutContext *ctx,
              const char *label)
{
  RutFold *fold = g_slice_new0 (RutFold);
  static CoglBool initialized = FALSE;
  RutStack *header_stack;
  RutBoxLayout *header_hbox;
  RutBin *label_bin;
  RutBin *fold_icon_align;
  CoglTexture *texture;
  CoglColor black;

  if (initialized == FALSE)
    {
      _rut_fold_init_type ();

      initialized = TRUE;
    }

  fold->ref_count = 1;
  fold->context = rut_refable_ref (ctx);

  rut_object_init (&fold->_parent, &rut_fold_type);

  rut_graphable_init (fold);

  fold->vbox = rut_box_layout_new (ctx, RUT_BOX_LAYOUT_PACKING_TOP_TO_BOTTOM);

  header_stack = rut_stack_new (ctx, 0, 0);
  rut_box_layout_add (fold->vbox, FALSE, header_stack);
  rut_refable_unref (header_stack);

  header_hbox =
    rut_box_layout_new (ctx, RUT_BOX_LAYOUT_PACKING_LEFT_TO_RIGHT);
  rut_stack_add (header_stack, header_hbox);
  rut_refable_unref (header_hbox);

  fold_icon_align = rut_bin_new (ctx);
  rut_bin_set_x_position (fold_icon_align, RUT_BIN_POSITION_BEGIN);
  rut_bin_set_y_position (fold_icon_align, RUT_BIN_POSITION_CENTER);
  rut_bin_set_right_padding (fold_icon_align, 10);
  rut_box_layout_add (header_hbox, FALSE, fold_icon_align);
  rut_refable_unref (fold_icon_align);

  texture = rut_load_texture_from_data_file (ctx, "tri-fold-up.png", NULL);
  fold->fold_up_icon = rut_nine_slice_new (ctx, texture,
                                           0, 0, 0, 0,
                                           cogl_texture_get_width (texture),
                                           cogl_texture_get_height (texture));
  cogl_object_unref (texture);

  texture = rut_load_texture_from_data_file (ctx, "tri-fold-down.png", NULL);
  fold->fold_down_icon = rut_nine_slice_new (ctx, texture,
                                             0, 0, 0, 0,
                                             cogl_texture_get_width (texture),
                                             cogl_texture_get_height (texture));
  cogl_object_unref (texture);

  fold->fold_icon_shim = rut_fixed_new (ctx,
                                        cogl_texture_get_width (texture),
                                        cogl_texture_get_height (texture));
  rut_bin_set_child (fold_icon_align, fold->fold_icon_shim);
  rut_refable_unref (fold->fold_icon_shim);

  rut_graphable_add_child (fold->fold_icon_shim, fold->fold_down_icon);

  /* NB: we keep references to the icons so they can be swapped
   * without getting disposed. */

  label_bin = rut_bin_new (ctx);
  rut_bin_set_y_position (label_bin, RUT_BIN_POSITION_CENTER);
  rut_box_layout_add (header_hbox, FALSE, label_bin);
  rut_refable_unref (label_bin);

  fold->label = rut_text_new_with_text (ctx, NULL, label);
  rut_bin_set_child (label_bin, fold->label);
  rut_refable_unref (fold->label);

  cogl_color_init_from_4f (&black, 0, 0, 0, 1);
  rut_fold_set_folder_color (fold, &black);
  rut_fold_set_label_color (fold, &black);

  rut_graphable_add_child (fold, fold->vbox);
  rut_refable_unref (fold->vbox);

  fold->input_region = rut_input_region_new_rectangle (0, 0, 0, 0,
                                                       input_cb,
                                                       fold);
  rut_stack_add (header_stack, fold->input_region);
  rut_refable_unref (fold->input_region);

  fold->folded = FALSE;

  return fold;
}

void
rut_fold_set_child (RutFold *fold, RutObject *child)
{
  g_return_if_fail (rut_object_get_type (fold) == &rut_fold_type);

  if (child)
    rut_refable_ref (child);

  if (fold->child)
    {
      if (!fold->folded)
        rut_box_layout_remove (fold->vbox, fold->child);
      rut_refable_unref (fold->child);
    }

  fold->child = child;
  if (child && !fold->folded)
    rut_box_layout_add (fold->vbox, TRUE, child);
}

void
rut_fold_set_folded (RutFold *fold, CoglBool folded)
{
  if (fold->folded == folded || fold->child == NULL)
    return;

  if (folded)
    {
      rut_fixed_remove_child (fold->fold_icon_shim, fold->fold_down_icon);
      rut_fixed_add_child (fold->fold_icon_shim, fold->fold_up_icon);
      rut_box_layout_remove (fold->vbox, fold->child);
    }
  else
    {
      rut_fixed_remove_child (fold->fold_icon_shim, fold->fold_up_icon);
      rut_fixed_add_child (fold->fold_icon_shim, fold->fold_down_icon);
      rut_box_layout_add (fold->vbox, TRUE, fold->child);
    }

  fold->folded = folded;

  rut_shell_queue_redraw (fold->context->shell);
}

void
rut_fold_set_folder_color (RutFold *fold, const CoglColor *color)
{
  CoglPipeline *pipeline;

  pipeline = rut_nine_slice_get_pipeline (fold->fold_up_icon);
  cogl_pipeline_set_color (pipeline, color);
  pipeline = rut_nine_slice_get_pipeline (fold->fold_down_icon);
  cogl_pipeline_set_color (pipeline, color);
}

void
rut_fold_set_label_color (RutFold *fold, const CoglColor *color)
{
  rut_text_set_color (fold->label, color);
}

void
rut_fold_set_font_name (RutFold *fold, const char *font)
{
  rut_text_set_font_name (fold->label, font);
}
