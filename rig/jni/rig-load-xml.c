/*
 * Rig
 *
 * Copyright (C) 2013  Intel Corporation
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

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <glib.h>

#include "rig-engine.h"

enum {
  LOADER_STATE_NONE,
  LOADER_STATE_LOADING_ENTITY,
  LOADER_STATE_LOADING_MATERIAL_COMPONENT,
  LOADER_STATE_LOADING_MODEL_COMPONENT,
  LOADER_STATE_LOADING_SHAPE_COMPONENT,
  LOADER_STATE_LOADING_DIAMOND_COMPONENT,
  LOADER_STATE_LOADING_LIGHT_COMPONENT,
  LOADER_STATE_LOADING_CAMERA_COMPONENT,
  LOADER_STATE_LOADING_TRANSITION,
  LOADER_STATE_LOADING_PROPERTY,
  LOADER_STATE_LOADING_CONSTANT,
  LOADER_STATE_LOADING_PATH
};

typedef struct _Loader
{
  RigEngine *engine;

  GList *assets;
  GList *entities;
  RutEntity *light;
  GList *controllers;

  GHashTable *id_map;

  GQueue state;
  uint32_t id;
  CoglBool texture_specified;
  CoglBool normal_map_specified;
  CoglBool alpha_mask_specified;
  uint32_t texture_asset_id;
  uint32_t normal_map_asset_id;
  uint32_t alpha_mask_asset_id;

  CoglBool device_found;
  int device_width;
  int device_height;
  CoglColor background;
  CoglBool background_set;

  int component_id;

  CoglColor material_ambient;
  CoglBool ambient_set;
  CoglColor material_diffuse;
  CoglBool diffuse_set;
  CoglColor material_specular;
  CoglBool specular_set;
  float material_shininess;
  CoglBool shininess_set;

  CoglBool shaped;
  float diamond_size;
  RutEntity *current_entity;
  CoglBool is_light;

  RigController *current_controller;
  RigControllerPropData *current_property;
  RigPath *current_path;
} Loader;

static void
loader_push_state (Loader *loader, int state)
{
  g_queue_push_tail (&loader->state, GINT_TO_POINTER (state));
}

static int
loader_get_state (Loader *loader)
{
  void *state = g_queue_peek_tail (&loader->state);
  return GPOINTER_TO_INT (state);
}

static void
loader_pop_state (Loader *loader)
{
  g_queue_pop_tail (&loader->state);
}

static RutEntity *
loader_find_entity (Loader *loader, uint64_t id)
{
  RutObject *object = g_hash_table_lookup (loader->id_map, &id);
  if (object == NULL || rut_object_get_type (object) != &rut_entity_type)
    return NULL;
  return RUT_ENTITY (object);
}

static RutAsset *
loader_find_asset (Loader *loader, uint64_t id)
{
  RutObject *object = g_hash_table_lookup (loader->id_map, &id);
  if (object == NULL || rut_object_get_type (object) != &rut_asset_type)
    return NULL;
  return RUT_ASSET (object);
}

static RutEntity *
loader_find_introspectable (Loader *loader, uint64_t id)
{
  RutObject *object = g_hash_table_lookup (loader->id_map, &id);
  if (object == NULL ||
      !rut_object_is (object, RUT_INTERFACE_ID_INTROSPECTABLE) ||
      !rut_object_is (object, RUT_INTERFACE_ID_REF_COUNTABLE))
    return NULL;
  return RUT_ENTITY (object);
}

static CoglBool
load_float (float *value,
            const char *str)
{
  *value = g_ascii_strtod (str, NULL);
  return TRUE;
}

static CoglBool
load_double (double *value,
             const char *str)
{
  *value = g_ascii_strtod (str, NULL);
  return TRUE;
}

static CoglBool
load_integer (int *value,
              const char *str)
{
  *value = g_ascii_strtoll (str, NULL, 10);
  return TRUE;
}

static CoglBool
load_uint32 (uint32_t *value,
             const char *str)
{
  *value = g_ascii_strtoull (str, NULL, 10);
  return TRUE;
}

static CoglBool
load_boolean (CoglBool *value,
              const char *str)
{
  /* Borrowed from gmarkup.c in glib */
  char const * const falses[] = { "false", "f", "no", "n", "0" };
  char const * const trues[] = { "true", "t", "yes", "y", "1" };
  int i;

  for (i = 0; i < G_N_ELEMENTS (falses); i++)
    {
      if (g_ascii_strcasecmp (str, falses[i]) == 0)
        {
          *value = FALSE;

          return TRUE;
        }
    }

  for (i = 0; i < G_N_ELEMENTS (trues); i++)
    {
      if (g_ascii_strcasecmp (str, trues[i]) == 0)
        {
          *value = TRUE;

          return TRUE;
        }
    }

  return FALSE;
}

static CoglBool
load_text (char **value,
           const char *str)
{
  *value = g_strdup (str);
  return TRUE;
}

static CoglBool
load_vec3 (float *value,
           const char *str)
{
  return sscanf (str, "(%f, %f, %f)",
                 &value[0], &value[1], &value[2]) == 3;
}

static CoglBool
load_vec4 (float *value,
           const char *str)
{
  return sscanf (str, "(%f, %f, %f, %f)",
                 &value[0], &value[1], &value[2], &value[3]) == 4;
}

static CoglBool
load_color (CoglColor *value,
            const char *str)
{
  return sscanf (str, "(%f, %f, %f, %f)",
                 &value->red, &value->green, &value->blue, &value->alpha) == 4;
}

static CoglBool
load_quaternion (CoglQuaternion *value,
                 const char *str)
{
  float angle, x, y, z;

  if (sscanf (str, "[%f (%f, %f, %f)]", &angle, &x, &y, &z) != 4)
    return FALSE;

  cogl_quaternion_init (value, angle, x, y, z);

  return TRUE;
}

static CoglBool
load_boxed_value (RutBoxed *value,
                  RutPropertyType type,
                  const char *str,
                  GError **error)
{
  value->type = type;

  switch (type)
    {
    case RUT_PROPERTY_TYPE_FLOAT:
      if (!load_float (&value->d.float_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_DOUBLE:
      if (!load_double (&value->d.double_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_INTEGER:
      if (!load_integer (&value->d.integer_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_ENUM:
      /* FIXME: this should probably load the string name rather than
       * the integer value */
      if (!load_integer (&value->d.enum_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_UINT32:
      if (!load_uint32 (&value->d.uint32_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_BOOLEAN:
      if (!load_boolean (&value->d.boolean_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_TEXT:
      if (!load_text (&value->d.text_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_QUATERNION:
      if (!load_quaternion (&value->d.quaternion_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_VEC3:
      if (!load_vec3 (value->d.vec3_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_VEC4:
      if (!load_vec4 (value->d.vec4_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_COLOR:
      if (!load_color (&value->d.color_val, str))
        goto error;
      return TRUE;

    case RUT_PROPERTY_TYPE_OBJECT:
    case RUT_PROPERTY_TYPE_POINTER:
      break;
    }

  g_warn_if_reached ();

 error:
  g_set_error (error,
               G_MARKUP_ERROR,
               G_MARKUP_ERROR_INVALID_CONTENT,
               "Invalid value encountered");

  return FALSE;
}

static CoglBool
load_path_node (RigPath *path,
                float t,
                const char *value_str,
                GError **error)
{
  switch (path->type)
    {
    case RUT_PROPERTY_TYPE_FLOAT:
      {
        float value;
        if (!load_float (&value, value_str))
          goto error;
        rig_path_insert_float (path, t, value);
        return TRUE;
      }
    case RUT_PROPERTY_TYPE_DOUBLE:
      {
        double value;
        if (!load_double (&value, value_str))
          goto error;
        rig_path_insert_double (path, t, value);
        return TRUE;
      }
    case RUT_PROPERTY_TYPE_INTEGER:
      {
        int value;
        if (!load_integer (&value, value_str))
          goto error;
        rig_path_insert_integer (path, t, value);
        return TRUE;
      }
    case RUT_PROPERTY_TYPE_UINT32:
      {
        uint32_t value;
        if (!load_uint32 (&value, value_str))
          goto error;
        rig_path_insert_uint32 (path, t, value);
        return TRUE;
      }
    case RUT_PROPERTY_TYPE_VEC3:
      {
        float value[3];
        if (!load_vec3 (value, value_str))
          goto error;
        rig_path_insert_vec3 (path, t, value);
        return TRUE;
      }
    case RUT_PROPERTY_TYPE_VEC4:
      {
        float value[4];
        if (!load_vec4 (value, value_str))
          goto error;
        rig_path_insert_vec4 (path, t, value);
        return TRUE;
      }
    case RUT_PROPERTY_TYPE_COLOR:
      {
        CoglColor value;
        if (!load_color (&value, value_str))
          goto error;
        rig_path_insert_color (path, t, &value);
        return TRUE;
      }
    case RUT_PROPERTY_TYPE_QUATERNION:
      {
        CoglQuaternion value;
        if (!load_quaternion (&value, value_str))
          goto error;
        rig_path_insert_quaternion (path, t, &value);
        return TRUE;
      }

      /* These shouldn't be animatable */
    case RUT_PROPERTY_TYPE_BOOLEAN:
    case RUT_PROPERTY_TYPE_TEXT:
    case RUT_PROPERTY_TYPE_ENUM:
    case RUT_PROPERTY_TYPE_OBJECT:
    case RUT_PROPERTY_TYPE_POINTER:
      g_warn_if_reached ();
      goto error;
    }

  g_warn_if_reached ();

 error:
  g_set_error (error,
               G_MARKUP_ERROR,
               G_MARKUP_ERROR_INVALID_CONTENT,
               "Invalid value encountered");

  return FALSE;
}

static void
collect_error (Loader *loader,
               const char *format,
               ...)
{
  va_list ap;

  va_start (ap, format);

  /* XXX: The intention is that we shouldn't just immediately abort loading
   * like this but rather we should collect the errors and try our best to
   * continue loading. At the end we can report the errors to the user so the
   * realize that their document may be corrupt.
   */

  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, format, ap);

  va_end (ap);
}

static void
register_loader_object (Loader *loader,
                        void *object,
                        uint64_t id)
{
  uint64_t *key = g_slice_new (uint64_t);

  *key = id;

  g_return_if_fail (id != 0);

  if (g_hash_table_lookup (loader->id_map, key))
    {
      collect_error (loader, "Duplicate loader object id %ld", id);
      return;
    }

  g_hash_table_insert (loader->id_map, key, object);
}

static CoglBool
check_and_set_id (Loader *loader,
                  uint64_t id,
                  void *object,
                  GError **error)
{
  /* XXX: XML COMPATIBILITY: The id for components shouldn't be
   * optional. This is to retain compatibility with version 1 of Rig
   * which didn't add an id */
  if (id == 0)
    return TRUE;

  if (g_hash_table_lookup (loader->id_map, &id))
    {
      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_INVALID_CONTENT,
                   "Duplicate id %d", (int)id);
      return FALSE;
    }

  register_loader_object (loader, object, id);

  return TRUE;
}

static CoglBool
parse_and_set_id (Loader *loader,
                  const char *id_str,
                  void *object,
                  GError **error)
{
  /* FIXME: XML COMPATIBILITY: The id for components shouldn't be
   * optional. This is to retain compatibility with version 1 of Rig
   * which didn't add an id */
  if (id_str)
    {
      int id = g_ascii_strtoull (id_str, NULL, 10);

      return check_and_set_id (loader, id, object, error);
    }
  else
    return TRUE;
}

static void
parse_start_element (GMarkupParseContext *context,
                     const char *element_name,
                     const char **attribute_names,
                     const char **attribute_values,
                     void *user_data,
                     GError **error)
{
  Loader *loader = user_data;
  RigEngine *engine = loader->engine;
  int state = loader_get_state (loader);

  if (state == LOADER_STATE_NONE &&
      strcmp (element_name, "device") == 0)
    {
      const char *width_str;
      const char *height_str;
      const char *background_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "width",
                                        &width_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "height",
                                        &height_str,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "background",
                                        &background_str,
                                        G_MARKUP_COLLECT_INVALID))
        {
          return;
        }

      loader->device_found = TRUE;
      loader->device_width = g_ascii_strtoull (width_str, NULL, 10);
      loader->device_height = g_ascii_strtoull (height_str, NULL, 10);

      if (background_str)
        {
          rut_color_init_from_string (loader->engine->ctx,
                                      &loader->background,
                                      background_str);
          loader->background_set = TRUE;
        }
    }
  else if (state == LOADER_STATE_NONE &&
           strcmp (element_name, "asset") == 0)
    {
      const char *id_str;
      const char *path;
      uint64_t id;
      char *full_path;
      GFile *asset_file;
      GFileInfo *info;
      RutAsset *asset = NULL;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "id",
                                        &id_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "path",
                                        &path,
                                        G_MARKUP_COLLECT_INVALID))
        {
          return;
        }

      id = g_ascii_strtoull (id_str, NULL, 10);
      if (g_hash_table_lookup (loader->id_map, &id))
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Duplicate id %d", (int)id);
          return;
        }

      full_path = g_build_filename (engine->ctx->assets_location, path, NULL);
      asset_file = g_file_new_for_path (full_path);
      info = g_file_query_info (asset_file,
                                "standard::*",
                                G_FILE_QUERY_INFO_NONE,
                                NULL,
                                NULL);
      if (info)
        {
          asset = rig_load_asset (engine, info, asset_file);
          if (asset)
            {
              loader->assets = g_list_prepend (loader->assets, asset);
              register_loader_object (loader, asset, id);
            }
          g_object_unref (info);
        }

      g_object_unref (asset_file);
      g_free (full_path);
    }
  else if (state == LOADER_STATE_NONE &&
           strcmp (element_name, "entity") == 0)
    {
      const char *id_str;
      uint64_t id;
      RutEntity *entity;
      const char *parent_id_str;
      const char *label_str;
      const char *position_str;
      const char *rotation_str;
      const char *scale_str;
      const char *cast_shadow_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "id",
                                        &id_str,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "parent",
                                        &parent_id_str,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "label",
                                        &label_str,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "position",
                                        &position_str,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "rotation",
                                        &rotation_str,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "scale",
                                        &scale_str,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "cast_shadow",
                                        &cast_shadow_str,
                                        G_MARKUP_COLLECT_INVALID))
      {
        return;
      }

      id = g_ascii_strtoull (id_str, NULL, 10);
      if (g_hash_table_lookup (loader->id_map, &id))
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Duplicate entity id %d", (int)id);
          return;
        }

      entity = rut_entity_new (loader->engine->ctx);

      if (parent_id_str)
        {
          uint64_t parent_id = g_ascii_strtoull (parent_id_str, NULL, 10);
          RutEntity *parent = loader_find_entity (loader, parent_id);

          if (!parent)
            {
              g_set_error (error,
                           G_MARKUP_ERROR,
                           G_MARKUP_ERROR_INVALID_CONTENT,
                           "Invalid parent id referenced in entity element");
              rut_refable_unref (entity);
              return;
            }

          rut_graphable_add_child (parent, entity);
        }

      if (label_str)
        rut_entity_set_label (entity, label_str);

      if (position_str)
        {
          float pos[3];
          if (sscanf (position_str, "(%f, %f, %f)",
                      &pos[0], &pos[1], &pos[2]) != 3)
            {
              g_set_error (error,
                           G_MARKUP_ERROR,
                           G_MARKUP_ERROR_INVALID_CONTENT,
                           "Invalid entity position");
              return;
            }
          rut_entity_set_position (entity, pos);
        }
      if (rotation_str)
        {
          float angle;
          float axis[3];
          CoglQuaternion q;

          if (sscanf (rotation_str, "[%f (%f, %f, %f)]",
                      &angle, &axis[0], &axis[1], &axis[2]) != 4)
            {
              g_set_error (error,
                           G_MARKUP_ERROR,
                           G_MARKUP_ERROR_INVALID_CONTENT,
                           "Invalid entity rotation");
              return;
            }
          cogl_quaternion_init_from_angle_vector (&q, angle, axis);
          rut_entity_set_rotation (entity, &q);
        }
      if (scale_str)
        {
          double scale = g_ascii_strtod (scale_str, NULL);
          rut_entity_set_scale (entity, scale);
        }
      if (cast_shadow_str)
        {
          if (strcmp (cast_shadow_str, "yes") == 0)
            rut_entity_set_cast_shadow (entity, TRUE);
          else if (strcmp (cast_shadow_str, "no") == 0)
            rut_entity_set_cast_shadow (entity, FALSE);
          else
            {
              g_set_error (error,
                           G_MARKUP_ERROR,
                           G_MARKUP_ERROR_INVALID_CONTENT,
                           "Invalid cast_shadow value");
              return;
            }
        }

      loader->current_entity = entity;
      loader->is_light = FALSE;

      register_loader_object (loader, entity, id);

      loader_push_state (loader, LOADER_STATE_LOADING_ENTITY);
    }
  else if (state == LOADER_STATE_LOADING_ENTITY &&
           strcmp (element_name, "material") == 0)
    {
      const char *id_str, *color_str, *ambient_str, *diffuse_str, *specular_str;
      const char *shininess_str;

      loader->texture_specified = FALSE;
      loader->normal_map_specified = FALSE;
      loader->alpha_mask_specified = FALSE;
      loader_push_state (loader, LOADER_STATE_LOADING_MATERIAL_COMPONENT);

      g_markup_collect_attributes (element_name,
                                   attribute_names,
                                   attribute_values,
                                   error,
                                   G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                   "id",
                                   &id_str,
                                   G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                   "color",
                                   &color_str,
                                   G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                   "ambient",
                                   &ambient_str,
                                   G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                   "diffuse",
                                   &diffuse_str,
                                   G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                   "specular",
                                   &specular_str,
                                   G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                   "shininess",
                                   &shininess_str,
                                   G_MARKUP_COLLECT_INVALID);

      loader->component_id = id_str ? g_ascii_strtoull (id_str, NULL, 10) : 0;

      /* XXX: This is a depecated attribute, remove once existing xml
       * files have stopped using it... */
      if (color_str && diffuse_str == NULL)
        diffuse_str = color_str;

      if (ambient_str)
        {
          rut_color_init_from_string (loader->engine->ctx,
                                      &loader->material_ambient,
                                      ambient_str);
          loader->ambient_set = TRUE;
        }
      else
        loader->ambient_set = FALSE;

      if (diffuse_str)
        {
          rut_color_init_from_string (loader->engine->ctx,
                                      &loader->material_diffuse,
                                      diffuse_str);
          loader->diffuse_set = TRUE;
        }
      else
        loader->diffuse_set = FALSE;

      if (specular_str)
        {
          rut_color_init_from_string (loader->engine->ctx,
                                      &loader->material_specular,
                                      specular_str);
          loader->specular_set = TRUE;
        }
      else
        loader->specular_set = FALSE;

      if (shininess_str)
        {
          loader->material_shininess = g_ascii_strtod (shininess_str, NULL);
          loader->shininess_set = TRUE;
        }
      else
        loader->shininess_set = FALSE;
    }
  else if (state == LOADER_STATE_LOADING_ENTITY &&
           strcmp (element_name, "light") == 0)
    {
      const char *id_str;
      const char *ambient_str;
      const char *diffuse_str;
      const char *specular_str;
      CoglColor ambient;
      CoglColor diffuse;
      CoglColor specular;
      RutLight *light;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING |
                                        G_MARKUP_COLLECT_OPTIONAL,
                                        "id",
                                        &id_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "ambient",
                                        &ambient_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "diffuse",
                                        &diffuse_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "specular",
                                        &specular_str,
                                        G_MARKUP_COLLECT_INVALID))
        {
          return;
        }

      rut_color_init_from_string (loader->engine->ctx, &ambient, ambient_str);
      rut_color_init_from_string (loader->engine->ctx, &diffuse, diffuse_str);
      rut_color_init_from_string (loader->engine->ctx, &specular, specular_str);

      light = rut_light_new (loader->engine->ctx);
      rut_light_set_ambient (light, &ambient);
      rut_light_set_diffuse (light, &diffuse);
      rut_light_set_specular (light, &specular);

      if (!parse_and_set_id (loader, id_str, light, error))
        return;

      rut_entity_add_component (loader->current_entity, light);

      loader->is_light = TRUE;

      //loader_push_state (loader, LOADER_STATE_LOADING_LIGHT_COMPONENT);
    }
  else if (state == LOADER_STATE_LOADING_ENTITY &&
           strcmp (element_name, "shape") == 0)
    {
      const char *id_str, *shaped_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING |
                                        G_MARKUP_COLLECT_OPTIONAL,
                                        "id",
                                        &id_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "shaped",
                                        &shaped_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      loader->component_id = id_str ? g_ascii_strtoull (id_str, NULL, 10) : 0;

      if (strcmp (shaped_str, "true") == 0)
        loader->shaped = TRUE;
      else if (strcmp (shaped_str, "false") == 0)
        loader->shaped = FALSE;
      else
        {
          g_warn_if_reached ();
          loader->shaped = FALSE;
        }

      loader_push_state (loader, LOADER_STATE_LOADING_SHAPE_COMPONENT);
    }
  else if (state == LOADER_STATE_LOADING_ENTITY &&
           strcmp (element_name, "diamond") == 0)
    {
      const char *id_str, *size_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING |
                                        G_MARKUP_COLLECT_OPTIONAL,
                                        "id",
                                        &id_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "size",
                                        &size_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      loader->component_id = id_str ? g_ascii_strtoull (id_str, NULL, 10) : 0;

      loader->diamond_size = g_ascii_strtod (size_str, NULL);

      loader_push_state (loader, LOADER_STATE_LOADING_DIAMOND_COMPONENT);
    }
  else if (state == LOADER_STATE_LOADING_ENTITY &&
           strcmp (element_name, "model") == 0)
    {
      const char *id_str, *asset_id_str;
      uint32_t asset_id;
      RutAsset *asset;
      RutModel *model;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING |
                                        G_MARKUP_COLLECT_OPTIONAL,
                                        "id",
                                        &id_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "asset",
                                        &asset_id_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      asset_id = g_ascii_strtoull (asset_id_str, NULL, 10);
      asset = loader_find_asset (loader, asset_id);
      if (!asset)
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Invalid asset id");
          return;
        }

      model = rut_asset_get_model (asset);
      if (model)
        {
          rut_refable_unref (asset);
          rut_entity_add_component (loader->current_entity, model);
        }

      if (!parse_and_set_id (loader, id_str, model, error))
        return;
    }
  else if (state == LOADER_STATE_LOADING_ENTITY &&
           strcmp (element_name, "text") == 0)
    {
      const char *id_str, *text_str, *font_str, *color_str;
      RutText *text;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "id",
                                        &id_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "text",
                                        &text_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "font",
                                        &font_str,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL,
                                        "color",
                                        &color_str,
                                        G_MARKUP_COLLECT_INVALID))
          return;

      text = rut_text_new_with_text (engine->ctx, font_str, text_str);

      if (!parse_and_set_id (loader, id_str, text, error))
        return;

      if (color_str)
        {
          CoglColor color;
          rut_color_init_from_string (engine->ctx, &color, color_str);
          rut_text_set_color (text, &color);
        }

      rut_entity_add_component (loader->current_entity, text);
    }
  else if (state == LOADER_STATE_LOADING_MATERIAL_COMPONENT &&
           strcmp (element_name, "texture") == 0)
    {
      const char *id_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "asset",
                                        &id_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      loader->texture_specified = TRUE;
      loader->texture_asset_id = g_ascii_strtoull (id_str, NULL, 10);
    }
  else if (state == LOADER_STATE_LOADING_MATERIAL_COMPONENT &&
           strcmp (element_name, "normal_map") == 0)
    {
      const char *id_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "asset",
                                        &id_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      loader->normal_map_specified = TRUE;
      loader->normal_map_asset_id = g_ascii_strtoull (id_str, NULL, 10);
    }
  else if (state == LOADER_STATE_LOADING_MATERIAL_COMPONENT &&
           strcmp (element_name, "alpha_mask") == 0)
    {
      const char *id_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "asset",
                                        &id_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      loader->alpha_mask_specified = TRUE;
      loader->alpha_mask_asset_id = g_ascii_strtoull (id_str, NULL, 10);
    }
  else if (state == LOADER_STATE_NONE &&
      strcmp (element_name, "controller") == 0)
    {
      const char *id_str;

      loader_push_state (loader, LOADER_STATE_LOADING_TRANSITION);

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "id",
                                        &id_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      loader->current_controller =
        rig_controller_new (loader->engine->ctx, "Controller 0");
      loader->controllers = g_list_prepend (loader->controllers, loader->current_controller);
    }
  else if (state == LOADER_STATE_LOADING_TRANSITION &&
           strcmp (element_name, "property") == 0)
    {
      const char *object_id_str;
      uint32_t object_id;
      RutEntity *object;
      const char *property_name;
      CoglBool animated;
      RigControllerPropData *prop_data;

      /* FIXME: the entity attribute should be renamed because not
         everything being animated is necessarily an entity */
      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "entity",
                                        &object_id_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "name",
                                        &property_name,
                                        G_MARKUP_COLLECT_BOOLEAN,
                                        "animated",
                                        &animated,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      object_id = g_ascii_strtoull (object_id_str, NULL, 10);

      object = loader_find_introspectable (loader, object_id);
      if (!object)
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Invalid object id %d referenced in property element",
                       object_id);
          return;
        }

      prop_data = rig_controller_get_prop_data (loader->current_controller,
                                                object,
                                                property_name);

      if (prop_data->property->spec->animatable)
        {
          if (animated)
            rig_controller_set_property_animated (loader->current_controller,
                                                  prop_data->property,
                                                  TRUE);
        }
      else if (animated)
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "A non-animatable property is marked as animated");
          return;
        }

      if (!prop_data)
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Invalid Entity property referenced in "
                       "property element");
          return;
        }

      loader->current_property = prop_data;

      loader_push_state (loader, LOADER_STATE_LOADING_PROPERTY);
    }
  else if (state == LOADER_STATE_LOADING_PROPERTY &&
           strcmp (element_name, "constant") == 0)
    {
      const char *value_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "value",
                                        &value_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      if (!load_boxed_value (&loader->current_property->constant_value,
                             loader->current_property->constant_value.type,
                             value_str,
                             error))
        return;

      loader_push_state (loader, LOADER_STATE_LOADING_CONSTANT);
    }
  else if (state == LOADER_STATE_LOADING_PROPERTY &&
           strcmp (element_name, "path") == 0)
    {
      loader->current_path =
        rig_path_new (engine->ctx,
                      loader->current_property->property->spec->type);

      loader_push_state (loader, LOADER_STATE_LOADING_PATH);
    }
  else if (state == LOADER_STATE_LOADING_PATH &&
           strcmp (element_name, "node") == 0)
    {
      const char *t_str;
      float t;
      const char *value_str;

      if (!g_markup_collect_attributes (element_name,
                                        attribute_names,
                                        attribute_values,
                                        error,
                                        G_MARKUP_COLLECT_STRING,
                                        "t",
                                        &t_str,
                                        G_MARKUP_COLLECT_STRING,
                                        "value",
                                        &value_str,
                                        G_MARKUP_COLLECT_INVALID))
        return;

      t = g_ascii_strtod (t_str, NULL);

      if (!load_path_node (loader->current_path,
                           t,
                           value_str,
                           error))
        return;
    }
}

static void
parse_end_element (GMarkupParseContext *context,
                   const char *element_name,
                   void *user_data,
                   GError **error)
{
  Loader *loader = user_data;
  int state = loader_get_state (loader);

  if (state == LOADER_STATE_LOADING_ENTITY &&
      strcmp (element_name, "entity") == 0)
    {
      if (loader->is_light && loader->light == NULL)
        loader->light = rut_refable_ref (loader->current_entity);

      loader->entities =
        g_list_prepend (loader->entities, loader->current_entity);

      loader_pop_state (loader);
    }
  else if (state == LOADER_STATE_LOADING_SHAPE_COMPONENT &&
           strcmp (element_name, "shape") == 0)
    {
      RutMaterial *material =
        rut_entity_get_component (loader->current_entity,
                                  RUT_COMPONENT_TYPE_MATERIAL);
      RutAsset *asset = NULL;
      CoglTexture *texture = NULL;
      RutShape *shape;

      /* We need to know the size of the texture before we can create
       * a shape component */
      if (material)
        asset = rut_material_get_texture_asset (material);

      if (asset)
        texture = rut_asset_get_texture (asset);

      if (!texture)
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Can't add shape component without a texture");
          return;
        }

      shape = rut_shape_new (loader->engine->ctx,
                             loader->shaped,
                             cogl_texture_get_width (texture),
                             cogl_texture_get_height (texture));
      rut_entity_add_component (loader->current_entity,
                                shape);

      if (!check_and_set_id (loader, loader->component_id, shape, error))
        return;

      loader_pop_state (loader);
    }
  else if (state == LOADER_STATE_LOADING_DIAMOND_COMPONENT &&
           strcmp (element_name, "diamond") == 0)
    {
      RutMaterial *material =
        rut_entity_get_component (loader->current_entity,
                                  RUT_COMPONENT_TYPE_MATERIAL);
      RutAsset *asset = NULL;
      CoglTexture *texture = NULL;
      RutDiamond *diamond;

      /* We need to know the size of the texture before we can create
       * a diamond component */
      if (material)
        asset = rut_material_get_texture_asset (material);

      if (asset)
        texture = rut_asset_get_texture (asset);

      if (!texture)
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Can't add diamond component without a texture");
          return;
        }

      diamond = rut_diamond_new (loader->engine->ctx,
                                 loader->diamond_size,
                                 cogl_texture_get_width (texture),
                                 cogl_texture_get_height (texture));
      rut_entity_add_component (loader->current_entity,
                                diamond);

      if (!check_and_set_id (loader, loader->component_id, diamond, error))
        return;

      loader_pop_state (loader);
    }
  else if (state == LOADER_STATE_LOADING_MATERIAL_COMPONENT &&
           strcmp (element_name, "material") == 0)
    {
      RutMaterial *material;
      RutAsset *asset;

      material = rut_material_new (loader->engine->ctx, NULL);

      if (!check_and_set_id (loader, loader->component_id, material, error))
        return;

      if (loader->texture_specified)
        {
          asset = loader_find_asset (loader, loader->texture_asset_id);
          if (!asset)
            {
              g_set_error (error,
                           G_MARKUP_ERROR,
                           G_MARKUP_ERROR_INVALID_CONTENT,
                           "Invalid asset id");
              return;
            }
          rut_material_set_texture_asset (material, asset);
        }

      if (loader->normal_map_specified)
        {
          asset = loader_find_asset (loader, loader->normal_map_asset_id);
          if (!asset)
            {
              g_set_error (error,
                           G_MARKUP_ERROR,
                           G_MARKUP_ERROR_INVALID_CONTENT,
                           "Invalid asset id");
              return;
            }
          rut_material_set_normal_map_asset (material, asset);
        }

      if (loader->alpha_mask_specified)
        {
          asset = loader_find_asset (loader, loader->alpha_mask_asset_id);
          if (!asset)
            {
              g_set_error (error,
                           G_MARKUP_ERROR,
                           G_MARKUP_ERROR_INVALID_CONTENT,
                           "Invalid asset id");
              return;
            }
          rut_material_set_alpha_mask_asset (material, asset);
        }

      if (loader->ambient_set)
        rut_material_set_ambient (material, &loader->material_ambient);
      if (loader->diffuse_set)
        rut_material_set_diffuse (material, &loader->material_diffuse);
      if (loader->specular_set)
        rut_material_set_specular (material, &loader->material_specular);
      if (loader->shininess_set)
        rut_material_set_shininess (material, loader->material_shininess);

      rut_entity_add_component (loader->current_entity, material);

      loader_pop_state (loader);
    }
  else
  if (state == LOADER_STATE_LOADING_TRANSITION &&
           strcmp (element_name, "controller") == 0)
    {
      loader_pop_state (loader);
    }
  else if (state == LOADER_STATE_LOADING_PROPERTY &&
           strcmp (element_name, "property") == 0)
    {
      loader_pop_state (loader);
    }
  else if (state == LOADER_STATE_LOADING_PATH &&
           strcmp (element_name, "path") == 0)
    {
      g_assert (loader->current_property->path == NULL);
      loader->current_property->path = loader->current_path;
      loader_pop_state (loader);
    }
  else if (state == LOADER_STATE_LOADING_CONSTANT &&
           strcmp (element_name, "constant") == 0)
    {
      loader_pop_state (loader);
    }
}

static void
parse_error (GMarkupParseContext *context,
             GError *error,
             void *user_data)
{

}

static void
free_id_slice (void *id)
{
  g_slice_free (uint64_t, id);
}

void
rig_load_xml (RigEngine *engine, const char *file)
{
  Loader loader;
  GMarkupParser parser = {
    .start_element = parse_start_element,
    .end_element = parse_end_element,
    .error = parse_error
  };
  char *contents;
  gsize len;
  GError *error = NULL;
  GMarkupParseContext *context;
  GList *l;

  memset (&loader, 0, sizeof (loader));
  loader.engine = engine;

  /* This hash table maps from uint64_t ids to objects while loading */
  loader.id_map = g_hash_table_new_full (g_int64_hash,
                                         g_int64_equal,
                                         free_id_slice,
                                         NULL);

  g_queue_init (&loader.state);
  loader_push_state (&loader, LOADER_STATE_NONE);
  g_queue_push_tail (&loader.state, LOADER_STATE_NONE);

  if (!g_file_get_contents (file,
                            &contents,
                            &len,
                            &error))
    {
      g_warning ("Failed to load ui description: %s", error->message);
      return;
    }

  context = g_markup_parse_context_new (&parser, 0, &loader, NULL);

  if (!g_markup_parse_context_parse (context, contents, len, &error))
    g_warning ("Failed to parse ui description: %s", error->message);

  g_markup_parse_context_free (context);

  g_queue_clear (&loader.state);

  if (loader.device_found)
    {
      engine->device_width = loader.device_width;
      engine->device_height = loader.device_height;

      if (loader.background_set)
        engine->background_color = loader.background;
    }

  rig_engine_free_ui (engine);

  engine->scene = rut_graph_new (engine->ctx);
  for (l = loader.entities; l; l = l->next)
    {
      if (rut_graphable_get_parent (l->data) == NULL)
        rut_graphable_add_child (engine->scene, l->data);
    }

  if (loader.light)
    engine->light = loader.light;

  engine->controllers = loader.controllers;

  engine->assets = loader.assets;

  rig_engine_handle_ui_update (engine);

  rut_shell_queue_redraw (engine->ctx->shell);

  g_hash_table_destroy (loader.id_map);

  g_print ("File Loaded\n");
}
