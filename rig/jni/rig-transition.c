/*
 * Rut
 *
 * Copyright (C) 2012  Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "rig-transition.h"

static RutPropertySpec _rig_transition_prop_specs[] = {
  {
    .name = "progress",
    .flags = RUT_PROPERTY_FLAG_READWRITE,
    .type = RUT_PROPERTY_TYPE_FLOAT,
    .data_offset = offsetof (RigTransition, progress)
  },
  { 0 }
};

static RutIntrospectableVTable _rig_transition_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};

RutType rig_transition_type;

static void
_rig_transition_type_init (void)
{
  rut_type_init (&rig_transition_type, "RigTransition");
  rut_type_add_interface (&rig_transition_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &_rig_transition_introspectable_vtable);
  rut_type_add_interface (&rig_transition_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (RigTransition, introspectable),
                          NULL); /* no implied vtable */
}

static void
free_prop_data_cb (void *user_data)
{
  RigTransitionPropData *prop_data = user_data;

  if (prop_data->path)
    rut_refable_unref (prop_data->path);

  rut_boxed_destroy (&prop_data->constant_value);

  g_slice_free (RigTransitionPropData, prop_data);
}

RigTransition *
rig_transition_new (RutContext *context,
                    uint32_t id)
{
  //CoglError *error = NULL;
  RigTransition *transition;
  static CoglBool initialized = FALSE;

  if (initialized == FALSE)
    {
      _rig_transition_type_init ();

      initialized = TRUE;
    }

  transition = g_slice_new0 (RigTransition);

  transition->id = id;
  transition->context = rut_refable_ref (context);

  rut_object_init (&transition->_parent, &rig_transition_type);

  rut_list_init (&transition->operation_cb_list);

  rut_simple_introspectable_init (transition, _rig_transition_prop_specs, transition->props);

  transition->progress = 0;

  transition->properties = g_hash_table_new_full (g_direct_hash,
                                                  g_direct_equal,
                                                  NULL, /* key_destroy */
                                                  free_prop_data_cb);

  return transition;
}

void
rig_transition_free (RigTransition *transition)
{
  rut_closure_list_disconnect_all (&transition->operation_cb_list);

  rut_simple_introspectable_destroy (transition);

  g_hash_table_destroy (transition->properties);

  rut_refable_unref (transition->context);

  g_slice_free (RigTransition, transition);
}

RigTransitionPropData *
rig_transition_find_prop_data_for_property (RigTransition *transition,
                                            RutProperty *property)
{
  return g_hash_table_lookup (transition->properties, property);
}

RigTransitionPropData *
rig_transition_get_prop_data_for_property (RigTransition *transition,
                                           RutProperty *property)
{
  RigTransitionPropData *prop_data;

  prop_data = rig_transition_find_prop_data_for_property (transition, property);

  if (prop_data == NULL)
    {
      prop_data = g_slice_new (RigTransitionPropData);
      prop_data->animated = FALSE;
      prop_data->property = property;
      prop_data->path = NULL;

      rut_property_box (property, &prop_data->constant_value);

      g_hash_table_insert (transition->properties,
                           property,
                           prop_data);

      rut_closure_list_invoke (&transition->operation_cb_list,
                               RigTransitionOperationCallback,
                               transition,
                               RIG_TRANSITION_OPERATION_ADDED,
                               prop_data);
    }

  return prop_data;
}

RigTransitionPropData *
rig_transition_get_prop_data (RigTransition *transition,
                              RutObject *object,
                              const char *property_name)
{
  RutProperty *property =
    rut_introspectable_lookup_property (object, property_name);

  if (!property)
    return NULL;

  return rig_transition_get_prop_data_for_property (transition, property);
}

RigPath *
rig_transition_find_path (RigTransition *transition,
                          RutProperty *property)
{
  RigTransitionPropData *prop_data;

  prop_data = rig_transition_find_prop_data_for_property (transition, property);

  return prop_data ? prop_data->path : NULL;
}

static RigPath *
rig_transition_get_path_for_prop_data (RigTransition *transition,
                                       RigTransitionPropData *prop_data)
{
  if (prop_data->path == NULL)
      prop_data->path = rig_path_new (transition->context,
                                      prop_data->property->spec->type);

  return prop_data->path;
}

RigPath *
rig_transition_get_path_for_property (RigTransition *transition,
                                      RutProperty *property)
{
  RigTransitionPropData *prop_data =
    rig_transition_get_prop_data_for_property (transition,
                                               property);

  if (!prop_data)
    return NULL;

  return rig_transition_get_path_for_prop_data (transition, prop_data);
}

RigPath *
rig_transition_get_path (RigTransition *transition,
                         RutObject *object,
                         const char *property_name)
{
  RigTransitionPropData *prop_data =
    rig_transition_get_prop_data (transition, object, property_name);

  if (!prop_data)
    return NULL;

  return rig_transition_get_path_for_prop_data (transition, prop_data);
}

static void
update_progress_cb (RigTransitionPropData *prop_data,
                    void *user_data)
{
  RigTransition *transition = user_data;

  if (prop_data->animated && prop_data->path)
    rig_path_lerp_property (prop_data->path,
                            prop_data->property,
                            transition->progress);
}

void
rig_transition_set_progress (RigTransition *transition,
                             float progress)
{
  transition->progress = progress;
  rut_property_dirty (&transition->context->property_ctx,
                      &transition->props[RUT_TRANSITION_PROP_PROGRESS]);

  rig_transition_foreach_property (transition,
                                   update_progress_cb,
                                   transition);
}

typedef struct
{
  RigTransitionForeachPropertyCb callback;
  void *user_data;
} ForeachPathData;

static void
foreach_path_cb (void *key,
                 void *value,
                 void *user_data)
{
  RigTransitionPropData *prop_data = value;
  ForeachPathData *foreach_data = user_data;

  foreach_data->callback (prop_data, foreach_data->user_data);
}

void
rig_transition_foreach_property (RigTransition *transition,
                                 RigTransitionForeachPropertyCb callback,
                                 void *user_data)
{
  ForeachPathData foreach_data;

  foreach_data.callback = callback;
  foreach_data.user_data = user_data;

  g_hash_table_foreach (transition->properties,
                        foreach_path_cb,
                        &foreach_data);
}

void
rig_transition_update_property (RigTransition *transition,
                                RutProperty *property)
{
  RigTransitionPropData *prop_data =
    rig_transition_find_prop_data_for_property (transition, property);

  /* Update the given property depending on what the transition thinks
   * it should currently be. This will either be calculated by
   * interpolating the path for the property or by using the constant
   * value depending on whether the property is animated */

  if (prop_data)
    {
      if (prop_data->animated)
        {
          if (prop_data->path)
            rig_path_lerp_property (prop_data->path,
                                    property,
                                    transition->progress);
        }
      else
        rut_property_set_boxed (&transition->context->property_ctx,
                                property,
                                &prop_data->constant_value);
    }
}

RutClosure *
rig_transition_add_operation_callback (RigTransition *transition,
                                       RigTransitionOperationCallback callback,
                                       void *user_data,
                                       RutClosureDestroyCallback destroy_cb)
{
  return rut_closure_list_add (&transition->operation_cb_list,
                               callback,
                               user_data,
                               destroy_cb);
}

void
rig_transition_set_property_animated (RigTransition *transition,
                                      RutProperty *property,
                                      CoglBool animated)
{
  RigTransitionPropData *prop_data;

  if (animated)
    {
      prop_data =
        rig_transition_get_prop_data_for_property (transition, property);
    }
  else
    {
      /* If the animated state is being disabled then we don't want to
       * create the property state if doesn't already exist */
      prop_data =
        rig_transition_find_prop_data_for_property (transition, property);

      if (prop_data == NULL)
        return;
    }

  if (animated != prop_data->animated)
    {
      prop_data->animated = animated;
      rut_closure_list_invoke (&transition->operation_cb_list,
                               RigTransitionOperationCallback,
                               transition,
                               RIG_TRANSITION_OPERATION_ANIMATED_CHANGED,
                               prop_data);
    }
}

void
rig_transition_remove_property (RigTransition *transition,
                                RutProperty *property)
{
  RigTransitionPropData *prop_data =
    rig_transition_find_prop_data_for_property (transition, property);

  if (prop_data)
    {
      rut_closure_list_invoke (&transition->operation_cb_list,
                               RigTransitionOperationCallback,
                               transition,
                               RIG_TRANSITION_OPERATION_REMOVED,
                               prop_data);

      g_hash_table_remove (transition->properties, property);
    }
}
