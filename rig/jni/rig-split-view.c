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

  int ref_count;

  RutContext *ctx;

  RutGraphableProps graphable;

  int width;
  int height;

  RigSplitViewSplit split;

  float split_fraction;

  RutTransform *child1_transform;

  RutObject *child0;
  RutObject *child1;

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

static void
_rig_split_view_free (void *object)
{
  RigSplitView *split_view = object;

  rig_split_view_set_child0 (split_view, NULL);
  rig_split_view_set_child1 (split_view, NULL);

  rut_graphable_remove_child (split_view->child1_transform);
  rut_refable_unref (split_view->child1_transform);

  rut_simple_introspectable_destroy (split_view);
  rut_graphable_destroy (split_view);

  g_slice_free (RigSplitView, split_view);
}

static void
rig_split_view_get_preferred_width (void *object,
                                    float for_height,
                                    float *min_width_p,
                                    float *natural_width_p)
{
  RigSplitView *split_view = object;
  float child0_min = 0;
  float child0_natural = 0;
  float child1_min = 0;
  float child1_natural = 0;

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    {
      int child0_for_height = for_height * split_view->split_fraction;
      int child1_for_height = for_height - child0_for_height;

      if (split_view->child0)
        {
          rut_sizable_get_preferred_width (split_view->child0,
                                           child0_for_height,
                                           &child0_min,
                                           &child0_natural);
        }

      if (split_view->child1)
        {
          rut_sizable_get_preferred_width (split_view->child0,
                                           child1_for_height,
                                           &child1_min,
                                           &child1_natural);
        }

      if (min_width_p)
        *min_width_p = MAX (child0_min, child1_min);
      if (natural_width_p)
        *natural_width_p = MAX (child0_natural, child1_natural);
    }
  else
    {
      float ratio0 = (1.0 - split_view->split_fraction) /
        split_view->split_fraction;
      float ratio1 = 1.0 / ratio0;

      if (split_view->child0)
        {
          rut_sizable_get_preferred_width (split_view->child0,
                                           for_height,
                                           &child0_min,
                                           &child0_natural);
        }

      if (split_view->child1)
        {
          rut_sizable_get_preferred_width (split_view->child0,
                                           for_height,
                                           &child1_min,
                                           &child1_natural);
        }

      if (min_width_p)
        {
          if (child0_min * ratio0 >= child1_min)
            *min_width_p = child0_min + child0_min * ratio0;
          else
            *min_width_p = child1_min + child1_min * ratio1;
        }

      if (natural_width_p)
        {
          if (child0_natural * ratio0 >= child1_natural)
            *natural_width_p = child0_natural + child0_natural * ratio0;
          else
            *natural_width_p = child1_natural + child1_natural * ratio1;
        }
    }
}

static void
rig_split_view_get_preferred_height (void *object,
                                     float for_width,
                                     float *min_height_p,
                                     float *natural_height_p)
{
  RigSplitView *split_view = object;
  float child0_min = 0;
  float child0_natural = 0;
  float child1_min = 0;
  float child1_natural = 0;

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    {
      int child0_for_width = for_width * split_view->split_fraction;
      int child1_for_width = for_width - child0_for_width;

      if (split_view->child0)
        {
          rut_sizable_get_preferred_height (split_view->child0,
                                            child0_for_width,
                                            &child0_min,
                                            &child0_natural);
        }

      if (split_view->child1)
        {
          rut_sizable_get_preferred_height (split_view->child0,
                                            child1_for_width,
                                            &child1_min,
                                            &child1_natural);
        }

      if (min_height_p)
        *min_height_p = MAX (child0_min, child1_min);
      if (natural_height_p)
        *natural_height_p = MAX (child0_natural, child1_natural);
    }
  else
    {
      float ratio0 = (1.0 - split_view->split_fraction) /
        split_view->split_fraction;
      float ratio1 = 1.0 / ratio0;

      if (split_view->child0)
        {
          rut_sizable_get_preferred_height (split_view->child0,
                                            for_width,
                                            &child0_min,
                                            &child0_natural);
        }

      if (split_view->child1)
        {
          rut_sizable_get_preferred_height (split_view->child0,
                                            for_width,
                                            &child1_min,
                                            &child1_natural);
        }

      if (min_height_p)
        {
          if (child0_min * ratio0 >= child1_min)
            *min_height_p = child0_min + child0_min * ratio0;
          else
            *min_height_p = child1_min + child1_min * ratio1;
        }

      if (natural_height_p)
        {
          if (child0_natural * ratio0 >= child1_natural)
            *natural_height_p = child0_natural + child0_natural * ratio0;
          else
            *natural_height_p = child1_natural + child1_natural * ratio1;
        }
    }
}


void
rig_split_view_get_size (RutObject *object,
                         float *width,
                         float *height)
{
  RigSplitView *split_view = object;

  *width = split_view->width;
  *height = split_view->height;
}

static void
allocate_cb (RutObject *graphable,
             void *user_data)
{
  RigSplitView *split_view = graphable;
  int width = split_view->width;
  int height = split_view->height;
  int child0_width, child0_height;
  RutRectangleInt geom1;

  geom1.x = geom1.y = 0;
  geom1.width = width;
  geom1.height = height;

  if (split_view->split == RIG_SPLIT_VIEW_SPLIT_VERTICAL)
    {
      int offset = split_view->split_fraction * split_view->width;
      child0_width = offset;
      child0_height = height;
      geom1.x = child0_width;
      geom1.width = width - geom1.x;
    }
  else if (split_view->split == RIG_SPLIT_VIEW_SPLIT_HORIZONTAL)
    {
      int offset = split_view->split_fraction * split_view->height;
      child0_width = width;
      child0_height = offset;
      geom1.y = child0_height;
      geom1.height = height - geom1.y;
    }

  if (split_view->child0)
    rut_sizable_set_size (split_view->child0,
                          child0_width,
                          child0_height);

  if (split_view->child1)
    {
      rut_transform_init_identity (split_view->child1_transform);

      rut_transform_translate (split_view->child1_transform,
                               geom1.x,
                               geom1.y,
                               0);

      rut_sizable_set_size (split_view->child1,
                            geom1.width,
                            geom1.height);
    }
}

static void
queue_allocation (RigSplitView *split_view)
{
  rut_shell_add_pre_paint_callback (split_view->ctx->shell,
                                    split_view,
                                    allocate_cb,
                                    NULL /* user_data */);
  rut_shell_queue_redraw (split_view->ctx->shell);
}

void
rig_split_view_set_size (RutObject *object,
                         float width,
                         float height)
{
  RigSplitView *split_view = object;

  if (split_view->width == width && split_view->height == height)
    return;

  split_view->width = width;
  split_view->height = height;

  queue_allocation (split_view);

  rut_property_dirty (&split_view->ctx->property_ctx,
                      &split_view->properties[RIG_SPLIT_VIEW_PROP_WIDTH]);
  rut_property_dirty (&split_view->ctx->property_ctx,
                      &split_view->properties[RIG_SPLIT_VIEW_PROP_HEIGHT]);
}

void
rig_split_view_set_width (RutObject *object,
                          float width)
{
  RigSplitView *split_view = object;

  rig_split_view_set_size (split_view, width, split_view->height);
}

void
rig_split_view_set_height (RutObject *object,
                           float height)
{
  RigSplitView *split_view = object;

  rig_split_view_set_size (split_view, split_view->width, height);
}

RutType rig_split_view_type;

static void
_rig_split_view_init_type (void)
{
  static RutRefCountableVTable refable_vtable = {
      rut_refable_simple_ref,
      rut_refable_simple_unref,
      _rig_split_view_free
  };

  static RutGraphableVTable graphable_vtable = {
      NULL, /* child removed */
      NULL, /* child addded */
      NULL /* parent changed */
  };

  static RutSizableVTable sizable_vtable = {
      rig_split_view_set_size,
      rig_split_view_get_size,
      rig_split_view_get_preferred_width,
      rig_split_view_get_preferred_height,
      NULL /* add_preferred_size_callback */
  };

  static RutIntrospectableVTable introspectable_vtable = {
      rut_simple_introspectable_lookup_property,
      rut_simple_introspectable_foreach_property
  };


  RutType *type = &rig_split_view_type;
#define TYPE RigSplitView

  rut_type_init (&rig_split_view_type, G_STRINGIFY (TYPE));
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

  split_view->ref_count = 1;

  rut_simple_introspectable_init (split_view,
                                  _rig_split_view_prop_specs,
                                  split_view->properties);

  rut_graphable_init (RUT_OBJECT (split_view));

  split_view->ctx = context;

  split_view->width = width;
  split_view->height = height;

  split_view->split = split;

  split_view->child1_transform = rut_transform_new (context);
  rut_graphable_add_child (split_view, split_view->child1_transform);

  queue_allocation (split_view);

  return split_view;
}

void
rig_split_view_set_split_fraction (RigSplitView *split_view,
                                   float fraction)
{
  g_return_if_fail (fraction != 0);

  split_view->split_fraction = fraction;

  queue_allocation (split_view);
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

  queue_allocation (split_view);
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

  queue_allocation (split_view);
}

#warning "need to handle add_preferred_size_callback"
