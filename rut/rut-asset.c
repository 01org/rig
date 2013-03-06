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
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <glib.h>

#include <cogl/cogl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "rut-context.h"
#include "rut-interfaces.h"
#include "rut-asset.h"
#include "rut-util.h"
#include "rut-mesh-ply.h"

#if 0
enum {
  ASSET_N_PROPS
};
#endif

struct _RutAsset
{
  RutObjectProps _parent;

  int ref_count;

  RutContext *ctx;

#if 0
  RutSimpleIntrospectableProps introspectable;
  RutProperty props[ASSET_N_PROPS];
#endif

  RutAssetType type;

  char *path;
  CoglTexture *texture;
  RutMesh  *mesh;

  GList *inferred_tags;

};

#if 0
static RutPropertySpec _asset_prop_specs[] = {
  { 0 }
};
#endif

#if 0
static RutIntrospectableVTable _asset_introspectable_vtable = {
  rut_simple_introspectable_lookup_property,
  rut_simple_introspectable_foreach_property
};
#endif

static void
_rut_asset_free (void *object)
{
  RutAsset *asset = object;

  if (asset->texture)
    cogl_object_unref (asset->texture);

  if (asset->path)
    g_free (asset->path);

  //rut_simple_introspectable_destroy (asset);

  g_slice_free (RutAsset, asset);
}

static RutRefCountableVTable _rut_asset_ref_countable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_asset_free
};

RutType rut_asset_type;

void
_rut_asset_type_init (void)
{
  rut_type_init (&rut_asset_type, "RigAsset");
  rut_type_add_interface (&rut_asset_type,
                           RUT_INTERFACE_ID_REF_COUNTABLE,
                           offsetof (RutAsset, ref_count),
                           &_rut_asset_ref_countable);

#if 0
  rut_type_add_interface (&_asset_type,
                          RUT_INTERFACE_ID_INTROSPECTABLE,
                          0, /* no implied properties */
                          &_asset_introspectable_vtable);
  rut_type_add_interface (&_asset_type,
                          RUT_INTERFACE_ID_SIMPLE_INTROSPECTABLE,
                          offsetof (Asset, introspectable),
                          NULL); /* no implied vtable */
#endif
}

/* These should be sorted in descending order of size to
 * avoid gaps due to attributes being naturally aligned. */
static RutPLYAttribute ply_attributes[] =
{
  {
    .name = "cogl_position_in",
    .properties = {
      { "x" },
      { "y" },
      { "z" },
    },
    .n_properties = 3,
    .min_components = 1,
  },
  {
    .name = "cogl_normal_in",
    .properties = {
      { "nx" },
      { "ny" },
      { "nz" },
    },
    .n_properties = 3,
    .min_components = 3,
    .pad_n_components = 3,
    .pad_type = RUT_ATTRIBUTE_TYPE_FLOAT,
  },
  {
    .name = "cogl_tex_coord0_in",
    .properties = {
      { "s" },
      { "t" },
      { "r" },
    },
    .n_properties = 3,
    .min_components = 2,
  },
  {
    .name = "tangent",
    .properties = {
      { "tanx" },
      { "tany" },
      { "tanz" }
    },
    .n_properties = 3,
    .min_components = 3,
    .pad_n_components = 3,
    .pad_type = RUT_ATTRIBUTE_TYPE_FLOAT,
  },
  {
    .name = "cogl_color_in",
    .properties = {
      { "red" },
      { "green" },
      { "blue" },
      { "alpha" }
    },
    .n_properties = 4,
    .normalized = TRUE,
    .min_components = 3,
  }
};

static RutAsset *
rut_asset_new_full (RutContext *ctx,
                    const char *path,
                    RutAssetType type)
{
  RutAsset *asset = g_slice_new0 (RutAsset);
  const char *real_path;
  char *full_path;

#ifndef __ANDROID__
  if (type == RUT_ASSET_TYPE_BUILTIN)
    {
      full_path = rut_find_data_file (path);
      if (full_path == NULL)
        full_path = g_strdup (path);
    }
  else
    full_path = g_build_filename (ctx->assets_location, path, NULL);
  real_path = full_path;
#else
  real_path = path;
#endif

  asset->ref_count = 1;

  asset->ctx = ctx;

  asset->type = type;

  switch (type)
    {
    case RUT_ASSET_TYPE_BUILTIN:
    case RUT_ASSET_TYPE_TEXTURE:
    case RUT_ASSET_TYPE_NORMAL_MAP:
    case RUT_ASSET_TYPE_ALPHA_MASK:
      {
        CoglError *error = NULL;

        asset->texture = rut_load_texture (ctx, real_path, &error);

        if (!asset->texture)
          {
            g_slice_free (RutAsset, asset);
            g_warning ("Failed to load asset texture: %s", error->message);
            cogl_error_free (error);
            asset = NULL;
            goto DONE;
          }

        break;
      }
    case RUT_ASSET_TYPE_PLY_MODEL:
      {
        RutPLYAttributeStatus padding_status[G_N_ELEMENTS (ply_attributes)];
        GError *error = NULL;

        asset->mesh = rut_mesh_new_from_ply (ctx,
                                             real_path,
                                             ply_attributes,
                                             G_N_ELEMENTS (ply_attributes),
                                             padding_status,
                                             &error);
        if (!asset->mesh)
          {
            g_slice_free (RutAsset, asset);
            g_warning ("could not load model %s: %s", path, error->message);
            g_error_free (error);
            asset = NULL;
            goto DONE;
          }

        break;
      }
    }
  asset->path = g_strdup (path);

  rut_object_init (&asset->_parent, &rut_asset_type);

  //rut_simple_introspectable_init (asset);

DONE:

#ifndef __ANDROID__
  g_free (full_path);
#endif

  return asset;
}

static CoglBitmap *
bitmap_new_from_pixbuf (CoglContext *ctx,
                        GdkPixbuf *pixbuf)
{
  CoglBool has_alpha;
  GdkColorspace color_space;
  CoglPixelFormat pixel_format;
  int width;
  int height;
  int rowstride;
  int bits_per_sample;
  int n_channels;
  CoglBitmap *bmp;

  /* Get pixbuf properties */
  has_alpha       = gdk_pixbuf_get_has_alpha (pixbuf);
  color_space     = gdk_pixbuf_get_colorspace (pixbuf);
  width           = gdk_pixbuf_get_width (pixbuf);
  height          = gdk_pixbuf_get_height (pixbuf);
  rowstride       = gdk_pixbuf_get_rowstride (pixbuf);
  bits_per_sample = gdk_pixbuf_get_bits_per_sample (pixbuf);
  n_channels      = gdk_pixbuf_get_n_channels (pixbuf);

  /* According to current docs this should be true and so
   * the translation to cogl pixel format below valid */
  g_assert (bits_per_sample == 8);

  if (has_alpha)
    g_assert (n_channels == 4);
  else
    g_assert (n_channels == 3);

  /* Translate to cogl pixel format */
  switch (color_space)
    {
    case GDK_COLORSPACE_RGB:
      /* The only format supported by GdkPixbuf so far */
      pixel_format = has_alpha ?
	COGL_PIXEL_FORMAT_RGBA_8888 :
	COGL_PIXEL_FORMAT_RGB_888;
      break;

    default:
      /* Ouch, spec changed! */
      g_object_unref (pixbuf);
      return FALSE;
    }

  /* We just use the data directly from the pixbuf so that we don't
   * have to copy to a seperate buffer.
   */
  bmp = cogl_bitmap_new_for_data (ctx,
                                  width,
                                  height,
                                  pixel_format,
                                  rowstride,
                                  gdk_pixbuf_get_pixels (pixbuf));

  return bmp;
}

RutAsset *
rut_asset_new_from_data (RutContext *ctx,
                         const char *path,
                         RutAssetType type,
                         uint8_t *data,
                         size_t len)
{
  RutAsset *asset = g_slice_new0 (RutAsset);

  rut_object_init (&asset->_parent, &rut_asset_type);

  asset->ref_count = 1;

  asset->ctx = ctx;

  asset->type = type;

  switch (type)
    {
    case RUT_ASSET_TYPE_BUILTIN:
    case RUT_ASSET_TYPE_TEXTURE:
    case RUT_ASSET_TYPE_NORMAL_MAP:
    case RUT_ASSET_TYPE_ALPHA_MASK:
      {
        GInputStream *istream = g_memory_input_stream_new_from_data (data, len, NULL);
        GError *error = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream (istream, NULL, &error);
        CoglBitmap *bitmap;
        CoglError *cogl_error = NULL;

        if (!pixbuf)
          {
            g_slice_free (RutAsset, asset);
            g_warning ("Failed to load asset texture: %s", error->message);
            g_error_free (error);
            return NULL;
          }

        g_object_unref (istream);

        bitmap = bitmap_new_from_pixbuf (ctx->cogl_context, pixbuf);

        asset->texture = COGL_TEXTURE (
          cogl_texture_2d_new_from_bitmap (bitmap,
                                           COGL_PIXEL_FORMAT_ANY,
                                           &cogl_error));

        cogl_object_unref (bitmap);
        g_object_unref (pixbuf);

        if (!asset->texture)
          {
            g_slice_free (RutAsset, asset);
            g_warning ("Failed to load asset texture: %s", cogl_error->message);
            cogl_error_free (cogl_error);
            return NULL;
          }

        break;
      }
    case RUT_ASSET_TYPE_PLY_MODEL:
      {
        RutPLYAttributeStatus padding_status[G_N_ELEMENTS (ply_attributes)];
        GError *error = NULL;

        asset->mesh = rut_mesh_new_from_ply_data (ctx,
                                                  data,
                                                  len,
                                                  ply_attributes,
                                                  G_N_ELEMENTS (ply_attributes),
                                                  padding_status,
                                                  &error);
        if (!asset->mesh)
          {
            g_slice_free (RutAsset, asset);
            g_warning ("could not load model %s: %s", path, error->message);
            g_error_free (error);
            return NULL;
          }

        break;
      }
    }

  asset->path = g_strdup (path);

  return asset;
}

RutAsset *
rut_asset_new_builtin (RutContext *ctx,
                       const char *path)
{
  return rut_asset_new_full (ctx, path, RUT_ASSET_TYPE_BUILTIN);
}

RutAsset *
rut_asset_new_texture (RutContext *ctx,
                       const char *path)
{
  return rut_asset_new_full (ctx, path, RUT_ASSET_TYPE_TEXTURE);
}

RutAsset *
rut_asset_new_normal_map (RutContext *ctx,
                          const char *path)
{
  return rut_asset_new_full (ctx, path, RUT_ASSET_TYPE_NORMAL_MAP);
}

RutAsset *
rut_asset_new_alpha_mask (RutContext *ctx,
                          const char *path)
{
  return rut_asset_new_full (ctx, path, RUT_ASSET_TYPE_ALPHA_MASK);
}

RutAsset *
rut_asset_new_ply_model (RutContext *ctx,
                         const char *path)
{
  return rut_asset_new_full (ctx, path, RUT_ASSET_TYPE_PLY_MODEL);
}

RutAssetType
rut_asset_get_type (RutAsset *asset)
{
  return asset->type;
}

const char *
rut_asset_get_path (RutAsset *asset)
{
  return asset->path;
}

RutContext *
rut_asset_get_context (RutAsset *asset)
{
  return asset->ctx;
}

CoglTexture *
rut_asset_get_texture (RutAsset *asset)
{
  return asset->texture;
}

RutMesh *
rut_asset_get_mesh (RutAsset *asset)
{
  return asset->mesh;
}

static GList *
copy_tags (const GList *tags)
{
  const GList *l;
  GList *copy = NULL;
  for (l = tags; l; l = l->next)
    {
      const char *tag = g_intern_string (l->data);
      copy = g_list_prepend (copy, (char *)tag);
    }
  return copy;
}

void
rut_asset_set_inferred_tags (RutAsset *asset,
                             const GList *inferred_tags)
{
  asset->inferred_tags = g_list_concat (asset->inferred_tags,
                                        copy_tags (inferred_tags));
}

const GList *
rut_asset_get_inferred_tags (RutAsset *asset)
{
  return asset->inferred_tags;
}

CoglBool
rut_asset_has_tag (RutAsset *asset, const char *tag)
{
  GList *l;

  for (l = asset->inferred_tags; l; l = l->next)
    if (strcmp (tag, l->data) == 0)
      return TRUE;
  return FALSE;
}

static const char *
get_extension (const char *path)
{
  const char *ext = strrchr (path, '.');
  return ext ? ext + 1 : NULL;
}

CoglBool
rut_file_info_is_asset (GFileInfo *info, const char *name)
{
  const char *content_type = g_file_info_get_content_type (info);
  char *mime_type = g_content_type_get_mime_type (content_type);
  const char *ext;
  if (mime_type)
    {
      if (strncmp (mime_type, "image/", 6) == 0)
        {
          g_free (mime_type);
          return TRUE;
        }
      g_free (mime_type);
    }

  ext = get_extension (name);
  if (ext && strcmp (ext, "ply") == 0)
    return TRUE;

  return FALSE;
}

GList *
rut_infer_asset_tags (RutContext *ctx, GFileInfo *info, GFile *asset_file)
{
  GFile *assets_dir = g_file_new_for_path (ctx->assets_location);
  GFile *dir = g_file_get_parent (asset_file);
  char *basename;
  const char *content_type = g_file_info_get_content_type (info);
  char *mime_type = g_content_type_get_mime_type (content_type);
  const char *ext;
  GList *inferred_tags = NULL;

  while (dir && !g_file_equal (assets_dir, dir))
    {
      basename = g_file_get_basename (dir);
      inferred_tags =
        g_list_prepend (inferred_tags, (char *)g_intern_string (basename));
      g_free (basename);
      dir = g_file_get_parent (dir);
    }

  if (mime_type)
    {
      if (strncmp (mime_type, "image/", 6) == 0)
        inferred_tags =
          g_list_prepend (inferred_tags, (char *)g_intern_string ("image"));
      inferred_tags =
        g_list_prepend (inferred_tags, (char *)g_intern_string ("img"));

      if (rut_util_find_tag (inferred_tags, "normal-maps"))
        {
          inferred_tags =
            g_list_prepend (inferred_tags,
                            (char *)g_intern_string ("map"));
          inferred_tags =
            g_list_prepend (inferred_tags,
                            (char *)g_intern_string ("normal-map"));
          inferred_tags =
            g_list_prepend (inferred_tags,
                            (char *)g_intern_string ("bump-map"));
        }
      else if (rut_util_find_tag (inferred_tags, "alpha-masks"))
        {
          inferred_tags =
            g_list_prepend (inferred_tags,
                            (char *)g_intern_string ("alpha-mask"));
          inferred_tags =
            g_list_prepend (inferred_tags,
                            (char *)g_intern_string ("mask"));
        }
    }

  basename = g_file_get_basename (asset_file);
  ext = get_extension (basename);
  if (ext && strcmp (ext, "ply") == 0)
    {
      inferred_tags =
        g_list_prepend (inferred_tags, (char *)g_intern_string ("ply"));
      inferred_tags =
        g_list_prepend (inferred_tags, (char *)g_intern_string ("mesh"));
      inferred_tags =
        g_list_prepend (inferred_tags, (char *)g_intern_string ("model"));
    }
  g_free (basename);

  return inferred_tags;
}

void
rut_asset_add_inferred_tag (RutAsset *asset,
                            const char *tag)
{
  asset->inferred_tags =
    g_list_prepend (asset->inferred_tags, (char *)g_intern_string (tag));
}
