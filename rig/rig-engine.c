/*
 * Rig
 *
 * UI Engine & Editor
 *
 * Copyright (C) 2012,2013  Intel Corporation.
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

#include <sys/stat.h>
#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <cogl/cogl.h>
#ifdef USE_SDL
#include <cogl/cogl-sdl.h>
#endif

#ifdef USE_GTK
#include <glib-object.h>
#include <gtk/gtk.h>
#endif

#include <clib.h>

#include <rut.h>
#include <rut-bin.h>

#ifdef RIG_EDITOR_ENABLED
#include "rig-undo-journal.h"
#include "rig-slave-master.h"
#include "rig-editor.h"
#endif

#include "rig-engine.h"
#include "rig-controller.h"
#include "rig-load-save.h"
#include "rig-renderer.h"
#include "rig-defines.h"
#ifdef HAVE_OSX
#include "rig-osx.h"
#endif
#ifdef USE_GTK
#include "rig-application.h"
#endif /* USE_GTK */
#include "rig-split-view.h"
#include "rig-rpc-network.h"
#include "rig.pb-c.h"
#include "rig-frontend.h"
#include "rig-simulator.h"
#include "rig-image-source.h"

#include "components/rig-camera.h"

//#define DEVICE_WIDTH 480.0
//#define DEVICE_HEIGHT 800.0
#define DEVICE_WIDTH 720.0
#define DEVICE_HEIGHT 1280.0

static rut_property_spec_t _rig_engine_prop_specs[] = {
    { .name = "width",
      .flags = RUT_PROPERTY_FLAG_READABLE,
      .type = RUT_PROPERTY_TYPE_FLOAT,
      .data_offset = offsetof(rig_engine_t, window_width) },
    { .name = "height",
      .flags = RUT_PROPERTY_FLAG_READABLE,
      .type = RUT_PROPERTY_TYPE_FLOAT,
      .data_offset = offsetof(rig_engine_t, window_height) },
    { .name = "device_width",
      .flags = RUT_PROPERTY_FLAG_READABLE,
      .type = RUT_PROPERTY_TYPE_FLOAT,
      .data_offset = offsetof(rig_engine_t, device_width) },
    { .name = "device_height",
      .flags = RUT_PROPERTY_FLAG_READABLE,
      .type = RUT_PROPERTY_TYPE_FLOAT,
      .data_offset = offsetof(rig_engine_t, device_height) },
    { 0 }
};

static rut_traverse_visit_flags_t
scenegraph_pre_paint_cb(rut_object_t *object, int depth, void *user_data)
{
    rut_paint_context_t *rut_paint_ctx = user_data;
    rut_object_t *camera = rut_paint_ctx->camera;
    cg_framebuffer_t *fb = rut_camera_get_framebuffer(camera);

#if 0
    if (rut_object_get_type (object) == &rut_camera_type)
    {
        c_debug ("%*sCamera = %p\n", depth, "", object);
        rut_camera_flush (RUT_CAMERA (object));
        return RUT_TRAVERSE_VISIT_CONTINUE;
    }
    else
#endif

    if (rut_object_get_type(object) == &rut_ui_viewport_type) {
        rut_ui_viewport_t *ui_viewport = RUT_UI_VIEWPORT(object);
#if 0
        c_debug ("%*sPushing clip = %f %f\n",
                 depth, "",
                 rut_ui_viewport_get_width (ui_viewport),
                 rut_ui_viewport_get_height (ui_viewport));
#endif
        cg_framebuffer_push_rectangle_clip(
            fb,
            0,
            0,
            rut_ui_viewport_get_width(ui_viewport),
            rut_ui_viewport_get_height(ui_viewport));
    }

    if (rut_object_is(object, RUT_TRAIT_ID_TRANSFORMABLE)) {
        // c_debug ("%*sTransformable = %p\n", depth, "", object);
        const cg_matrix_t *matrix = rut_transformable_get_matrix(object);
        // cg_debug_matrix_print (matrix);
        cg_framebuffer_push_matrix(fb);
        cg_framebuffer_transform(fb, matrix);
    }

    if (rut_object_is(object, RUT_TRAIT_ID_PAINTABLE)) {
        rut_paintable_vtable_t *vtable =
            rut_object_get_vtable(object, RUT_TRAIT_ID_PAINTABLE);
        vtable->paint(object, rut_paint_ctx);
    }

    /* XXX:
     * How can we maintain state between the pre and post stages?  Is it
     * ok to just "sub-class" the paint context and maintain a stack of
     * state that needs to be shared with the post paint code.
     */

    return RUT_TRAVERSE_VISIT_CONTINUE;
}

static rut_traverse_visit_flags_t
scenegraph_post_paint_cb(rut_object_t *object, int depth, void *user_data)
{
    rut_paint_context_t *rut_paint_ctx = user_data;
    cg_framebuffer_t *fb = rut_camera_get_framebuffer(rut_paint_ctx->camera);

#if 0
    if (rut_object_get_type (object) == &rut_camera_type)
    {
        rut_camera_end_frame (RUT_CAMERA (object));
        return RUT_TRAVERSE_VISIT_CONTINUE;
    }
    else
#endif

    if (rut_object_get_type(object) == &rut_ui_viewport_type) {
        cg_framebuffer_pop_clip(fb);
    }

    if (rut_object_is(object, RUT_TRAIT_ID_TRANSFORMABLE)) {
        cg_framebuffer_pop_matrix(fb);
    }

    return RUT_TRAVERSE_VISIT_CONTINUE;
}

void
rig_engine_paint(rig_engine_t *engine)
{
    cg_framebuffer_t *fb = engine->frontend->onscreen->cg_onscreen;
    rig_paint_context_t paint_ctx;
    rut_paint_context_t *rut_paint_ctx = &paint_ctx._parent;

    rut_camera_set_framebuffer(engine->camera_2d, fb);

#warning "FIXME: avoid clear overdraw between engine_paint and camera_view_paint"
    cg_framebuffer_clear4f(
        fb, CG_BUFFER_BIT_COLOR | CG_BUFFER_BIT_DEPTH, 0.9, 0.9, 0.9, 1);

    paint_ctx.engine = engine;
    paint_ctx.renderer = engine->renderer;

    paint_ctx.pass = RIG_PASS_COLOR_BLENDED;
    rut_paint_ctx->camera = engine->camera_2d;

    rut_camera_flush(engine->camera_2d);
    rut_paint_graph_with_layers(engine->root,
                                scenegraph_pre_paint_cb,
                                scenegraph_post_paint_cb,
                                rut_paint_ctx);
    rut_camera_end_frame(engine->camera_2d);

    cg_onscreen_swap_buffers(CG_ONSCREEN(fb));
}

static void
rig_engine_set_current_ui(rig_engine_t *engine, rig_ui_t *ui)
{
    rig_camera_view_set_ui(engine->main_camera_view, ui);
    engine->current_ui = ui;
    rut_shell_queue_redraw(engine->shell);
}

void
rig_engine_allocate(rig_engine_t *engine)
{
    // engine->main_width = engine->window_width - engine->left_bar_width -
    // engine->right_bar_width;
    // engine->main_height = engine->window_height - engine->top_bar_height -
    // engine->bottom_bar_height;

    rut_sizable_set_size(
        engine->top_stack, engine->window_width, engine->window_height);

#ifdef RIG_EDITOR_ENABLED
    if (engine->frontend && engine->frontend_id == RIG_FRONTEND_ID_EDITOR) {
        if (engine->resize_handle_transform) {
            rut_transform_t *transform = engine->resize_handle_transform;

            rut_transform_init_identity(transform);
            rut_transform_translate(transform,
                                    engine->window_width - 18.0f,
                                    engine->window_height - 18.0f,
                                    0.0f);
        }
    }
#endif

    /* Update the window camera */
    rut_camera_set_projection_mode(engine->camera_2d,
                                   RUT_PROJECTION_ORTHOGRAPHIC);
    rut_camera_set_orthographic_coordinates(
        engine->camera_2d, 0, 0, engine->window_width, engine->window_height);
    rut_camera_set_near_plane(engine->camera_2d, -1);
    rut_camera_set_far_plane(engine->camera_2d, 100);

    rut_camera_set_viewport(
        engine->camera_2d, 0, 0, engine->window_width, engine->window_height);
}

void
rig_engine_resize(rig_engine_t *engine, int width, int height)
{
    engine->window_width = width;
    engine->window_height = height;

    rut_property_dirty(&engine->shell->property_ctx,
                       &engine->properties[RIG_ENGINE_PROP_WIDTH]);
    rut_property_dirty(&engine->shell->property_ctx,
                       &engine->properties[RIG_ENGINE_PROP_HEIGHT]);

    rig_engine_allocate(engine);
}

void
rig_engine_set_play_mode_ui(rig_engine_t *engine, rig_ui_t *ui)
{
    if (engine->frontend)
        c_return_if_fail(engine->frontend->ui_update_pending == false);

    // bool first_ui = (engine->edit_mode_ui == NULL &&
    //                 engine->play_mode_ui == NULL &&
    //                 ui != NULL);

    if (engine->play_mode_ui == ui)
        return;

    if (engine->play_mode_ui) {
        rig_ui_reap(engine->play_mode_ui);
        rut_object_release(engine->play_mode_ui, engine);
        engine->play_mode_ui = NULL;
    }

    if (ui) {
        engine->play_mode_ui = rut_object_claim(ui, engine);
        rig_code_update_dso(engine, ui->dso_data, ui->dso_len);
    }

    // if (engine->edit_mode_ui == NULL && engine->play_mode_ui == NULL)
    //  free_shared_ui_state (engine);

    if (engine->play_mode) {
        rig_engine_set_current_ui(engine, ui);
        rig_ui_resume(ui);
    } else if (ui)
        rig_ui_suspend(ui);

    // if (!ui)
    //  return;

    // if (first_ui)
    //  setup_shared_ui_state (engine);
}

void
rig_engine_set_edit_mode_ui(rig_engine_t *engine, rig_ui_t *ui)
{
    c_return_if_fail(engine->simulator ||
                     engine->frontend->ui_update_pending == false);
    c_return_if_fail(engine->play_mode == false);

    // bool first_ui = (engine->edit_mode_ui == NULL &&
    //                 engine->play_mode_ui == NULL &&
    //                 ui != NULL);

    if (engine->edit_mode_ui == ui)
        return;

    c_return_if_fail(engine->frontend_id == RIG_FRONTEND_ID_EDITOR);

#if RIG_EDITOR_ENABLED
    /* Updating the edit mode ui implies we need to also replace
     * any play mode ui too... */
    rig_engine_set_play_mode_ui(engine, NULL);

    if (engine->frontend) {
        rig_controller_view_set_controller(engine->controller_view, NULL);

        rig_editor_clear_search_results(engine->editor);
        rig_editor_free_result_input_closures(engine->editor);

        if (engine->grid_prim) {
            cg_object_unref(engine->grid_prim);
            engine->grid_prim = NULL;
        }
    }

    if (engine->play_camera_handle) {
        rut_object_unref(engine->play_camera_handle);
        engine->play_camera_handle = NULL;
    }

    if (engine->light_handle) {
        rut_object_unref(engine->light_handle);
        engine->light_handle = NULL;
    }

    if (engine->edit_mode_ui) {
        rig_ui_reap(engine->edit_mode_ui);
        rut_object_release(engine->edit_mode_ui, engine);
    }

    if (ui)
        engine->edit_mode_ui = rut_object_claim(ui, engine);

    rig_engine_set_current_ui(engine, engine->edit_mode_ui);

    if (ui)
        rig_ui_resume(ui);

#endif /* RIG_EDITOR_ENABLED */
}

void
rig_engine_set_ui_load_callback(rig_engine_t *engine,
                                void (*callback)(void *user_data),
                                void *user_data)
{
    engine->ui_load_callback = callback;
    engine->ui_load_data = user_data;
}

void
rig_engine_set_onscreen_size(rig_engine_t *engine, int width, int height)
{
    if (engine->window_width == width && engine->window_height == height)
        return;

    /* XXX: We should be able to have multiple onscreens per
     * engine that may be associated with different cameras
     */
#warning "FIXME: remove engine->window_width/height state"
    rut_shell_onscreen_resize(engine->frontend->onscreen,
                              width, height);
}

static void
ensure_shadow_map(rig_engine_t *engine)
{
    cg_texture_2d_t *color_buffer;
    // rig_ui_t *ui = engine->edit_mode_ui ?
    //  engine->edit_mode_ui : engine->play_mode_ui;

    /*
     * Shadow mapping
     */

    /* Setup the shadow map */

    c_warn_if_fail(engine->shadow_color == NULL);

    color_buffer = cg_texture_2d_new_with_size(engine->shell->cg_device,
                                               engine->device_width * 2,
                                               engine->device_height * 2);

    engine->shadow_color = color_buffer;

    c_warn_if_fail(engine->shadow_fb == NULL);

    /* XXX: Right now there's no way to avoid allocating a color buffer. */
    engine->shadow_fb = cg_offscreen_new_with_texture(color_buffer);
    if (engine->shadow_fb == NULL)
        c_critical("could not create offscreen buffer");

    /* retrieve the depth texture */
    cg_framebuffer_set_depth_texture_enabled(engine->shadow_fb, true);

    c_warn_if_fail(engine->shadow_map == NULL);

    engine->shadow_map = cg_framebuffer_get_depth_texture(engine->shadow_fb);
}

static void
free_shadow_map(rig_engine_t *engine)
{
    if (engine->shadow_map) {
        cg_object_unref(engine->shadow_map);
        engine->shadow_map = NULL;
    }
    if (engine->shadow_fb) {
        cg_object_unref(engine->shadow_fb);
        engine->shadow_fb = NULL;
    }
    if (engine->shadow_color) {
        cg_object_unref(engine->shadow_color);
        engine->shadow_color = NULL;
    }
}

static void
_rig_engine_free(void *object)
{
    rig_engine_t *engine = object;
    rut_shell_t *shell = engine->shell;

    if (engine->frontend) {
#ifdef RIG_EDITOR_ENABLED
        if (engine->frontend_id == RIG_FRONTEND_ID_EDITOR) {
            int i;

            for (i = 0; i < C_N_ELEMENTS(engine->splits); i++)
                rut_object_unref(engine->splits[i]);

            rut_object_unref(engine->top_vbox);
            rut_object_unref(engine->top_hbox);
            rut_object_unref(engine->asset_panel_hbox);
            rut_object_unref(engine->properties_hbox);

            if (engine->transparency_grid)
                rut_object_unref(engine->transparency_grid);

            rut_closure_list_disconnect_all(&engine->tool_changed_cb_list);

            rut_object_unref(engine->objects_selection);
        }
#endif
        _rig_code_fini(engine);

        rig_renderer_fini(engine);

        cg_object_unref(engine->circle_node_attribute);

        free_shadow_map(engine);

        cg_object_unref(engine->default_pipeline);

        _rig_destroy_image_source_wrappers(engine);

#ifdef __APPLE__
        rig_osx_deinit(engine);
#endif

#ifdef USE_GTK
        {
            GApplication *application = g_application_get_default();
            g_object_unref(application);
        }
#endif /* USE_GTK */
    }

    rig_engine_set_edit_mode_ui(engine, NULL);

    rut_shell_remove_input_camera(shell, engine->camera_2d, engine->root);

    rut_object_unref(engine->main_camera_view);
    rut_object_unref(engine->camera_2d);
    rut_object_unref(engine->root);

    if (engine->queued_deletes->len) {
        c_warning("Leaking %d un-garbage-collected objects",
                  engine->queued_deletes->len);
    }
    rut_queue_free(engine->queued_deletes);

    rig_pb_serializer_destroy(engine->ops_serializer);

    rut_memory_stack_free(engine->frame_stack);
    rut_memory_stack_free(engine->sim_frame_stack);

    rut_magazine_free(engine->object_id_magazine);

    rut_introspectable_destroy(engine);

    rut_object_free(rig_engine_t, engine);
}

rut_type_t rig_engine_type;

static void
_rig_engine_init_type(void)
{
    rut_type_t *type = &rig_engine_type;
#define TYPE rig_engine_t

    rut_type_init(type, C_STRINGIFY(TYPE), _rig_engine_free);
    rut_type_add_trait(type,
                       RUT_TRAIT_ID_INTROSPECTABLE,
                       offsetof(TYPE, introspectable),
                       NULL); /* no implied vtable */

#undef TYPE
}

#if 0
static rut_magazine_t *object_id_magazine = NULL;

static void
free_object_id (void *id)
{
    rut_magazine_chunk_free (object_id_magazine, id);
}
#endif

void
finish_ui_load(rig_engine_t *engine, rig_ui_t *ui)
{
    if (engine->frontend_id == RIG_FRONTEND_ID_EDITOR)
        rig_engine_set_edit_mode_ui(engine, ui);
    else
        rig_engine_set_play_mode_ui(engine, ui);

    rut_object_unref(ui);

    if (engine->ui_load_callback)
        engine->ui_load_callback(engine->ui_load_data);
}

static void
finish_ui_load_cb(rig_frontend_t *frontend, void *user_data)
{
    rig_ui_t *ui = user_data;
    rig_engine_t *engine = frontend->engine;

    rut_closure_disconnect(engine->finish_ui_load_closure);
    engine->finish_ui_load_closure = NULL;

    finish_ui_load(engine, ui);
}

void
rig_engine_load_file(rig_engine_t *engine, const char *filename)
{
    rig_ui_t *ui;

    c_return_if_fail(engine->frontend);

    engine->ui_filename = c_strdup(filename);

    ui = rig_load(engine, filename);

    if (!ui) {
        ui = rig_ui_new(engine);
        rig_ui_prepare(ui);
    }

    /*
     * Wait until the simulator is idle before swapping in a new UI...
     */
    if (!engine->frontend->ui_update_pending)
        finish_ui_load(engine, ui);
    else {
        /* Throw away any outstanding closure since it is now redundant... */
        if (engine->finish_ui_load_closure)
            rut_closure_disconnect(engine->finish_ui_load_closure);

        engine->finish_ui_load_closure =
            rig_frontend_add_ui_update_callback(engine->frontend,
                                                finish_ui_load_cb,
                                                ui,
                                                rut_object_unref); /* destroy */
    }
}

void
rig_engine_load_empty_ui(rig_engine_t *engine)
{
    rig_ui_t *ui = rig_ui_new(engine);
    rig_ui_prepare(ui);
    finish_ui_load(engine, ui);
}

static rig_engine_t *
_rig_engine_new_full(rut_shell_t *shell,
                     rig_frontend_t *frontend,
                     rig_simulator_t *simulator)
{
    rig_engine_t *engine = rut_object_alloc0(
        rig_engine_t, &rig_engine_type, _rig_engine_init_type);

    engine->shell = shell;

    engine->headless = engine->shell->headless;

    if (frontend) {
        engine->frontend_id = frontend->id;
        engine->frontend = frontend;
    } else if (simulator) {
        engine->frontend_id = simulator->frontend_id;
        engine->simulator = simulator;
    }

    cg_matrix_init_identity(&engine->identity);

    rut_introspectable_init(engine, _rig_engine_prop_specs, engine->properties);

    engine->object_id_magazine = rut_magazine_new(sizeof(uint64_t), 1000);

    /* The frame stack is a very cheap way to allocate memory that will
     * be automatically freed at the end of the next frame (or current
     * frame if one is already being processed.)
     */
    engine->frame_stack = rut_memory_stack_new(8192);

    /* Since the frame rate of the frontend may not match the frame rate
     * of the simulator, we maintain a separate frame stack for
     * allocations whose lifetime is tied to a simulation frame, not a
     * frontend frame...
     */
    if (frontend)
        engine->sim_frame_stack = rut_memory_stack_new(8192);

    engine->ops_serializer = rig_pb_serializer_new(engine);

    if (frontend) {
        /* By default a rig_pb_serializer_t will use engine->frame_stack,
         * but operations generated in a frontend need to be batched
         * until they can be sent to the simulator which may be longer
         * than one frontend frame so we need to use the sim_frame_stack
         * instead...
         */
        rig_pb_serializer_set_stack(engine->ops_serializer,
                                    engine->sim_frame_stack);
    }

    rig_pb_serializer_set_use_pointer_ids_enabled(engine->ops_serializer, true);

    engine->queued_deletes = rut_queue_new();

    engine->device_width = DEVICE_WIDTH;
    engine->device_height = DEVICE_HEIGHT;

    if (engine->frontend)
        ensure_shadow_map(engine);

    /*
     * Setup the 2D widget scenegraph
     */
    engine->root = rut_graph_new(engine->shell);

    engine->top_stack = rut_stack_new(engine->shell, 1, 1);
    rut_graphable_add_child(engine->root, engine->top_stack);
    rut_object_unref(engine->top_stack);

    engine->camera_2d = rig_camera_new(engine,
                                       -1, /* ortho/vp width */
                                       -1, /* ortho/vp height */
                                       NULL);
    rut_camera_set_clear(engine->camera_2d, false);

    /* XXX: Basically just a hack for now. We should have a
     * rut_shell_window_t type that internally creates a rig_camera_t that can
     * be used when handling input events in device coordinates.
     */
    rut_shell_set_window_camera(shell, engine->camera_2d);

    rut_shell_add_input_camera(shell, engine->camera_2d, engine->root);

    _rig_code_init(engine);

    return engine;
}

rig_engine_t *
rig_engine_new_for_simulator(rut_shell_t *shell,
                             rig_simulator_t *simulator)
{
    return _rig_engine_new_full(shell, NULL, simulator);
}

rig_engine_t *
rig_engine_new_for_frontend(rut_shell_t *shell,
                            rig_frontend_t *frontend)
{
    return _rig_engine_new_full(shell, frontend, NULL);
}

rut_input_event_status_t
rig_engine_input_handler(rut_input_event_t *event,
                         void *user_data)
{
    rig_engine_t *engine = user_data;

#if 0
    if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
        /* Anything that can claim the keyboard focus will do so during
         * motion events so we clear it before running other input
         * callbacks */
        engine->key_focus_callback = NULL;
    }
#endif

    switch (rut_input_event_get_type(event)) {
    case RUT_INPUT_EVENT_TYPE_KEY:
#ifdef RIG_EDITOR_ENABLED
        if (engine->frontend && engine->frontend_id == RIG_FRONTEND_ID_EDITOR &&
            rut_key_event_get_action(event) == RUT_KEY_EVENT_ACTION_DOWN) {
            switch (rut_key_event_get_keysym(event)) {
            case RUT_KEY_s:
                if ((rut_key_event_get_modifier_state(event) &
                     RUT_MODIFIER_CTRL_ON)) {
                    rig_save(engine, engine->ui_filename);
                    return RUT_INPUT_EVENT_STATUS_UNHANDLED;
                }
                break;
            case RUT_KEY_z:
                if ((rut_key_event_get_modifier_state(event) &
                     RUT_MODIFIER_CTRL_ON)) {
                    rig_undo_journal_undo(engine->undo_journal);
                    return RUT_INPUT_EVENT_STATUS_HANDLED;
                }
                break;
            case RUT_KEY_y:
                if ((rut_key_event_get_modifier_state(event) &
                     RUT_MODIFIER_CTRL_ON)) {
                    rig_undo_journal_redo(engine->undo_journal);
                    return RUT_INPUT_EVENT_STATUS_HANDLED;
                }
                break;

#if 1
            /* HACK: Currently it's quite hard to select the play
             * camera because it will usually be positioned far away
             * from the scene. This provides a way to select it by
             * pressing Ctrl+C. Eventually it should be possible to
             * select it using a list of entities somewhere */
            case RUT_KEY_r:
                if ((rut_key_event_get_modifier_state(event) &
                     RUT_MODIFIER_CTRL_ON)) {
                    rig_entity_t *play_camera =
                        engine->play_mode ? engine->play_mode_ui->play_camera
                        : engine->edit_mode_ui->play_camera;

                    rig_select_object(
                        engine, play_camera, RUT_SELECT_ACTION_REPLACE);
                    rig_editor_update_inspector(engine);
                    return RUT_INPUT_EVENT_STATUS_HANDLED;
                }
                break;
#endif
            }
        }
#endif /* RIG_EDITOR_ENABLED */
        break;

    case RUT_INPUT_EVENT_TYPE_MOTION:
    case RUT_INPUT_EVENT_TYPE_TEXT:
    case RUT_INPUT_EVENT_TYPE_DROP_OFFER:
    case RUT_INPUT_EVENT_TYPE_DROP:
    case RUT_INPUT_EVENT_TYPE_DROP_CANCEL:
        break;
    }

    return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

#if 0
static rut_input_event_status_t
add_light_cb (rut_input_region_t *region,
              rut_input_event_t *event,
              void *user_data)
{
    if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
        if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_DOWN)
        {
            c_debug ("Add light!\n");
            return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
    }

    return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}
#endif

void
rig_engine_set_log_op_callback(rig_engine_t *engine,
                               void (*callback)(Rig__Operation *pb_op,
                                                void *user_data),
                               void *user_data)
{
    engine->log_op_callback = callback;
    engine->log_op_data = user_data;
}

void
rig_engine_queue_delete(rig_engine_t *engine, rut_object_t *object)
{
    rut_object_claim(object, engine);
    rut_queue_push_tail(engine->queued_deletes, object);
}

void
rig_engine_garbage_collect(rig_engine_t *engine,
                           void (*object_callback)(rut_object_t *object,
                                                   void *user_data),
                           void *user_data)
{
    rut_queue_item_t *item;

    rut_list_for_each(item, &engine->queued_deletes->items, list_node)
    {
        if (object_callback)
            object_callback(item->data, user_data);
        rut_object_release(item->data, engine);
    }
    rut_queue_clear(engine->queued_deletes);
}

void
rig_engine_set_play_mode_enabled(rig_engine_t *engine, bool enabled)
{
    engine->play_mode = enabled;

    if (engine->play_mode) {
        if (engine->play_mode_ui)
            rig_ui_resume(engine->play_mode_ui);
        rig_engine_set_current_ui(engine, engine->play_mode_ui);
        rig_camera_view_set_play_mode_enabled(engine->main_camera_view, true);
    } else {
        rig_engine_set_current_ui(engine, engine->edit_mode_ui);
        rig_camera_view_set_play_mode_enabled(engine->main_camera_view, false);
        if (engine->play_mode_ui)
            rig_ui_suspend(engine->play_mode_ui);
    }

    if (engine->play_mode_callback)
        engine->play_mode_callback(enabled, engine->play_mode_data);
}

char *
rig_engine_get_object_debug_name(rut_object_t *object)
{
    if (rut_object_get_type(object) == &rig_entity_type)
        return c_strdup_printf(
            "%p(label=\"%s\")", object, rig_entity_get_label(object));
    else if (rut_object_is(object, RUT_TRAIT_ID_COMPONENTABLE)) {
        rut_componentable_props_t *component_props =
            rut_object_get_properties(object, RUT_TRAIT_ID_COMPONENTABLE);
        rig_entity_t *entity = component_props->entity;

        if (entity) {
            const char *entity_label =
                entity ? rig_entity_get_label(entity) : "";
            return c_strdup_printf("%p(label=\"%s\"::%s)",
                                   object,
                                   entity_label,
                                   rut_object_get_type_name(object));
        } else
            return c_strdup_printf(
                "%p(<orphaned>::%s)", object, rut_object_get_type_name(object));
    } else
        return c_strdup_printf(
            "%p(%s)", object, rut_object_get_type_name(object));
}

void
rig_engine_set_play_mode_callback(rig_engine_t *engine,
                                  void (*callback)(bool play_mode,
                                                   void *user_data),
                                  void *user_data)
{
    engine->play_mode_callback = callback;
    engine->play_mode_data = user_data;
}

void
rig_engine_set_apply_op_context(rig_engine_t *engine,
                                rig_engine_op_apply_context_t *ctx)
{
    engine->apply_op_ctx = ctx;
}
