/*
 * Rig
 *
 * UI Engine & Editor
 *
 * Copyright (C) 2012  Intel Corporation
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
 */

#include <config.h>

#include "rig-image-source.h"

struct _rig_image_source_t {
    rut_object_base_t _base;

    rig_engine_t *engine;

    cg_texture_t *texture;

#ifdef USE_GSTREAMER
    CgGstVideoSink *sink;
    GstElement *pipeline;
    GstElement *bin;
#endif

    bool is_video;

    int first_layer;
    bool default_sample;

    rut_list_t changed_cb_list;
    rut_list_t ready_cb_list;
};

typedef struct _image_source_wrappers_t {
    cg_snippet_t *image_source_vertex_wrapper;
    cg_snippet_t *image_source_fragment_wrapper;

    cg_snippet_t *video_source_vertex_wrapper;
    cg_snippet_t *video_source_fragment_wrapper;
} image_source_wrappers_t;

static void
destroy_source_wrapper(gpointer data)
{
    image_source_wrappers_t *wrapper = data;

    if (wrapper->image_source_vertex_wrapper)
        cg_object_unref(wrapper->image_source_vertex_wrapper);
    if (wrapper->image_source_vertex_wrapper)
        cg_object_unref(wrapper->image_source_vertex_wrapper);

    if (wrapper->video_source_vertex_wrapper)
        cg_object_unref(wrapper->video_source_vertex_wrapper);
    if (wrapper->video_source_fragment_wrapper)
        cg_object_unref(wrapper->video_source_fragment_wrapper);

    c_slice_free(image_source_wrappers_t, wrapper);
}

void
_rig_init_image_source_wrappers_cache(rig_engine_t *engine)
{
    engine->image_source_wrappers =
        g_hash_table_new_full(g_direct_hash,
                              g_direct_equal,
                              NULL, /* key destroy */
                              destroy_source_wrapper);
}

void
_rig_destroy_image_source_wrappers(rig_engine_t *engine)
{
    g_hash_table_destroy(engine->image_source_wrappers);
}

static image_source_wrappers_t *
get_image_source_wrappers(rig_engine_t *engine,
                          int layer_index)
{
    image_source_wrappers_t *wrappers = g_hash_table_lookup(
        engine->image_source_wrappers, GUINT_TO_POINTER(layer_index));
    char *wrapper;

    if (wrappers)
        return wrappers;

    wrappers = c_slice_new0(image_source_wrappers_t);

    /* XXX: Note: we use texture2D() instead of the
     * cg_texture_lookup%i wrapper because the _GLOBALS hook comes
     * before the _lookup functions are emitted by Cogl */
    wrapper = c_strdup_printf("vec4\n"
                              "rig_image_source_sample%d (vec2 UV)\n"
                              "{\n"
                              "#if __VERSION__ >= 130\n"
                              "  return texture (cg_sampler%d, UV);\n"
                              "#else\n"
                              "  return texture2D (cg_sampler%d, UV);\n"
                              "#endif\n"
                              "}\n",
                              layer_index,
                              layer_index);

    wrappers->image_source_vertex_wrapper =
        cg_snippet_new(CG_SNIPPET_HOOK_VERTEX_GLOBALS, wrapper, NULL);
    wrappers->image_source_fragment_wrapper =
        cg_snippet_new(CG_SNIPPET_HOOK_FRAGMENT_GLOBALS, wrapper, NULL);

    c_free(wrapper);

    wrapper = c_strdup_printf("vec4\n"
                              "rig_image_source_sample%d (vec2 UV)\n"
                              "{\n"
                              "  return cg_gst_sample_video%d (UV);\n"
                              "}\n",
                              layer_index,
                              layer_index);

    wrappers->video_source_vertex_wrapper =
        cg_snippet_new(CG_SNIPPET_HOOK_VERTEX_GLOBALS, wrapper, NULL);
    wrappers->video_source_fragment_wrapper =
        cg_snippet_new(CG_SNIPPET_HOOK_FRAGMENT_GLOBALS, wrapper, NULL);

    c_free(wrapper);

    g_hash_table_insert(
        engine->image_source_wrappers, GUINT_TO_POINTER(layer_index), wrappers);

    return wrappers;
}

#ifdef USE_GSTREAMER
static gboolean
_rig_image_source_video_loop(GstBus *bus, GstMessage *msg, void *data)
{
    rig_image_source_t *source = (rig_image_source_t *)data;
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        gst_element_seek(source->pipeline,
                         1.0,
                         GST_FORMAT_TIME,
                         GST_SEEK_FLAG_FLUSH,
                         GST_SEEK_TYPE_SET,
                         0,
                         GST_SEEK_TYPE_NONE,
                         GST_CLOCK_TIME_NONE);
        break;
    default:
        break;
    }
    return true;
}

static void
_rig_image_source_video_stop(rig_image_source_t *source)
{
    if (source->sink) {
        gst_element_set_state(source->pipeline, GST_STATE_NULL);
        gst_object_unref(source->sink);
    }
}

static void
_rig_image_source_video_play(rig_image_source_t *source,
                             rig_engine_t *engine,
                             const char *path,
                             const uint8_t *data,
                             size_t len)
{
    GstBus *bus;
    char *uri;
    char *filename = NULL;

    _rig_image_source_video_stop(source);

    source->sink = cg_gst_video_sink_new(engine->ctx->cg_device);
    source->pipeline = gst_pipeline_new("renderer");
    source->bin = gst_element_factory_make("playbin", NULL);

    if (data && len)
        uri = c_strdup_printf("mem://%p:%lu", data, (unsigned long)len);
    else {
        filename = g_build_filename(engine->ctx->assets_location, path, NULL);
        uri = gst_filename_to_uri(filename, NULL);
    }

    g_object_set(
        G_OBJECT(source->bin), "video-sink", GST_ELEMENT(source->sink), NULL);
    g_object_set(G_OBJECT(source->bin), "uri", uri, NULL);
    gst_bin_add(GST_BIN(source->pipeline), source->bin);

    bus = gst_pipeline_get_bus(GST_PIPELINE(source->pipeline));

    gst_element_set_state(source->pipeline, GST_STATE_PLAYING);
    gst_bus_add_watch(bus, _rig_image_source_video_loop, source);

    c_free(uri);
    if (filename)
        c_free(filename);
    gst_object_unref(bus);
}
#endif /* USE_GSTREAMER */

static void
_rig_image_source_free(void *object)
{
#ifdef USE_GSTREAMER
    rig_image_source_t *source = object;

    _rig_image_source_video_stop(source);
#endif
}

rut_type_t rig_image_source_type;

void
_rig_image_source_init_type(void)
{
    rut_type_init(
        &rig_image_source_type, "rig_image_source_t", _rig_image_source_free);
}

#ifdef USE_GSTREAMER
static void
pipeline_ready_cb(gpointer instance, gpointer user_data)
{
    rig_image_source_t *source = (rig_image_source_t *)user_data;

    source->is_video = true;

    rut_closure_list_invoke(
        &source->ready_cb_list, rig_image_source_ready_callback_t, source);
}

static void
new_frame_cb(gpointer instance, gpointer user_data)
{
    rig_image_source_t *source = (rig_image_source_t *)user_data;

    rut_closure_list_invoke(
        &source->changed_cb_list, rig_image_source_changed_callback_t, source);
}
#endif /* USE_GSTREAMER */

rig_image_source_t *
rig_image_source_new(rig_engine_t *engine,
                     rig_asset_t *asset)
{
    rig_image_source_t *source = rut_object_alloc0(rig_image_source_t,
                                                   &rig_image_source_type,
                                                   _rig_image_source_init_type);

    source->engine = engine;
    source->default_sample = true;

    rut_list_init(&source->changed_cb_list);
    rut_list_init(&source->ready_cb_list);

    if (rig_asset_get_is_video(asset)) {
#ifdef USE_GSTREAMER
        _rig_image_source_video_play(source,
                                     engine,
                                     rig_asset_get_path(asset),
                                     rig_asset_get_data(asset),
                                     rig_asset_get_data_len(asset));
        g_signal_connect(source->sink,
                         "pipeline_ready",
                         (GCallback)pipeline_ready_cb,
                         source);
        g_signal_connect(
            source->sink, "new_frame", (GCallback)new_frame_cb, source);
#else
        g_error("FIXME: missing video support on this platform");
#endif
    } else if (rig_asset_get_texture(asset))
        source->texture = rig_asset_get_texture(asset);

    return source;
}

rut_closure_t *
rig_image_source_add_ready_callback(rig_image_source_t *source,
                                    rig_image_source_ready_callback_t callback,
                                    void *user_data,
                                    rut_closure_destroy_callback_t destroy_cb)
{
    if (source->texture) {
        callback(source, user_data);
        return NULL;
    } else
        return rut_closure_list_add(
            &source->ready_cb_list, callback, user_data, destroy_cb);
}

cg_texture_t *
rig_image_source_get_texture(rig_image_source_t *source)
{
    return source->texture;
}

#ifdef USE_GSTREAMER
CgGstVideoSink *
rig_image_source_get_sink(rig_image_source_t *source)
{
    return source->sink;
}
#endif

bool
rig_image_source_get_is_video(rig_image_source_t *source)
{
    return source->is_video;
}

rut_closure_t *
rig_image_source_add_on_changed_callback(
    rig_image_source_t *source,
    rig_image_source_changed_callback_t callback,
    void *user_data,
    rut_closure_destroy_callback_t destroy_cb)
{
    return rut_closure_list_add(
        &source->changed_cb_list, callback, user_data, destroy_cb);
}

void
rig_image_source_set_first_layer(rig_image_source_t *source,
                                 int first_layer)
{
    source->first_layer = first_layer;
}

void
rig_image_source_set_default_sample(rig_image_source_t *source,
                                    bool default_sample)
{
    source->default_sample = default_sample;
}

void
rig_image_source_setup_pipeline(rig_image_source_t *source,
                                cg_pipeline_t *pipeline)
{
    cg_snippet_t *vertex_snippet;
    cg_snippet_t *fragment_snippet;
    image_source_wrappers_t *wrappers =
        get_image_source_wrappers(source->engine, source->first_layer);

    if (!rig_image_source_get_is_video(source)) {
        cg_texture_t *texture = rig_image_source_get_texture(source);

        cg_pipeline_set_layer_texture(pipeline, source->first_layer, texture);

        if (!source->default_sample)
            cg_pipeline_set_layer_combine(
                pipeline, source->first_layer, "RGBA=REPLACE(PREVIOUS)", NULL);

        vertex_snippet = wrappers->image_source_vertex_wrapper;
        fragment_snippet = wrappers->image_source_fragment_wrapper;
    } else {
#ifdef USE_GSTREAMER
        CgGstVideoSink *sink = rig_image_source_get_sink(source);

        cg_gst_video_sink_set_first_layer(sink, source->first_layer);
        cg_gst_video_sink_set_default_sample(sink, true);
        cg_gst_video_sink_setup_pipeline(sink, pipeline);

        vertex_snippet = wrappers->video_source_vertex_wrapper;
        fragment_snippet = wrappers->video_source_fragment_wrapper;
#else
        g_error("FIXME: missing video support for this platform");
#endif
    }

    cg_pipeline_add_snippet(pipeline, vertex_snippet);
    cg_pipeline_add_snippet(pipeline, fragment_snippet);
}

void
rig_image_source_attach_frame(rig_image_source_t *source,
                              cg_pipeline_t *pipeline)
{
    /* NB: For non-video sources we always attach the texture
     * during rig_image_source_setup_pipeline() so we don't
     * have to do anything here.
     */

    if (rig_image_source_get_is_video(source)) {
#ifdef USE_GSTREAMER
        cg_gst_video_sink_attach_frame(rig_image_source_get_sink(source),
                                       pipeline);
#else
        g_error("FIXME: missing video support for this platform");
#endif
    }
}

void
rig_image_source_get_natural_size(rig_image_source_t *source,
                                  float *width,
                                  float *height)
{
    if (rig_image_source_get_is_video(source)) {
#ifdef USE_GSTREAMER
        cg_gst_video_sink_get_natural_size(source->sink, width, height);
#else
        g_error("FIXME: missing video support for this platform");
#endif
    } else {
        cg_texture_t *texture = rig_image_source_get_texture(source);
        *width = cg_texture_get_width(texture);
        *height = cg_texture_get_height(texture);
    }
}
