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

#include "rut-image-source.h"

struct _RutImageSource
{
  RutObjectProps _parent;
  int ref_count;
  CoglTexture *texture;
  CoglGstVideoSink *sink;
  GstElement *pipeline;
  GstElement *bin;
  CoglBool is_video;
};

static CoglBool
_rut_image_source_video_loop (GstBus *bus,
                              GstMessage *msg,
                              void *data)
{
  RutImageSource *source = (RutImageSource*) data;
  switch (GST_MESSAGE_TYPE(msg))
    {
      case GST_MESSAGE_EOS:
        gst_element_seek (source->pipeline, 1.0, GST_FORMAT_TIME,
                          GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0,
                          GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        break;
      default:
        break;
    }
  return TRUE;
}

static void
_rut_image_source_video_stop (RutImageSource *source)
{
  if (source->sink)
    {
      gst_element_set_state (source->pipeline, GST_STATE_NULL);
      gst_object_unref (source->sink);
    }
}

static void
_rut_image_source_video_play (RutImageSource *source,
                              RutContext *ctx,
                              const char *path)
{
  GstBus* bus;
  char *uri;
  char *filename = g_build_filename (ctx->assets_location, path, NULL);

  _rut_image_source_video_stop (source);

  source->sink = cogl_gst_video_sink_new (ctx->cogl_context);
  source->pipeline = gst_pipeline_new ("renderer");
  source->bin = gst_element_factory_make ("playbin", NULL);

  uri = g_strconcat ("file://", filename, NULL);
  g_object_set (G_OBJECT (source->bin), "video-sink",
                GST_ELEMENT (source->sink), NULL);
  g_object_set (G_OBJECT (source->bin), "uri", uri, NULL);
  gst_bin_add (GST_BIN (source->pipeline), source->bin);

  bus = gst_pipeline_get_bus (GST_PIPELINE (source->pipeline));

  gst_element_set_state (source->pipeline, GST_STATE_PLAYING);
  gst_bus_add_watch (bus, _rut_image_source_video_loop, source);

  g_free (uri);
  g_free (filename);
  gst_object_unref (bus);
}

static void
_rut_image_source_ready_for_use (gpointer instance,
                                 gpointer user_data)
{
  RutImageSource *src = (RutImageSource*) user_data;
  src->is_video = TRUE;
}

static void
_rut_image_source_free (void *object)
{
  RutImageSource *source = object;

  _rut_image_source_video_stop (source);
}

static RutRefCountableVTable _rut_image_source_ref_countable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rut_image_source_free
};

RutType rut_image_source_type;

void
_rut_image_source_init_type (void)
{
  rut_type_init (&rut_image_source_type, "RutImageSource");
  rut_type_add_interface (&rut_image_source_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RutImageSource, ref_count),
                          &_rut_image_source_ref_countable);
}

RutImageSource*
rut_image_source_new (RutContext *ctx,
                      RutAsset *asset,
                      RutImageSourceReadyCallback callback,
                      void *user_data)
{
  RutImageSource *source = g_slice_new0 (RutImageSource);
  rut_object_init (&source->_parent, &rut_image_source_type);
  source->ref_count = 1;

  source->sink = NULL;
  source->texture = NULL;
  source->is_video = FALSE;

  if (rut_asset_get_is_video (asset))
    {
      _rut_image_source_video_play (source, ctx,
                                    rut_asset_get_path (asset));
       g_signal_connect (source->sink, "pipeline_ready", (GCallback) callback,
                         user_data);

       g_signal_connect (source->sink, "pipeline_ready",
                        (GCallback) _rut_image_source_ready_for_use, source);
    }
  else if (rut_asset_get_texture (asset))
    {
      source->texture = rut_asset_get_texture (asset);
      callback (NULL, user_data);
    }

  return source;
}

CoglTexture*
rut_image_source_get_texture (RutImageSource *source)
{
  return source->texture;
}

CoglGstVideoSink*
rut_image_source_get_sink (RutImageSource *source)
{
  return source->sink;
}

CoglBool
rut_image_source_get_is_video (RutImageSource *source)
{
  return source->is_video;
}
