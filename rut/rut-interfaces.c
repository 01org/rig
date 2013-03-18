/*
 * Rut
 *
 * Copyright (C) 2012,2013  Intel Corporation
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
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "rut-object.h"
#include "rut-interfaces.h"
#include "rut-transform-private.h"
#include "rut-property.h"
#include "rut-util.h"
#include "components/rut-camera.h"
#include "rut-refcount-debug.h"

void *
rut_refable_simple_ref (void *object)
{
  int *ref_count = rut_object_get_properties (object,
                                               RUT_INTERFACE_ID_REF_COUNTABLE);
  (*ref_count)++;
  return object;
}

void
rut_refable_simple_unref (void *object)
{
  int *ref_count = rut_object_get_properties (object,
                                               RUT_INTERFACE_ID_REF_COUNTABLE);

  if (--(*ref_count) < 1)
    {
      RutRefCountableVTable *vtable =
        rut_object_get_vtable (object, RUT_INTERFACE_ID_REF_COUNTABLE);
      vtable->free (object);
    }
}

void *
rut_refable_ref (void *object)
{
  RutObject *obj = object;
  const RutType *type = rut_object_get_type (obj);

  RutRefCountableVTable *vtable =
    type->interfaces[RUT_INTERFACE_ID_REF_COUNTABLE].vtable;

  _rut_refcount_debug_ref (object);

  return vtable->ref (obj);
}

void
rut_refable_unref (void *object)
{
  RutObject *obj = object;
  const RutType *type = rut_object_get_type (obj);
  RutRefCountableVTable *vtable =
    type->interfaces[RUT_INTERFACE_ID_REF_COUNTABLE].vtable;

  _rut_refcount_debug_unref (object);

  vtable->unref (obj);
}

void
rut_graphable_init (RutObject *object)
{
  RutGraphableProps *props =
    rut_object_get_properties (object, RUT_INTERFACE_ID_GRAPHABLE);

  props->parent = NULL;
  props->children.head = NULL;
  props->children.tail = NULL;
  props->children.length = 0;
}

void
rut_graphable_destroy (RutObject *object)
{
  RutGraphableProps *props =
    rut_object_get_properties (object, RUT_INTERFACE_ID_GRAPHABLE);

  /* The node shouldn't have a parent, because if it did then it would
   * still have a reference and it shouldn't be being destroyed */
  g_warn_if_fail (props->parent == NULL);

  rut_graphable_remove_all_children (object);
}

void
rut_graphable_add_child (RutObject *parent, RutObject *child)
{
  RutGraphableProps *parent_props =
    rut_object_get_properties (parent, RUT_INTERFACE_ID_GRAPHABLE);
  RutGraphableVTable *parent_vtable =
    rut_object_get_vtable (parent, RUT_INTERFACE_ID_GRAPHABLE);
  RutGraphableProps *child_props =
    rut_object_get_properties (child, RUT_INTERFACE_ID_GRAPHABLE);
  RutGraphableVTable *child_vtable =
    rut_object_get_vtable (child, RUT_INTERFACE_ID_GRAPHABLE);
  RutObject *old_parent = child_props->parent;

  rut_refable_ref (child);

  if (old_parent)
    rut_graphable_remove_child (child);

  child_props->parent = parent;
  if (child_vtable && child_vtable->parent_changed)
    child_vtable->parent_changed (child, old_parent, parent);

  if (parent_vtable && parent_vtable->child_added)
    parent_vtable->child_added (parent, child);

  /* XXX: maybe this should be deferred to parent_vtable->child_added ? */
  g_queue_push_tail (&parent_props->children, child);
}

void
rut_graphable_remove_child (RutObject *child)
{
  RutGraphableProps *child_props =
    rut_object_get_properties (child, RUT_INTERFACE_ID_GRAPHABLE);
  RutObject *parent = child_props->parent;
  RutGraphableVTable *parent_vtable;
  RutGraphableProps *parent_props;

  if (!parent)
    return;

  parent_vtable = rut_object_get_vtable (parent, RUT_INTERFACE_ID_GRAPHABLE);
  parent_props = rut_object_get_properties (parent, RUT_INTERFACE_ID_GRAPHABLE);

  if (parent_vtable->child_removed)
    parent_vtable->child_removed (parent, child);

  g_queue_remove (&parent_props->children, child);
  child_props->parent = NULL;
  rut_refable_unref (child);
}

void
rut_graphable_remove_all_children (RutObject *parent)
{
  RutGraphableProps *parent_props =
    rut_object_get_properties (parent, RUT_INTERFACE_ID_GRAPHABLE);
  RutObject *child;

  while ((child = g_queue_pop_tail (&parent_props->children)))
    rut_graphable_remove_child (child);
}

RutObject *
rut_graphable_get_parent (RutObject *child)
{
  RutGraphableProps *child_props =
    rut_object_get_properties (child, RUT_INTERFACE_ID_GRAPHABLE);

  return child_props->parent;
}

static RutTraverseVisitFlags
_rut_graphable_traverse_breadth (RutObject *graphable,
                                 RutTraverseCallback callback,
                                 void *user_data)
{
  GQueue *queue = g_queue_new ();
  int dummy;
  int current_depth = 0;
  RutTraverseVisitFlags flags = 0;

  g_queue_push_tail (queue, graphable);
  g_queue_push_tail (queue, &dummy); /* use to delimit depth changes */

  while ((graphable = g_queue_pop_head (queue)))
    {
      if (graphable == &dummy)
        {
          current_depth++;
          g_queue_push_tail (queue, &dummy);
          continue;
        }

      flags = callback (graphable, current_depth, user_data);
      if (flags & RUT_TRAVERSE_VISIT_BREAK)
        break;
      else if (!(flags & RUT_TRAVERSE_VISIT_SKIP_CHILDREN))
        {
          RutGraphableProps *props =
            rut_object_get_properties (graphable, RUT_INTERFACE_ID_GRAPHABLE);
          GList *l;
          for (l = props->children.head; l; l = l->next)
            g_queue_push_tail (queue, l->data);
        }
    }

  g_queue_free (queue);

  return flags;
}

static RutTraverseVisitFlags
_rut_graphable_traverse_depth (RutObject *graphable,
                               RutTraverseCallback before_children_callback,
                               RutTraverseCallback after_children_callback,
                               int current_depth,
                               void *user_data)
{
  RutTraverseVisitFlags flags;

  flags = before_children_callback (graphable, current_depth, user_data);
  if (flags & RUT_TRAVERSE_VISIT_BREAK)
    return RUT_TRAVERSE_VISIT_BREAK;

  if (!(flags & RUT_TRAVERSE_VISIT_SKIP_CHILDREN))
    {
      RutGraphableProps *props =
        rut_object_get_properties (graphable, RUT_INTERFACE_ID_GRAPHABLE);
      GList *l;

      for (l = props->children.head; l; l = l->next)
        {
          flags = _rut_graphable_traverse_depth (l->data,
                                                 before_children_callback,
                                                 after_children_callback,
                                                 current_depth + 1,
                                                 user_data);

          if (flags & RUT_TRAVERSE_VISIT_BREAK)
            return RUT_TRAVERSE_VISIT_BREAK;
        }
    }

  if (after_children_callback)
    return after_children_callback (graphable, current_depth, user_data);
  else
    return RUT_TRAVERSE_VISIT_CONTINUE;
}

/* rut_graphable_traverse:
 * @graphable: The graphable object to start traversing the graph from
 * @flags: These flags may affect how the traversal is done
 * @before_children_callback: A function to call before visiting the
 *   children of the current object.
 * @after_children_callback: A function to call after visiting the
 *   children of the current object. (Ignored if
 *   %RUT_TRAVERSE_BREADTH_FIRST is passed to @flags.)
 * @user_data: The private data to pass to the callbacks
 *
 * Traverses the graph starting at the specified @root and descending
 * through all its children and its children's children.  For each
 * object traversed @before_children_callback and
 * @after_children_callback are called with the specified @user_data,
 * before and after visiting that object's children.
 *
 * The callbacks can return flags that affect the ongoing traversal
 * such as by skipping over an object's children or bailing out of
 * any further traversing.
 */
RutTraverseVisitFlags
rut_graphable_traverse (RutObject *root,
                        RutTraverseFlags flags,
                        RutTraverseCallback before_children_callback,
                        RutTraverseCallback after_children_callback,
                        void *user_data)
{
  if (flags & RUT_TRAVERSE_BREADTH_FIRST)
    return _rut_graphable_traverse_breadth (root,
                                            before_children_callback,
                                            user_data);
  else /* DEPTH_FIRST */
    return _rut_graphable_traverse_depth (root,
                                          before_children_callback,
                                          after_children_callback,
                                          0, /* start depth */
                                          user_data);
}

#if 0
static RutTraverseVisitFlags
_rut_graphable_paint_cb (RutObject *object,
                         int depth,
                         void *user_data)
{
  RutPaintContext *paint_ctx = user_data;
  RutPaintableVTable *vtable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_PAINTABLE);

  vtable->paint (object, paint_ctx);

  return RUT_TRAVERSE_VISIT_CONTINUE;
}

void
rut_graphable_paint (RutObject *root,
                     RutCamera *camera)
{
  RutPaintContext paint_ctx;

  paint_ctx.camera = camera;

  rut_graphable_traverse (root,
                           RUT_TRAVERSE_DEPTH_FIRST,
                           _rut_graphable_paint_cb,
                           NULL, /* after callback */
                           &paint_ctx);
}
#endif

#if 0
RutCamera *
rut_graphable_find_camera (RutObject *object)
{
  do {
    RutGraphableProps *graphable_priv;

    if (rut_object_get_type (object) == &rut_camera_type)
      return RUT_CAMERA (object);

    graphable_priv =
      rut_object_get_properties (object, RUT_INTERFACE_ID_GRAPHABLE);

    object = graphable_priv->parent;

  } while (object);

  return NULL;
}
#endif

void
rut_graphable_apply_transform (RutObject *graphable,
                               CoglMatrix *transform_matrix)
{
  int depth = 0;
  RutObject **transform_nodes;
  RutObject *node = graphable;
  int i;

  do {
    RutGraphableProps *graphable_priv =
      rut_object_get_properties (node, RUT_INTERFACE_ID_GRAPHABLE);

    depth++;

    node = graphable_priv->parent;
  } while (node);

  transform_nodes = g_alloca (sizeof (RutObject *) * depth);

  node = graphable;
  i = 0;
  do {
    RutGraphableProps *graphable_priv;

    if (rut_object_is (node, RUT_INTERFACE_ID_TRANSFORMABLE))
      transform_nodes[i++] = node;

    graphable_priv =
      rut_object_get_properties (node, RUT_INTERFACE_ID_GRAPHABLE);
    node = graphable_priv->parent;
  } while (node);

  for (i--; i >= 0; i--)
    {
      const CoglMatrix *matrix = rut_transformable_get_matrix (transform_nodes[i]);
      cogl_matrix_multiply (transform_matrix, transform_matrix, matrix);
    }
}

void
rut_graphable_get_transform (RutObject *graphable,
                             CoglMatrix *transform)
{
  cogl_matrix_init_identity (transform);
  rut_graphable_apply_transform (graphable, transform);
}

void
rut_graphable_get_modelview (RutObject *graphable,
                             RutCamera *camera,
                             CoglMatrix *transform)
{
  const CoglMatrix *view = rut_camera_get_view_transform (camera);
  *transform = *view;
  rut_graphable_apply_transform (graphable, transform);
}

void
rut_graphable_fully_transform_point (RutObject *graphable,
                                     RutCamera *camera,
                                     float *x,
                                     float *y,
                                     float *z)
{
  CoglMatrix modelview;
  const CoglMatrix *projection;
  const float *viewport;
  float point[3] = { *x, *y, *z };

  rut_graphable_get_modelview (graphable, camera, &modelview);
  projection = rut_camera_get_projection (camera);
  viewport = rut_camera_get_viewport (camera);

  rut_util_fully_transform_vertices (&modelview,
                                     projection,
                                     viewport,
                                     point,
                                     point,
                                     1);

  *x = point[0];
  *y = point[1];
  *z = point[2];
}

RutProperty *
rut_introspectable_lookup_property (RutObject *object,
                                    const char *name)
{
  RutIntrospectableVTable *introspectable_vtable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_INTROSPECTABLE);

  return introspectable_vtable->lookup_property (object, name);
}

void
rut_introspectable_foreach_property (RutObject *object,
                                     RutIntrospectablePropertyCallback callback,
                                     void *user_data)
{
  RutIntrospectableVTable *introspectable_vtable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_INTROSPECTABLE);

  introspectable_vtable->foreach_property (object, callback, user_data);
}

#if 0
void
rut_simple_introspectable_register_properties (RutObject *object,
                                               RutProperty *first_property)
{
  RutSimpleIntrospectableProps *props =
    rut_object_get_properties (object, RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE);
  RutPropertySpec *first_spec = first_property->spec;
  int n;

  for (n = 0; first_spec[n].name; n++)
    ;

  props->first_property = first_property;
  props->n_properties = n;
}
#endif

void
rut_simple_introspectable_init (RutObject *object,
                                RutPropertySpec *specs,
                                RutProperty *properties)
{
  RutSimpleIntrospectableProps *props =
    rut_object_get_properties (object, RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE);
  int n;

  for (n = 0; specs[n].name; n++)
    {
      rut_property_init (&properties[n],
                         &specs[n],
                         object);
    }

  props->first_property = properties;
  props->n_properties = n;
}


void
rut_simple_introspectable_destroy (RutObject *object)
{
  RutSimpleIntrospectableProps *props =
    rut_object_get_properties (object, RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE);
  RutProperty *properties = props->first_property;
  int i;

  for (i = 0; i < props->n_properties; i++)
    rut_property_destroy (&properties[i]);
}

RutProperty *
rut_simple_introspectable_lookup_property (RutObject *object,
                                           const char *name)
{
  RutSimpleIntrospectableProps *priv =
    rut_object_get_properties (object, RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE);
  int i;

  for (i = 0; i < priv->n_properties; i++)
    {
      RutProperty *property = priv->first_property + i;
      if (strcmp (property->spec->name, name) == 0)
        return property;
    }

  return NULL;
}

void
rut_simple_introspectable_foreach_property (RutObject *object,
                                            RutIntrospectablePropertyCallback callback,
                                            void *user_data)
{
  RutSimpleIntrospectableProps *priv =
    rut_object_get_properties (object, RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE);
  int i;

  for (i = 0; i < priv->n_properties; i++)
    {
      RutProperty *property = priv->first_property + i;
      callback (property, user_data);
    }
}

const CoglMatrix *
rut_transformable_get_matrix (RutObject *object)
{
  RutTransformableVTable *transformable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_TRANSFORMABLE);

  return transformable->get_matrix (object);
}

void
rut_sizable_set_size (RutObject *object,
                      float width,
                      float height)
{
  RutSizableVTable *sizable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_SIZABLE);

  sizable->set_size (object,
                     width,
                     height);
}

void
rut_sizable_get_size (void *object,
                      float *width,
                      float *height)
{
  RutSizableVTable *sizable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_SIZABLE);

  sizable->get_size (object,
                     width,
                     height);
}

void
rut_sizable_get_preferred_width (RutObject *object,
                                 float for_height,
                                 float *min_width_p,
                                 float *natural_width_p)
{
  RutSizableVTable *sizable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_SIZABLE);

  sizable->get_preferred_width (object,
                                for_height,
                                min_width_p,
                                natural_width_p);
}

void
rut_sizable_get_preferred_height (RutObject *object,
                                  float for_width,
                                  float *min_height_p,
                                  float *natural_height_p)
{
  RutSizableVTable *sizable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_SIZABLE);

  sizable->get_preferred_height (object,
                                 for_width,
                                 min_height_p,
                                 natural_height_p);
}

void
rut_simple_sizable_get_preferred_width (void *object,
                                        float for_height,
                                        float *min_width_p,
                                        float *natural_width_p)
{
  if (min_width_p)
    *min_width_p = 0;
  if (natural_width_p)
    *natural_width_p = 0;
}

void
rut_simple_sizable_get_preferred_height (void *object,
                                         float for_width,
                                         float *min_height_p,
                                         float *natural_height_p)
{
  if (min_height_p)
    *min_height_p = 0;
  if (natural_height_p)
    *natural_height_p = 0;
}

RutClosure *
rut_sizable_add_preferred_size_callback (RutObject *object,
                                         RutSizablePreferredSizeCallback cb,
                                         void *user_data,
                                         RutClosureDestroyCallback destroy_cb)
{
  RutSizableVTable *sizable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_SIZABLE);

  /* If the object has no implementation for the needs layout callback
   * then we'll assume its preferred size never changes. We'll return
   * a dummy closure object that will never be invoked so that the
   * rest of the code doesn't need to handle this specially */
  if (sizable->add_preferred_size_callback == NULL)
    {
      RutList dummy_list;
      RutClosure *closure;

      rut_list_init (&dummy_list);

      closure = rut_closure_list_add (&dummy_list,
                                      cb,
                                      user_data,
                                      destroy_cb);

      rut_list_init (&closure->list_node);

      return closure;
    }
  else
    return sizable->add_preferred_size_callback (object,
                                                 cb,
                                                 user_data,
                                                 destroy_cb);
}

CoglPrimitive *
rut_primable_get_primitive (RutObject *object)
{
  RutPrimableVTable *primable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_PRIMABLE);

  return primable->get_primitive (object);
}

void *
rut_pickable_get_mesh (RutObject *object)
{
  RutPickableVTable *pickable =
    rut_object_get_vtable (object, RUT_INTERFACE_ID_PICKABLE);

  return pickable->get_mesh (object);
}

RutInputRegion *
rut_inputable_get_input_region (RutObject *object)
{
  RutInputableProps *props =
    rut_object_get_properties (object, RUT_INTERFACE_ID_INPUTABLE);

  return props->input_region;
}
